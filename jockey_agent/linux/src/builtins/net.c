#include "jky_builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static Value mkbytes_empty(void) {
    Value v;
    v.type = VT_BYTES;
    v.rc   = 0;
    v.v.bytes.data = malloc(1);
    v.v.bytes.len  = 0;
    return v;
}


static BuiltinResult bi_net_connect(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR || a[1].type != VT_INT) {
        *out = mkint(-1);
        return BUILTIN_OK;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { *out = mkint(-1); return BUILTIN_OK; }

    struct hostent *he = gethostbyname(a[0].v.s);
    if (!he) { close(fd); *out = mkint(-1); return BUILTIN_OK; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)a[1].v.i);
    memcpy(&sa.sin_addr, he->h_addr, he->h_length);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        *out = mkint(-1);
        return BUILTIN_OK;
    }
    *out = mkint(fd);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_send(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkint(-1); return BUILTIN_OK; }

    const uint8_t *data = NULL;
    size_t len = 0;
    if (a[1].type == VT_STR) {
        data = (const uint8_t *)a[1].v.s;
        len  = strlen(a[1].v.s);
    } else if (a[1].type == VT_BYTES) {
        data = a[1].v.bytes.data;
        len  = a[1].v.bytes.len;
    } else {
        *out = mkint(-1);
        return BUILTIN_OK;
    }

    ssize_t sent = send((int)a[0].v.i, data, len, 0);
    *out = mkint((int64_t)sent);
    return BUILTIN_OK;
}

static BuiltinResult bi_net_recv(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT || a[1].type != VT_INT) {
        *out = mkbytes_empty();
        return BUILTIN_OK;
    }

    int64_t want = a[1].v.i;
    if (want <= 0) want = 4096;
    if (want > 64 * 1024 * 1024) want = 64 * 1024 * 1024;

    uint8_t *buf = malloc((size_t)want);
    if (!buf) { *out = mkbytes_empty(); return BUILTIN_OK; }

    ssize_t got = recv((int)a[0].v.i, buf, (size_t)want, 0);
    if (got < 0) got = 0;

    Value v;
    v.type = VT_BYTES;
    v.rc   = 0;
    v.v.bytes.data = buf;
    v.v.bytes.len  = (size_t)got;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_net_close(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(close((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_NET[] = {
    { "net_connect", 2, 2, bi_net_connect },
    { "net_send",    2, 2, bi_net_send    },
    { "net_recv",    2, 2, bi_net_recv    },
    { "net_close",   1, 1, bi_net_close   },
    { NULL, 0, 0, NULL },
};
