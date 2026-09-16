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
    /* web UI aliases */
    { "sysinfo",       0, 0, bi_info     },
    { "time",          0, 0, bi_time     },
    { "sleep",         1, 1, bi_sleep    },
    { "env",           0, 1, bi_env      },
    { "exec",          1, 1, bi_shell    },
    { "uname",         0, 0, bi_kernel_ver },
    { "kernel_version",0, 0, bi_kernel_ver },
    { NULL, 0, 0, NULL },
};
