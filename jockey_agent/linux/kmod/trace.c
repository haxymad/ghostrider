#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/uprobes.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include <linux/kfifo.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "trace.h"

#define MAX_PROBES       64
#define MAX_EVENTS       1024

struct probe_entry {
    struct list_head list;
    int              id;
    int              kind;         /* 0 = kprobe, 1 = uprobe */
    char             name[128];
    struct kprobe    kp;
    struct uprobe    up;
};

static LIST_HEAD(probes);
static DEFINE_MUTEX(probes_lock);
static int next_id = 1;

static DEFINE_KFIFO(events, struct jky_trace_event, MAX_EVENTS);

static void emit_event(const char *name, int id, struct pt_regs *regs) {
    struct jky_trace_event e;
    e.ts_ns = ktime_get_ns();
    e.pid   = current->pid;
    e.id    = id;
#if defined(__x86_64__)
    e.arg0 = regs->di;
    e.arg1 = regs->si;
    e.arg2 = regs->dx;
    e.arg3 = regs->cx;
#else
    e.arg0 = e.arg1 = e.arg2 = e.arg3 = 0;
#endif
    strncpy(e.name, name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    kfifo_in(&events, &e, 1);
}

static int kprobe_handler(struct kprobe *p, struct pt_regs *regs) {
    struct probe_entry *entry = container_of(p, struct probe_entry, kp);
    emit_event(entry->name, entry->id, regs);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
static int uprobe_handler(struct uprobe *u, struct pt_regs *regs) {
    struct probe_entry *entry = container_of(u, struct probe_entry, up);
    emit_event(entry->name, entry->id, regs);
    return 0;
}
#else
static int uprobe_handler(struct uprobe *u, struct pt_regs *regs) {
    struct probe_entry *entry = container_of(u, struct probe_entry, up);
    emit_event(entry->name, entry->id, regs);
    return 0;
}
#endif

static struct probe_entry *find_probe(int id) {
    struct probe_entry *e;
    list_for_each_entry(e, &probes, list)
        if (e->id == id) return e;
    return NULL;
}

int jky_kprobe_add(const char *symbol) {
    struct probe_entry *e;
    int ret;

    mutex_lock(&probes_lock);
    if (next_id > 100000) { mutex_unlock(&probes_lock); return -EMFILE; }

    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&probes_lock); return -ENOMEM; }

    e->id = next_id++;
    e->kind = 0;
    strncpy(e->name, symbol, sizeof(e->name) - 1);

    e->kp.symbol_name = e->name;
    e->kp.pre_handler = kprobe_handler;

    ret = register_kprobe(&e->kp);
    if (ret < 0) {
        kfree(e);
        mutex_unlock(&probes_lock);
        return ret;
    }

    list_add(&e->list, &probes);
    pr_info("jky: kprobe #%d on %s at %px\n", e->id, e->name, e->kp.addr);
    mutex_unlock(&probes_lock);
    return e->id;
}

int jky_kprobe_remove(int id) {
    struct probe_entry *e;
    mutex_lock(&probes_lock);
    e = find_probe(id);
    if (!e) { mutex_unlock(&probes_lock); return -ENOENT; }
    list_del(&e->list);
    mutex_unlock(&probes_lock);

    if (e->kind == 0) unregister_kprobe(&e->kp);
    else              unregister_uprobe(&e->up);
    kfree(e);
    return 0;
}

int jky_uprobe_add(const char *path, unsigned long offset) {
    struct probe_entry *e;
    struct path p;
    int ret;

    ret = kern_path(path, LOOKUP_FOLLOW, &p);
    if (ret < 0) return ret;

    mutex_lock(&probes_lock);
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&probes_lock); path_put(&p); return -ENOMEM; }

    e->id = next_id++;
    e->kind = 1;
    strncpy(e->name, path, sizeof(e->name) - 1);

    e->up.inode   = p.dentry->d_inode;
    e->up.offset  = offset;
    e->up.handler = uprobe_handler;

    ret = register_uprobe(&e->up);
    path_put(&p);
    if (ret < 0) {
        kfree(e);
        mutex_unlock(&probes_lock);
        return ret;
    }

    list_add(&e->list, &probes);
    pr_info("jky: uprobe #%d on %s+0x%lx\n", e->id, e->name, offset);
    mutex_unlock(&probes_lock);
    return e->id;
}

int jky_uprobe_remove(int id) {
    return jky_kprobe_remove(id);
}

int jky_trace_read(void __user *buf, int max_events) {
    struct jky_trace_event e;
    int count = 0;

    while (count < max_events && kfifo_out(&events, &e, 1) == 1) {
        if (copy_to_user((char __user *)buf + count * sizeof(e),
                         &e, sizeof(e)) != 0)
            break;
        count++;
    }
    return count;
}

int jky_trace_clear(void) {
    kfifo_reset(&events);
    return 0;
}

int jky_trace_list(char __user *buf, int max_len) {
    struct probe_entry *e;
    char tmp[2048];
    int n = 0;

    mutex_lock(&probes_lock);
    list_for_each_entry(e, &probes, list) {
        int w = snprintf(tmp + n, sizeof(tmp) - n,
                         "#%d %s %s\n",
                         e->id, e->kind == 0 ? "kprobe" : "uprobe", e->name);
        if (w < 0 || n + w >= (int)sizeof(tmp)) break;
        n += w;
    }
    mutex_unlock(&probes_lock);

    if (n > max_len) n = max_len;
    if (copy_to_user(buf, tmp, n) != 0) return -EFAULT;
    return n;
}

void jky_trace_cleanup(void) {
    struct probe_entry *e, *tmp;
    list_for_each_entry_safe(e, tmp, &probes, list) {
        list_del(&e->list);
        if (e->kind == 0) unregister_kprobe(&e->kp);
        else              unregister_uprobe(&e->up);
        kfree(e);
    }
    kfifo_reset(&events);
}
