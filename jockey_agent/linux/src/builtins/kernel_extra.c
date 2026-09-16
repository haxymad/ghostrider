#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static BuiltinResult bi_kernel_rootkit_check(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    /* Compare /proc/modules with /sys/module */
    Value d = mkdict();
    Value hidden = mkarray();

    Value seen = mkdict();
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char name[128] = {0};
            sscanf(line, "%127s", name);
            dict_set(seen.v.dict, name, mkint(1));
        }
        fclose(f);
    }
    DIR *sd = opendir("/sys/module");
    if (sd) {
        struct dirent *e;
        while ((e = readdir(sd))) {
            if (e->d_name[0] == '.') continue;
            Value v = dict_get(seen.v.dict, e->d_name);
            if (!v.v.i) arr_push(hidden.v.arr, mkstr(e->d_name));
        }
        closedir(sd);
    }
    vfree(&seen);
    dict_set(d.v.dict, "hidden_modules", hidden);
    dict_set(d.v.dict, "suspicious", mkint(hidden.v.arr->len > 0 ? 1 : 0));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_netfilter(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value d = mkdict();
    Value hooks = mkarray();
    FILE *f = fopen("/proc/net/netfilter/nf_log", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line); if (n && line[n-1]=='\n') line[n-1]=0;
            arr_push(hooks.v.arr, mkstr(line));
        }
        fclose(f);
    }
    dict_set(d.v.dict, "hooks", hooks);
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_kernel_drivers(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    DIR *d = opendir("/sys/bus");
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

static BuiltinResult bi_kernel_timers(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/proc/timer_list", "r");
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n < 200) {
        if (strstr(line, "#")) continue;
        size_t l = strlen(line); if (l && line[l-1]=='\n') line[l-1]=0;
        arr_push(arr.v.arr, mkstr(line));
        n++;
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

STUB_DICT(bi_kernel_syscall_check)
STUB_ARRAY(bi_kernel_hidden_procs)
STUB_ARRAY(bi_kernel_hidden_modules)
STUB_ARRAY(bi_kernel_callbacks)
STUB_DICT(bi_kernel_memory_scan)
STUB_DICT(bi_kernel_read)

const Builtin BUILTINS_KERNEL_EXTRA[] = {
    { "kernel_rootkit_check",  0, 0, bi_kernel_rootkit_check },
    { "kernel_syscall_check",  0, 0, bi_kernel_syscall_check },
    { "kernel_hidden_procs",   0, 0, bi_kernel_hidden_procs  },
    { "kernel_hidden_modules", 0, 0, bi_kernel_hidden_modules},
    { "kernel_netfilter_check",0, 0, bi_kernel_netfilter     },
    { "kernel_driver_list",    0, 0, bi_kernel_drivers       },
    { "kernel_timer_list",     0, 0, bi_kernel_timers        },
    { "kernel_callback_list",  0, 0, bi_kernel_callbacks     },
    { "kernel_memory_scan",    0, 0, bi_kernel_memory_scan   },
    { "kernel_read",           2, 2, bi_kernel_read          },
    { NULL, 0, 0, NULL },
};
