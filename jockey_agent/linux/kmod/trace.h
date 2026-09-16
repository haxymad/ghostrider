#ifndef JKY_TRACE_H
#define JKY_TRACE_H

#include "jky_ioctl.h"

/* register a kprobe on a kernel symbol. Returns id >= 0, or negative errno. */
int jky_kprobe_add(const char *symbol);

/* remove by id */
int jky_kprobe_remove(int id);

/* register a uprobe at offset inside the file at path. Returns id or neg errno. */
int jky_uprobe_add(const char *path, unsigned long offset);

int jky_uprobe_remove(int id);

/* read up to max events into user buffer. Returns count, or negative. */
int jky_trace_read(void __user *buf, int max_events);

/* clear the ring buffer */
int jky_trace_clear(void);

/* write human-readable list of active probes into user buffer */
int jky_trace_list(char __user *buf, int max_len);

void jky_trace_cleanup(void);

#endif
