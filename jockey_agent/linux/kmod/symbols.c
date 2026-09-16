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
