#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include "hooks.h"
#include "trace.h"

#define DEVICE_NAME "jky"
#define CLASS_NAME  "jky"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jky");
MODULE_DESCRIPTION("Jockey kernel bridge");
MODULE_VERSION("1.0");

static int    major;
static struct class  *jky_class;
static struct device *jky_device;

extern long jky_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

static int dev_open(struct inode *i, struct file *f) { (void)i; (void)f; return 0; }
static int dev_release(struct inode *i, struct file *f) { (void)i; (void)f; return 0; }

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = dev_open,
    .release        = dev_release,
    .unlocked_ioctl = jky_ioctl,
};

static int __init jky_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) return major;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    jky_class = class_create(CLASS_NAME);
#else
    jky_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(jky_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(jky_class);
    }

    jky_device = device_create(jky_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(jky_device)) {
        class_destroy(jky_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(jky_device);
    }

    if (jky_hooks_install() < 0)
        pr_warn("jky: hooks install failed; /dev/%s still usable\n", DEVICE_NAME);

    pr_info("jky: loaded, /dev/%s major=%d\n", DEVICE_NAME, major);
    return 0;
}

static void __exit jky_exit(void) {
    jky_trace_cleanup();
    jky_hooks_remove();
    device_destroy(jky_class, MKDEV(major, 0));
    class_destroy(jky_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("jky: unloaded\n");
}

module_init(jky_init);
module_exit(jky_exit);
