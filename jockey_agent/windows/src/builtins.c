/* builtins.c — dispatch only; arrays live in their own translation units */
#include "jky_builtins.h"
#include <string.h>

static const Builtin *TABLES[] = {
    BUILTINS_PURE,
    BUILTINS_SYSTEM,
    BUILTINS_FS,
    BUILTINS_DATA,
    BUILTINS_CRYPTO,
    BUILTINS_KERNEL,
    BUILTINS_PROCESS,
    BUILTINS_MEMORY,
    BUILTINS_NET,
    BUILTINS_CRED,
    BUILTINS_ANTI,
    BUILTINS_UTIL,
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
