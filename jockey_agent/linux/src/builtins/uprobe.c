#include "jky_builtins.h"
#include "jky_kernel_bridge.h"
#include <string.h>

static BuiltinResult bi_uprobe_add(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR || a[1].type != VT_INT) {
        *out = mkint(-1); return BUILTIN_OK;
    }
    *out = mkint(jky_kmod_uprobe_add(a[0].v.s, (uint64_t)a[1].v.i));
    return BUILTIN_OK;
}

static BuiltinResult bi_uprobe_remove(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_INT) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_kmod_uprobe_remove((int)a[0].v.i) == 0);
    return BUILTIN_OK;
}

/* uprobe_events and clear reuse the same trace ring as kprobe */
extern const Builtin BUILTINS_KPROBE[];

const Builtin BUILTINS_UPROBE[] = {
    { "uprobe_add",    2, 2, bi_uprobe_add    },
    { "uprobe_remove", 1, 1, bi_uprobe_remove },
    { NULL, 0, 0, NULL },
};
