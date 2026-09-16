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
extern const Builtin BUILTINS_PROCESS[];
extern const Builtin BUILTINS_NET[];


extern const Builtin BUILTINS_PROCESS_EXTRA[];
extern const Builtin BUILTINS_MEMORY[];
extern const Builtin BUILTINS_FS_EXTRA[];
extern const Builtin BUILTINS_CRED[];
extern const Builtin BUILTINS_KERNEL_EXTRA[];
extern const Builtin BUILTINS_NET_EXTRA[];
extern const Builtin BUILTINS_REGISTRY[];
extern const Builtin BUILTINS_ANTI[];
extern const Builtin BUILTINS_UTIL[];

extern const Builtin BUILTINS_PTRACE[];
extern const Builtin BUILTINS_TRACING[];
extern const Builtin BUILTINS_KPROBE[];
extern const Builtin BUILTINS_UPROBE[];

#endif
