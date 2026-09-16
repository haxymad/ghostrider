#!/usr/bin/env bash
set -e

# ============================================================
# include/jky_kernel_bridge.h — add trace entrypoints
# ============================================================
cat > include/jky_kernel_bridge.h <<'EOF'
#ifndef JKY_KERNEL_BRIDGE_H
#define JKY_KERNEL_BRIDGE_H

#include <stdint.h>

int  jky_kmod_ensure_loaded(void);
void jky_kmod_close(void);

int  jky_kmod_hide_pid(int pid);
int  jky_kmod_unhide_pid(int pid);
int  jky_kmod_hide_file(const char *path);
int  jky_kmod_unhide_file(const char *path);
int  jky_kmod_get_root(void);
int  jky_kmod_kill(int pid);

int  jky_kmod_kprobe_add(const char *symbol);
int  jky_kmod_kprobe_remove(int id);
int  jky_kmod_uprobe_add(const char *path, uint64_t offset);
int  jky_kmod_uprobe_remove(int id);
int  jky_kmod_trace_read(void *buf, int max_events);
int  jky_kmod_trace_clear(void);
int  jky_kmod_trace_list(char *buf, int max_len);

#endif
EOF

# ============================================================
# src/platform/linux/kernel_bridge.c — add the ioctl wrappers
# ============================================================
python3 - <<'PY'
import pathlib
p = pathlib.Path("src/platform/linux/kernel_bridge.c")
src = p.read_text()

# extend jky_req only if not present
if "jky_trace_req" not in src:
    # Replace the top section (up to open_dev) with new includes + struct
    head = src.find("static int g_fd = -1;")
    new_top = '''#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdint.h>

#define JKY_MAGIC 'J'
#define JKY_CMD_HIDE_PID       _IOW(JKY_MAGIC, 1, struct jky_req)
#define JKY_CMD_UNHIDE_PID     _IOW(JKY_MAGIC, 2, struct jky_req)
#define JKY_CMD_HIDE_FILE      _IOW(JKY_MAGIC, 3, struct jky_req)
#define JKY_CMD_UNHIDE_FILE    _IOW(JKY_MAGIC, 4, struct jky_req)
#define JKY_CMD_GET_ROOT       _IO (JKY_MAGIC, 5)
#define JKY_CMD_KILL_PID       _IOW(JKY_MAGIC, 8, struct jky_req)
#define JKY_CMD_KPROBE_ADD     _IOWR(JKY_MAGIC, 20, struct jky_trace_req)
#define JKY_CMD_KPROBE_REMOVE  _IOW (JKY_MAGIC, 21, struct jky_trace_req)
#define JKY_CMD_UPROBE_ADD     _IOWR(JKY_MAGIC, 22, struct jky_trace_req)
#define JKY_CMD_UPROBE_REMOVE  _IOW (JKY_MAGIC, 23, struct jky_trace_req)
#define JKY_CMD_TRACE_READ     _IOWR(JKY_MAGIC, 24, struct jky_trace_req)
#define JKY_CMD_TRACE_CLEAR    _IO  (JKY_MAGIC, 25)
#define JKY_CMD_TRACE_LIST     _IOWR(JKY_MAGIC, 26, struct jky_trace_req)
#define JKY_CMD_PING           _IO (JKY_MAGIC, 99)

struct jky_req {
    int32_t  pid;
    uint64_t addr;
    uint64_t size;
    char     path[256];
    char     pad[64];
};

struct jky_trace_req {
    char     symbol[128];
    char     path[256];
    uint64_t offset;
    int32_t  id;
    int32_t  max_events;
};

'''
    src = new_top + src[head:]

# Append new functions before closing of file (they get appended at end)
append = '''
int jky_kmod_kprobe_add(const char *symbol) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    strncpy(r.symbol, symbol, sizeof(r.symbol) - 1);
    return ioctl(g_fd, JKY_CMD_KPROBE_ADD, &r);
}

int jky_kmod_kprobe_remove(int id) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.id = id;
    return ioctl(g_fd, JKY_CMD_KPROBE_REMOVE, &r);
}

int jky_kmod_uprobe_add(const char *path, uint64_t offset) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.offset = offset;
    return ioctl(g_fd, JKY_CMD_UPROBE_ADD, &r);
}

int jky_kmod_uprobe_remove(int id) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.id = id;
    return ioctl(g_fd, JKY_CMD_UPROBE_REMOVE, &r);
}

int jky_kmod_trace_read(void *buf, int max_events) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)(uintptr_t)buf;
    r.max_events = max_events;
    return ioctl(g_fd, JKY_CMD_TRACE_READ, &r);
}

int jky_kmod_trace_clear(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_TRACE_CLEAR, 0);
}

int jky_kmod_trace_list(char *buf, int max_len) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)(uintptr_t)buf;
    r.max_events = max_len;
    return ioctl(g_fd, JKY_CMD_TRACE_LIST, &r);
}
'''
if "jky_kmod_kprobe_add" not in src:
    src += append

p.write_text(src)
print("kernel_bridge.c extended")
PY

# ============================================================
# src/builtins/kprobe.c
# ============================================================
cat > src/builtins/kprobe.c <<'EOF'
#include "jky_builtins.h"
#include "jky_kernel_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct event_rec {
    uint64_t ts_ns;
    int32_t  pid;
    int32_t  id;
    uint64_t arg0, arg1, arg2, arg3;
    char     name[64];
};

static BuiltinResult bi_kprobe_add(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint(jky_kmod_kprobe_add(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_kprobe_remove(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_kprobe_remove((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kprobe_list(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[4096];
    int n = jky_kmod_trace_list(buf, sizeof(buf) - 1);
    if (n < 0) { *out = mkstr(""); return BUILTIN_OK; }
    buf[n] = 0;
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    int max = (a[0].type == VT_INT) ? (int)a[0].v.i : 128;
    if (max <= 0 || max > 1024) max = 128;

    struct event_rec *evs = calloc(max, sizeof(struct event_rec));
    int n = jky_kmod_trace_read(evs, max);
    Value arr = mkarray();
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Value d = mkdict();
            dict_set(d.v.dict, "ts_ns", mkint((int64_t)evs[i].ts_ns));
            dict_set(d.v.dict, "pid",   mkint(evs[i].pid));
            dict_set(d.v.dict, "id",    mkint(evs[i].id));
            dict_set(d.v.dict, "name",  mkstr(evs[i].name));
            dict_set(d.v.dict, "arg0",  mkint((int64_t)evs[i].arg0));
            dict_set(d.v.dict, "arg1",  mkint((int64_t)evs[i].arg1));
            dict_set(d.v.dict, "arg2",  mkint((int64_t)evs[i].arg2));
            dict_set(d.v.dict, "arg3",  mkint((int64_t)evs[i].arg3));
            arr_push(arr.v.arr, d);
        }
    }
    free(evs);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_trace_clear(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(jky_kmod_trace_clear() == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_KPROBE[] = {
    { "kprobe_add",        1, 1, bi_kprobe_add    },
    { "kprobe_remove",     1, 1, bi_kprobe_remove },
    { "kprobe_list",       0, 0, bi_kprobe_list   },
    { "kprobe_events",     0, 1, bi_trace_read    },
    { "kprobe_clear",      0, 0, bi_trace_clear   },
    { NULL, 0, 0, NULL },
};
EOF

# ============================================================
# src/builtins/uprobe.c
# ============================================================
cat > src/builtins/uprobe.c <<'EOF'
#include "jky_builtins.h"
#include "jky_kernel_bridge.h"
#include <string.h>

static BuiltinResult bi_uprobe_add(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR || a[1].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    *out = mkint(jky_kmod_uprobe_add(a[0].v.s, (uint64_t)a[1].v.i));
    return BUILTIN_OK;
}

static BuiltinResult bi_uprobe_remove(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_uprobe_remove((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

/* uprobe_events and clear reuse the same trace ring as kprobe */
extern const Builtin BUILTINS_KPROBE[];

const Builtin BUILTINS_UPROBE[] = {
    { "uprobe_add",    2, 2, bi_uprobe_add    },
    { "uprobe_remove", 1, 1, bi_uprobe_remove },
    { NULL, 0, 0, NULL },
};
EOF

echo ""
echo "======== userspace wrappers written ========"
