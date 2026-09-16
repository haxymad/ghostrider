#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static BuiltinResult bi_display_clear(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    fputs("\x1b[2J\x1b[H", stdout); fflush(stdout);
    *out = mknone(); return BUILTIN_OK;
}

static BuiltinResult bi_display_text(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    for (int i = 0; i < c; i++) {
        char *s = value_to_str(a[i]);
        fputs(s, stdout);
        free(s);
    }
    fflush(stdout);
    *out = mknone(); return BUILTIN_OK;
}

static BuiltinResult bi_play_beep(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    fputc('\a', stdout); fflush(stdout);
    *out = mknone(); return BUILTIN_OK;
}

static BuiltinResult bi_input(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    if (c >= 1 && a[0].type == VT_STR) { fputs(a[0].v.s, stdout); fflush(stdout); }
    char buf[1024];
    if (!fgets(buf, sizeof(buf), stdin)) { *out = mkstr(""); return BUILTIN_OK; }
    size_t n = strlen(buf); if (n && buf[n-1]=='\n') buf[n-1]=0;
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    for (int i = 0; i < c; i++) {
        char *s = value_to_str(a[i]);
        fputs(s, stderr); fputc(' ', stderr);
        free(s);
    }
    fputc('\n', stderr); fflush(stderr);
    *out = mknone(); return BUILTIN_OK;
}

static BuiltinResult bi_timestamp(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint((int64_t)time(NULL)); return BUILTIN_OK;
}

static BuiltinResult bi_checksum_file(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkstr(""); return BUILTIN_OK; }
    FILE *f = fopen(a[0].v.s, "rb");
    if (!f) { *out = mkstr(""); return BUILTIN_OK; }
    /* simple sum for brevity — full crypto is in crypto.c */
    unsigned long long h = 1469598103934665603ULL;
    unsigned char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= 1099511628211ULL; }
    }
    fclose(f);
    char outbuf[32]; snprintf(outbuf, sizeof(outbuf), "%016llx", h);
    *out = mkstr(outbuf);
    return BUILTIN_OK;
}

STUB_DICT(bi_json_query)
STUB_ARRAY(bi_csv_parse)
STUB_DICT(bi_compress)

const Builtin BUILTINS_UTIL[] = {
    { "display_clear", 0, 0, bi_display_clear },
    { "display_text",  1, -1, bi_display_text  },
    { "play_beep",     0, 0, bi_play_beep     },
    { "input",         0, 1, bi_input         },
    { "trace",         0, -1, bi_trace        },
    { "timestamp",     0, 0, bi_timestamp     },
    { "checksum_file", 1, 1, bi_checksum_file },
    { "json_query",    2, 2, bi_json_query    },
    { "csv_parse",     1, 1, bi_csv_parse     },
    { "compress",      1, 1, bi_compress      },
    { NULL, 0, 0, NULL },
};
