#include "jky_builtins.h"
#include "jky_platform.h"
#include "_stubs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static int read_file_str(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return -1; }
    size_t got = fread(buf, 1, cap - 1, f);
    buf[got] = 0;
    fclose(f);
    return (int)got;
}

/* proc_enum — list of pids */
static BuiltinResult bi_proc_enum(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    DIR *d = opendir("/proc");
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *e;
    while ((e = readdir(d))) {
        char *end; long pid = strtol(e->d_name, &end, 10);
        if (*end != 0) continue;
        arr_push(arr.v.arr, mkint(pid));
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_info — dict of process fields */
static BuiltinResult bi_proc_info(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mknone(); return BUILTIN_OK; }
    int pid = (int)a[0].v.i;
    char p[128], buf[4096];

    snprintf(p, sizeof(p), "/proc/%d/status", pid);
    if (read_file_str(p, buf, sizeof(buf)) < 0) { *out = mknone(); return BUILTIN_OK; }

    Value d = mkdict();
    char *line = strtok(buf, "\n");
    while (line) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = 0;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            dict_set(d.v.dict, line, mkstr(val));
        }
        line = strtok(NULL, "\n");
    }
    *out = d;
    return BUILTIN_OK;
}

/* proc_cmdline — argv as array */
static BuiltinResult bi_proc_cmdline(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128], buf[4096];
    snprintf(p, sizeof(p), "/proc/%d/cmdline", (int)a[0].v.i);
    int n = read_file_str(p, buf, sizeof(buf));
    Value arr = mkarray();
    if (n <= 0) { *out = arr; return BUILTIN_OK; }
    int i = 0;
    while (i < n) {
        int start = i;
        while (i < n && buf[i] != 0) i++;
        arr_push(arr.v.arr, mkstr(buf + start));
        i++;
    }
    *out = arr;
    return BUILTIN_OK;
}

/* proc_env — environ as dict */
static BuiltinResult bi_proc_env(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkdict(); return BUILTIN_OK; }
    char p[128], buf[8192];
    snprintf(p, sizeof(p), "/proc/%d/environ", (int)a[0].v.i);
    int n = read_file_str(p, buf, sizeof(buf));
    Value d = mkdict();
    if (n <= 0) { *out = d; return BUILTIN_OK; }
    int i = 0;
    while (i < n) {
        int start = i;
        while (i < n && buf[i] != 0) i++;
        char *eq = memchr(buf + start, '=', i - start);
        if (eq) {
            size_t klen = (size_t)(eq - (buf + start));
            char key[256]; if (klen >= sizeof(key)) klen = sizeof(key)-1;
            memcpy(key, buf + start, klen); key[klen] = 0;
            dict_set(d.v.dict, key, mkstr(eq + 1));
        }
        i++;
    }
    *out = d;
    return BUILTIN_OK;
}

/* proc_regions — /proc/pid/maps */
static BuiltinResult bi_proc_regions(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/maps", (int)a[0].v.i);
    FILE *f = fopen(p, "r");
    Value arr = mkarray();
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        unsigned long start, end;
        char perms[8] = {0}, path[256] = {0};
        sscanf(line, "%lx-%lx %7s %*s %*s %*s %255s", &start, &end, perms, path);
        Value d = mkdict();
        dict_set(d.v.dict, "start", mkint((int64_t)start));
        dict_set(d.v.dict, "end",   mkint((int64_t)end));
        dict_set(d.v.dict, "perms", mkstr(perms));
        dict_set(d.v.dict, "path",  mkstr(path));
        arr_push(arr.v.arr, d);
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_threads — /proc/pid/task */
static BuiltinResult bi_proc_threads(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/task", (int)a[0].v.i);
    Value arr = mkarray();
    DIR *d = opendir(p);
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *e;
    while ((e = readdir(d))) {
        char *end; long tid = strtol(e->d_name, &end, 10);
        if (*end != 0) continue;
        arr_push(arr.v.arr, mkint(tid));
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_modules — shared objects */
static BuiltinResult bi_proc_modules(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/maps", (int)a[0].v.i);
    FILE *f = fopen(p, "r");
    Value arr = mkarray();
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    Value seen = mkdict();
    while (fgets(line, sizeof(line), f)) {
        char *sl = strrchr(line, '/');
        if (!sl) continue;
        char path[256]; strncpy(path, sl + 1, sizeof(path) - 1); path[sizeof(path)-1]=0;
        char *nl = strchr(path, '\n'); if (nl) *nl = 0;
        if (!dict_get(seen.v.dict, path).v.i) {
            dict_set(seen.v.dict, path, mkint(1));
            arr_push(arr.v.arr, mkstr(path));
        }
    }
    fclose(f);
    vfree(&seen);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_handles — /proc/pid/fd */
static BuiltinResult bi_proc_handles(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/fd", (int)a[0].v.i);
    Value arr = mkarray();
    DIR *d = opendir(p);
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[512], target[512] = {0};
        snprintf(path, sizeof(path), "%s/%s", p, e->d_name);
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n > 0) target[n] = 0;
        Value item = mkdict();
        dict_set(item.v.dict, "fd",   mkstr(e->d_name));
        dict_set(item.v.dict, "path", mkstr(target));
        arr_push(arr.v.arr, item);
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_connections — tcp connections for pid */
static BuiltinResult bi_proc_connections(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkarray(); return BUILTIN_OK; }
    char p[128];
    snprintf(p, sizeof(p), "/proc/%d/net/tcp", (int)a[0].v.i);
    FILE *f = fopen(p, "r");
    Value arr = mkarray();
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    fgets(line, sizeof(line), f);
    while (fgets(line, sizeof(line), f)) {
        unsigned local, remote; int st;
        sscanf(line, " %*d: %8x:%*x %8x:%*x %x", &local, &remote, &st);
        Value d = mkdict();
        dict_set(d.v.dict, "local",  mkint(local));
        dict_set(d.v.dict, "remote", mkint(remote));
        dict_set(d.v.dict, "state",  mkint(st));
        arr_push(arr.v.arr, d);
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

/* proc_token_info — uid/gid */
static BuiltinResult bi_proc_token(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkdict(); return BUILTIN_OK; }
    char p[128];
    struct stat st;
    snprintf(p, sizeof(p), "/proc/%d", (int)a[0].v.i);
    if (stat(p, &st) != 0) { *out = mkdict(); return BUILTIN_OK; }
    Value d = mkdict();
    dict_set(d.v.dict, "uid", mkint((int64_t)st.st_uid));
    dict_set(d.v.dict, "gid", mkint((int64_t)st.st_gid));
    *out = d;
    return BUILTIN_OK;
}

/* proc_suspend / resume — SIGSTOP / SIGCONT */
static BuiltinResult bi_proc_suspend(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(kill((int)a[0].v.i, SIGSTOP) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_proc_resume(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(kill((int)a[0].v.i, SIGCONT) == 0);
    return BUILTIN_OK;
}

/* stubs */
STUB_NONE(bi_proc_dump)
STUB_ARRAY(bi_proc_scan_heap)
STUB_ARRAY(bi_proc_find_pattern)
STUB_FALSE(bi_proc_inject)
STUB_FALSE(bi_proc_hollow)
STUB_FALSE(bi_proc_apc_inject)
STUB_FALSE(bi_proc_get_root)
STUB_FALSE(bi_proc_impersonate)
STUB_FALSE(bi_proc_privilege)

const Builtin BUILTINS_PROCESS_EXTRA[] = {
    { "proc_enum",        0, 0, bi_proc_enum        },
    { "proc_info",        1, 1, bi_proc_info        },
    { "proc_dump",        2, 2, bi_proc_dump        },
    { "proc_regions",     1, 1, bi_proc_regions     },
    { "proc_threads",     1, 1, bi_proc_threads     },
    { "proc_modules",     1, 1, bi_proc_modules     },
    { "proc_env",         1, 1, bi_proc_env         },
    { "proc_cmdline",     1, 1, bi_proc_cmdline     },
    { "proc_handles",     1, 1, bi_proc_handles     },
    { "proc_connections", 1, 1, bi_proc_connections },
    { "proc_scan_heap",   1, 1, bi_proc_scan_heap   },
    { "proc_find_pattern",2, 2, bi_proc_find_pattern},
    { "proc_token_info",  1, 1, bi_proc_token       },
    { "proc_suspend",     1, 1, bi_proc_suspend     },
    { "proc_resume",      1, 1, bi_proc_resume      },
    { "proc_inject",      2, 2, bi_proc_inject      },
    { "proc_hollow",      2, 2, bi_proc_hollow      },
    { "proc_apc_inject",  2, 2, bi_proc_apc_inject  },
    { "proc_get_root",    1, 1, bi_proc_get_root    },
    { "proc_impersonate", 1, 1, bi_proc_impersonate },
    { "proc_privilege",   2, 2, bi_proc_privilege   },
    { NULL, 0, 0, NULL },
};
