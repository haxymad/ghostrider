#ifndef JKY_IOCTL_HANDLER_H
#define JKY_IOCTL_HANDLER_H

#include <linux/fs.h>

long jky_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

#endif
