/*
 * net.c — Networking builtins for Windows (Winsock).
 */

#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

static BuiltinResult bi_net_connect(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR || a[1].type != JKY_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { *out = mkint(-1); return BUILTIN_OK; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)a[1].v.i);

    if (inet_pton(AF_INET, a[0].v.s, &sa.sin_addr) != 1) {
        /* Try DNS resolution */
        struct hostent *he = gethostbyname(a[0].v.s);
        if (!he) { closesocket(s); *out = mkint(-1); return BUILTIN_OK; }
        memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        closesocket(s);
        *out = mkint(-1);
        return BUILTIN_OK;
    }

    *out = mkint((int64_t)s);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_send(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkint(-1); return BUILTIN_OK; }

    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[1].type == JKY_STR) {
        data = (const uint8_t *)a[1].v.s;
        len = strlen(a[1].v.s);
    } else if (a[1].type == JKY_BYTES) {
        data = a[1].v.bytes.data;
        len = a[1].v.bytes.len;
    } else {
        *out = mkint(-1);
        return BUILTIN_OK;
    }

    SOCKET s = (SOCKET)a[0].v.i;
    int sent = send(s, (const char *)data, (int)len, 0);
    *out = mkint((int64_t)sent);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_recv(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkbytes_empty(); return BUILTIN_OK; }

    int64_t want = a[1].type == JKY_INT ? a[1].v.i : 4096;
    if (want <= 0) want = 4096;
    if (want > 64 * 1024 * 1024) want = 64 * 1024 * 1024;

    uint8_t *buf = (uint8_t *)malloc((size_t)want);
    if (!buf) { *out = mkbytes_empty(); return BUILTIN_OK; }

    SOCKET s = (SOCKET)a[0].v.i;
    int got = recv(s, (char *)buf, (int)want, 0);

    Value v;
    v.type = JKY_BYTES;
    v.rc = 0;
    v.v.bytes.data = buf;
    v.v.bytes.len = got > 0 ? (size_t)got : 0;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_close(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mkbool(0); return BUILTIN_OK; }
    SOCKET s = (SOCKET)a[0].v.i;
    *out = mkint(closesocket(s) == 0 ? 1 : 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_resolve(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkstr(""); return BUILTIN_OK; }

    struct hostent *he = gethostbyname(a[0].v.s);
    if (!he || !he->h_addr) { *out = mkstr(""); return BUILTIN_OK; }

    char ip[64];
    inet_ntop(AF_INET, he->h_addr, ip, sizeof(ip));
    *out = mkstr(ip);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_connections(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value arr = mkarray();
    MIB_TCPTABLE_OWNER_PID *tcp = NULL;
    DWORD sz = 0;
    GetExtendedTcpTable(NULL, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    tcp = (MIB_TCPTABLE_OWNER_PID *)malloc(sz);

    if (GetExtendedTcpTable(tcp, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        for (DWORD i = 0; i < tcp->dwNumEntries; i++) {
            MIB_TCPROW_OWNER_PID *r = &tcp->table[i];
            if (r->dwState == MIB_TCP_STATE_ESTAB) {
                Value item = mkdict();
                char local[64], remote[64];
                sprintf(local, "%u.%u.%u.%u:%u",
                        (r->dwLocalAddr >> 0) & 0xFF, (r->dwLocalAddr >> 8) & 0xFF,
                        (r->dwLocalAddr >> 16) & 0xFF, (r->dwLocalAddr >> 24) & 0xFF,
                        ntohs((u_short)r->dwLocalPort));
                sprintf(remote, "%u.%u.%u.%u:%u",
                        (r->dwRemoteAddr >> 0) & 0xFF, (r->dwRemoteAddr >> 8) & 0xFF,
                        (r->dwRemoteAddr >> 16) & 0xFF, (r->dwRemoteAddr >> 24) & 0xFF,
                        ntohs((u_short)r->dwRemotePort));
                dict_set(item.v.dict, "local", mkstr(local));
                dict_set(item.v.dict, "remote", mkstr(remote));
                dict_set(item.v.dict, "pid", mkint((int64_t)r->dwOwningPid));
                arr_push(arr.v.arr, item);
            }
        }
    }

    free(tcp);
    *out = arr;
    return BUILTIN_OK;
}

const Builtin BUILTINS_NET[] = {
    { "net_connect",     2, 2, bi_net_connect     },
    { "net_send",        2, 2, bi_net_send        },
    { "net_recv",        2, 2, bi_net_recv        },
    { "net_close",       1, 1, bi_net_close       },
    { "netstat",         0, 0, bi_net_connections  },
    { "net_connections", 0, 0, bi_net_connections  },
    { "net_listening",   0, 0, bi_net_connections  },
    { "net_resolve",     1, 1, bi_net_resolve     },
    { "net_dns",         1, 1, bi_net_resolve     },
    { NULL, 0, 0, NULL },
};
