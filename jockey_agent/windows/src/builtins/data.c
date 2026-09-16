#include "jky_builtins.h"
#include "jky_platform.h"

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "bcrypt.lib")

#if defined(_MSC_VER)
#define strdup _strdup
#endif

static BuiltinResult bi_hex_dump(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[0].type == JKY_BYTES) {
        data = a[0].v.bytes.data;
        len  = a[0].v.bytes.len;
    } else if (a[0].type == JKY_STR) {
        data = (const uint8_t *)a[0].v.s;
        len  = strlen(a[0].v.s);
    } else {
        *out = mkstr("");
        return BUILTIN_OK;
    }

    size_t cap = len * 3 + 1;
    char *buf = malloc(cap);
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (i) buf[n++] = ' ';
        n += snprintf(buf + n, cap - n, "%02x", data[i]);
    }
    buf[n] = 0;
    *out = mkstr(buf);
    free(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_timestamp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_time_sec());
    return BUILTIN_OK;
}

static BuiltinResult bi_uuid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;

    uint8_t b[16];
    if (BCryptGenRandom(NULL, b, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        *out = mkstr("00000000-0000-4000-8000-000000000000");
        return BUILTIN_OK;
    }

    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;

    char buf[40];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_random_bytes(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) {
        value_set_error("data_random_bytes: int required");
        return BUILTIN_ERROR;
    }
    int64_t n = a[0].v.i;
    if (n < 0) n = 0;

    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    if (n > 0) {
        if (BCryptGenRandom(NULL, buf, (ULONG)n,
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            memset(buf, 0, (size_t)n);
    }
    *out = bytes_value(buf, (size_t)n);
    return BUILTIN_OK;
}

static int is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

static BuiltinResult bi_url_encode(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkstr(""); return BUILTIN_OK; }
    const unsigned char *s = (const unsigned char *)a[0].v.s;
    size_t n = strlen(a[0].v.s);
    size_t cap = n * 3 + 1;
    char *buf = malloc(cap);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (is_unreserved(s[i])) buf[k++] = (char)s[i];
        else k += snprintf(buf + k, cap - k, "%%%02X", s[i]);
    }
    buf[k] = 0;
    *out = mkstr(buf);
    free(buf);
    return BUILTIN_OK;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static BuiltinResult bi_url_decode(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkstr(""); return BUILTIN_OK; }
    const char *s = a[0].v.s;
    size_t n = strlen(s);
    char *buf = malloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int h = hexval(s[i + 1]);
            int l = hexval(s[i + 2]);
            if (h >= 0 && l >= 0) {
                buf[k++] = (char)((h << 4) | l);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') buf[k++] = ' ';
        else buf[k++] = s[i];
    }
    buf[k] = 0;
    *out = mkstr(buf);
    free(buf);
    return BUILTIN_OK;
}

const Builtin BUILTINS_DATA[] = {
    { "data_hex_dump",     1, 1, bi_hex_dump     },
    { "data_timestamp",    0, 0, bi_timestamp    },
    { "data_uuid",         0, 0, bi_uuid         },
    { "data_random_bytes", 1, 1, bi_random_bytes },
    { "data_url_encode",   1, 1, bi_url_encode   },
    { "data_url_decode",   1, 1, bi_url_decode   },
    { NULL, 0, 0, NULL },
};