/*
 * system.c — System information builtins for Windows.
 */

#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static BuiltinResult bi_platform(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkstr(jky_platform_name());
    return BUILTIN_OK;
}

static BuiltinResult bi_hostname(VM *vm, Value *a, int c, Value *out)
{
    char buf[256];
    (void)vm; (void)a; (void)c;
    jky_hostname(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_username(VM *vm, Value *a, int c, Value *out)
{
    char buf[256];
    (void)vm; (void)a; (void)c;
    jky_username(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_version(VM *vm, Value *a, int c, Value *out)
{
    char buf[256];
    (void)vm; (void)a; (void)c;
    jky_kernel_version(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_arch(VM *vm, Value *a, int c, Value *out)
{
    char buf[64];
    (void)vm; (void)a; (void)c;
    jky_arch(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_cpus(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_cpu_count());
    return BUILTIN_OK;
}

static BuiltinResult bi_uptime(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_uptime_sec());
    return BUILTIN_OK;
}

static BuiltinResult bi_memory(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    Value d = mkdict();
    dict_set(d.v.dict, "total", mkint(jky_total_mem()));
    dict_set(d.v.dict, "free", mkint(jky_free_mem()));
    dict_set(d.v.dict, "used", mkint(jky_total_mem() - jky_free_mem()));

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        dict_set(d.v.dict, "load", mkint((int)(ms.dwMemoryLoad)));
    }

    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_time(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;
    *out = mkint((int64_t)time(NULL));
    return BUILTIN_OK;
}

static BuiltinResult bi_sleep(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_INT) { *out = mknone(); return BUILTIN_OK; }
    jky_sleep_ms((int)a[0].v.i);
    *out = mknone();
    return BUILTIN_OK;
}

static BuiltinResult bi_env(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;

    if (c == 0) {
        Value d = mkdict();
        char *env = GetEnvironmentStringsA();
        char *p = env;
        while (*p) {
            char *eq = strchr(p, '=');
            if (eq && eq > p) {
                int klen = (int)(eq - p);
                char key[256];
                strncpy(key, p, klen);
                key[klen] = 0;
                dict_set(d.v.dict, key, mkstr(eq + 1));
            }
            p += strlen(p) + 1;
        }
        FreeEnvironmentStringsA(env);
        *out = d;
    } else {
        if (a[0].type != JKY_STR) { *out = mknone(); return BUILTIN_OK; }
        char *val = getenv(a[0].v.s);
        *out = val ? mkstr(val) : mknone();
    }
    return BUILTIN_OK;
}

static BuiltinResult bi_exec(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkstr(""); return BUILTIN_OK; }

    FILE *pipe = _popen(a[0].v.s, "r");
    if (!pipe) { *out = mkstr(""); return BUILTIN_OK; }

    char buf[4096];
    size_t total = 0;
    size_t cap = 4096;
    char *result = malloc(cap);

    while (fgets(buf, sizeof(buf), pipe)) {
        size_t blen = strlen(buf);
        if (total + blen >= cap) {
            cap *= 2;
            result = realloc(result, cap);
        }
        memcpy(result + total, buf, blen);
        total += blen;
    }

    result[total] = 0;
    _pclose(pipe);

    *out = mkstr(result);
    free(result);
    return BUILTIN_OK;
}

static BuiltinResult bi_sysinfo(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)a; (void)c;

    char host[256], user[256], kernel[256], arch[64];
    Value d = mkdict();

    jky_hostname(host, sizeof(host));
    jky_username(user, sizeof(user));
    jky_kernel_version(kernel, sizeof(kernel));
    jky_arch(arch, sizeof(arch));

    dict_set(d.v.dict, "sysname", mkstr("Windows"));
    dict_set(d.v.dict, "nodename", mkstr(host));
    dict_set(d.v.dict, "release", mkstr(kernel));
    dict_set(d.v.dict, "version", mkstr(kernel));
    dict_set(d.v.dict, "machine", mkstr(arch));
    dict_set(d.v.dict, "user", mkstr(user));
    dict_set(d.v.dict, "cpus", mkint(jky_cpu_count()));
    dict_set(d.v.dict, "uptime", mkint(jky_uptime_sec()));

    *out = d;
    return BUILTIN_OK;
}

const Builtin BUILTINS_SYSTEM[] = {
    { "sysinfo",             0, 0, bi_sysinfo            },
    { "system_platform",     0, 0, bi_platform           },
    { "system_hostname",     0, 0, bi_hostname           },
    { "system_username",     0, 0, bi_username           },
    { "system_kernel_version", 0, 0, bi_kernel_version   },
    { "system_arch",         0, 0, bi_arch               },
    { "system_cpus",         0, 0, bi_cpus               },
    { "system_uptime",       0, 0, bi_uptime             },
    { "system_memory",       0, 0, bi_memory             },
    { "system_info",         0, 0, bi_sysinfo            },
    { "time",                0, 0, bi_time               },
    { "sleep",               1, 1, bi_sleep              },
    { "env",                 0, 1, bi_env                },
    { "exec",                1, 1, bi_exec               },
    /* aliases */
    { "system_time",         0, 0, bi_time               },
    { "system_shell",        1, 1, bi_exec               },
    { "system_env",          0, 1, bi_env                },
    { NULL, 0, 0, NULL },
};
