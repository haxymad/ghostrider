#include "jky_builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define TRACE_BASE "/sys/kernel/debug/tracing"

static int read_text(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return -1; }
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
    fclose(f);
    return (int)n;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    size_t n = strlen(text);
    size_t w = fwrite(text, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

static BuiltinResult bi_trace_available(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[4096];
    if (read_text(TRACE_BASE "/available_tracers", buf, sizeof(buf)) < 0) {
        *out = mkarray(); return BUILTIN_OK;
    }
    Value arr = mkarray();
    char *tok = strtok(buf, " \n");
    while (tok) { arr_push(arr.v.arr, mkstr(tok)); tok = strtok(NULL, " \n"); }
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_current(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    read_text(TRACE_BASE "/current_tracer", buf, sizeof(buf));
    size_t n = strlen(buf); if (n && buf[n-1]=='\n') buf[n-1]=0;
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_set(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(write_text(TRACE_BASE "/current_tracer", a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_clear(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(write_text(TRACE_BASE "/trace", "") == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    FILE *f = fopen(TRACE_BASE "/trace", "r");
    if (!f) { *out = mkstr(""); return BUILTIN_OK; }
    size_t cap = 256 * 1024, n = 0;
    char *buf = malloc(cap);
    size_t got;
    while ((got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    buf[n] = 0;
    fclose(f);
    *out = mkstr(buf);
    free(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_filter(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(write_text(TRACE_BASE "/set_ftrace_filter", a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_events(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    DIR *d = opendir(TRACE_BASE "/events");
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        arr_push(arr.v.arr, mkstr(e->d_name));
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_event_enable(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    /* a[0] = "category/event" */
    char path[512];
    snprintf(path, sizeof(path), TRACE_BASE "/events/%s/enable", a[0].v.s);
    int on = (c >= 2 && a[1].type == VT_INT) ? (int)a[1].v.i : 1;
    *out = mkbool(write_text(path, on ? "1" : "0") == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_enabled(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    DIR *d = opendir(TRACE_BASE "/events");
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *cat;
    while ((cat = readdir(d))) {
        if (cat->d_name[0] == '.') continue;
        char sub[512];
        snprintf(sub, sizeof(sub), TRACE_BASE "/events/%s", cat->d_name);
        DIR *sd = opendir(sub);
        if (!sd) continue;
        struct dirent *ev;
        while ((ev = readdir(sd))) {
            if (ev->d_name[0] == '.') continue;
            char en[600];
            snprintf(en, sizeof(en), "%s/%s/enable", sub, ev->d_name);
            char buf[8] = {0};
            if (read_text(en, buf, sizeof(buf)) > 0 && buf[0] == '1') {
                char name[256];
                snprintf(name, sizeof(name), "%s/%s", cat->d_name, ev->d_name);
                arr_push(arr.v.arr, mkstr(name));
            }
        }
        closedir(sd);
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_enable_all(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(write_text(TRACE_BASE "/events/enable", "1") == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_disable_all(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(write_text(TRACE_BASE "/events/enable", "0") == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_TRACING[] = {
    { "trace_available",      0, 0, bi_trace_available    },
    { "trace_current",        0, 0, bi_trace_current      },
    { "trace_set",            1, 1, bi_trace_set          },
    { "trace_clear",          0, 0, bi_trace_clear        },
    { "trace_read",           0, 0, bi_trace_read         },
    { "trace_filter",         1, 1, bi_trace_filter       },
    { "trace_events",         0, 0, bi_trace_events       },
    { "trace_event_enable",   1, 2, bi_trace_event_enable },
    { "trace_enabled",        0, 0, bi_trace_enabled      },
    { "trace_enable_all",     0, 0, bi_trace_enable_all   },
    { "trace_disable_all",    0, 0, bi_trace_disable_all  },
    { NULL, 0, 0, NULL },
};
