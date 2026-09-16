#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int parse_proc_net_tcp(const char *path, Value *arr, int listening_only) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        unsigned laddr, raddr; int lport, rport, st;
        /* format: sl local_address rem_address st ... */
        if (sscanf(line, " %*d: %8x:%x %8x:%x %x",
                   &laddr, &lport, &raddr, &rport, &st) != 5) continue;
        if (listening_only && st != 0x0A) continue;
        Value d = mkdict();
        struct in_addr la = { .s_addr = laddr };
        struct in_addr ra = { .s_addr = raddr };
        dict_set(d.v.dict, "local_addr",  mkstr(inet_ntoa(la)));
        dict_set(d.v.dict, "local_port",  mkint(lport));
        dict_set(d.v.dict, "remote_addr", mkstr(inet_ntoa(ra)));
        dict_set(d.v.dict, "remote_port", mkint(rport));
        dict_set(d.v.dict, "state",       mkint(st));
        arr_push(arr->v.arr, d);
    }
    fclose(f);
    return 0;
}

static BuiltinResult bi_net_stat(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    parse_proc_net_tcp("/proc/net/tcp", &arr, 0);
    parse_proc_net_tcp("/proc/net/tcp6", &arr, 0);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_listening(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    parse_proc_net_tcp("/proc/net/tcp", &arr, 1);
    parse_proc_net_tcp("/proc/net/tcp6", &arr, 1);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_arp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/proc/net/arp", "r");
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        char ip[64], mac[64], dev[64];
        if (sscanf(line, "%63s %*s %*s %63s %*s %63s", ip, mac, dev) == 3) {
            Value d = mkdict();
            dict_set(d.v.dict, "ip",  mkstr(ip));
            dict_set(d.v.dict, "mac", mkstr(mac));
            dict_set(d.v.dict, "dev", mkstr(dev));
            arr_push(arr.v.arr, d);
        }
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_resolve(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkstr(""); return BUILTIN_OK; }
    struct hostent *he = gethostbyname(a[0].v.s);
    if (!he) { *out = mkstr(""); return BUILTIN_OK; }
    struct in_addr addr = *(struct in_addr *)he->h_addr;
    *out = mkstr(inet_ntoa(addr));
    return BUILTIN_OK;
}

static BuiltinResult bi_net_dns(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkarray(); return BUILTIN_OK; }
    Value arr = mkarray();
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(a[0].v.s, NULL, &hints, &res) != 0) { *out = arr; return BUILTIN_OK; }
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        char buf[64] = {0};
        if (p->ai_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in *)p->ai_addr)->sin_addr, buf, sizeof(buf));
        else
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, buf, sizeof(buf));
        if (buf[0]) arr_push(arr.v.arr, mkstr(buf));
    }
    freeaddrinfo(res);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_scan(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    Value arr = mkarray();
    if (a[0].type != VT_STR || a[1].type != VT_ARRAY) { *out = arr; return BUILTIN_OK; }
    for (int i = 0; i < a[1].v.arr->len; i++) {
        Value pv = a[1].v.arr->items[i];
        if (pv.type != VT_INT) continue;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons((uint16_t)pv.v.i);
        inet_pton(AF_INET, a[0].v.s, &sa.sin_addr);
        struct timeval tv = { .tv_sec = 1 };
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int r = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
        close(fd);
        if (r == 0) {
            Value e = mkdict();
            dict_set(e.v.dict, "port", mkint(pv.v.i));
            dict_set(e.v.dict, "state", mkstr("open"));
            arr_push(arr.v.arr, e);
        }
    }
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_proxy(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value d = mkdict();
    const char *hp = getenv("http_proxy"); if (!hp) hp = getenv("HTTP_PROXY");
    const char *sp = getenv("https_proxy"); if (!sp) sp = getenv("HTTPS_PROXY");
    if (hp) dict_set(d.v.dict, "http",  mkstr(hp));
    if (sp) dict_set(d.v.dict, "https", mkstr(sp));
    *out = d;
    return BUILTIN_OK;
}

STUB_ARRAY(bi_net_sockets)
STUB_DICT(bi_net_flow)
STUB_ARRAY(bi_net_ssl_keys)
STUB_ARRAY(bi_net_capture)
STUB_DICT(bi_net_http_inspect)
STUB_ARRAY(bi_net_firewall)
STUB_STR0(bi_net_whois)

const Builtin BUILTINS_NET_EXTRA[] = {
    { "net_stat",        0, 0, bi_net_stat         },
    { "net_listening",   0, 0, bi_net_listening    },
    { "net_resolve",     1, 1, bi_net_resolve      },
    { "net_dns",         1, 1, bi_net_dns          },
    { "net_socket_enum", 0, 0, bi_net_sockets      },
    { "net_arp",         0, 0, bi_net_arp          },
    { "net_connections", 0, 0, bi_net_stat         },
    { "net_flow",        0, 0, bi_net_flow         },
    { "net_proxy",       0, 0, bi_net_proxy        },
    { "net_scan",        2, 2, bi_net_scan         },
    { "net_whois",       1, 1, bi_net_whois        },
    { "net_http_inspect",2, 2, bi_net_http_inspect },
    { "net_firewall",    0, 0, bi_net_firewall     },
    { "net_ssl_keys",    0, 0, bi_net_ssl_keys     },
    { "net_capture",     0, 2, bi_net_capture      },
    { "netstat",         0, 0, bi_net_stat         },
    { NULL, 0, 0, NULL },
};
