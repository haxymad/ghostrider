#include "jky_builtins.h"
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static BuiltinResult bi_self_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_getpid());
    return BUILTIN_OK;
}

static BuiltinResult bi_parent_pid(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkint(jky_getppid());
    return BUILTIN_OK;
}

static BuiltinResult bi_list(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();

    DIR *d = opendir("/proc");
    if (!d) { *out = arr; return BUILTIN_OK; }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (*end != 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        FILE *f = fopen(path, "r");
        char name[256] = {0};
        if (f) {
            if (fgets(name, sizeof(name), f)) {
                size_t n = strlen(name);
                if (n && name[n-1] == '\n') name[n-1] = 0;
            }
            fclose(f);
        }

        Value item = mkdict();
        dict_set(item.v.dict, "pid",  mkint(pid));
        dict_set(item.v.dict, "name", mkstr(name));
        arr_push(arr.v.arr, item);
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_kill(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kill((int)a[0].v.i, 15) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_fork(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    pid_t p = fork();
    *out = mkint((int64_t)p);
    return BUILTIN_OK;
}

const Builtin BUILTINS_PROCESS[] = {
    { "process_self_pid",   0, 0, bi_self_pid   },
    { "process_parent_pid", 0, 0, bi_parent_pid },
    { "process_list",       0, 0, bi_list       },
    { "process_kill",       1, 1, bi_kill       },
    { "process_fork",       0, 0, bi_fork       },
    /* web UI aliases */
    { "getpid",  0, 0, bi_self_pid   },
    { "getppid", 0, 0, bi_parent_pid },
    { "procs",   0, 0, bi_list       },
    { "kill",    1, 1, bi_kill       },
    { "fork",    0, 0, bi_fork       },
    { NULL, 0, 0, NULL },
};
