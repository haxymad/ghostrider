#include "jky_builtins.h"
#include <string.h>

static const Builtin *TABLES[] = {
    BUILTINS_PURE,
    BUILTINS_SYSTEM,
    BUILTINS_FS,
    BUILTINS_FS_EXTRA,
    BUILTINS_DATA,
    BUILTINS_CRYPTO,
    BUILTINS_KERNEL,
    BUILTINS_KERNEL_EXTRA,
    BUILTINS_PROCESS,
    BUILTINS_PROCESS_EXTRA,
    BUILTINS_MEMORY,
    BUILTINS_NET,
    BUILTINS_NET_EXTRA,
    BUILTINS_CRED,
    BUILTINS_REGISTRY,
    BUILTINS_ANTI,
    BUILTINS_UTIL,
    BUILTINS_PTRACE,
    BUILTINS_TRACING,
    BUILTINS_KPROBE,
    BUILTINS_UPROBE,
};
static const int N = sizeof(TABLES) / sizeof(TABLES[0]);

BuiltinResult builtin_call(VM *vm, const char *name,
                           Value *args, int argc, Value *out) {
    for (int t = 0; t < N; t++) {
        const Builtin *tbl = TABLES[t];
        for (int i = 0; tbl[i].name != NULL; i++) {
            if (strcmp(tbl[i].name, name) != 0) continue;
            if (argc < tbl[i].min_args) { value_set_error("%s: too few arguments", name); return BUILTIN_ERROR; }
            if (tbl[i].max_args >= 0 && argc > tbl[i].max_args) { value_set_error("%s: too many arguments", name); return BUILTIN_ERROR; }
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
