#ifndef JKY_STUBS_H
#define JKY_STUBS_H
#include "jky_builtins.h"

#define STUB_ARRAY(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mkarray(); return BUILTIN_OK; }
#define STUB_DICT(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mkdict(); return BUILTIN_OK; }
#define STUB_NONE(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mknone(); return BUILTIN_OK; }
#define STUB_FALSE(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mkbool(0); return BUILTIN_OK; }
#define STUB_INT0(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mkint(0); return BUILTIN_OK; }
#define STUB_STR0(name) \
    static BuiltinResult name(VM *vm, Value *a, int c, Value *out) { \
        (void)vm;(void)a;(void)c; *out = mkstr(""); return BUILTIN_OK; }
#endif
