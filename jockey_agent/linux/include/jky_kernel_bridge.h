#ifndef JKY_KERNEL_BRIDGE_H
#define JKY_KERNEL_BRIDGE_H

#include <stdint.h>

int  jky_kmod_ensure_loaded(void);
void jky_kmod_close(void);

int  jky_kmod_hide_pid(int pid);
int  jky_kmod_unhide_pid(int pid);
int  jky_kmod_hide_file(const char *path);
int  jky_kmod_unhide_file(const char *path);
int  jky_kmod_get_root(void);
int  jky_kmod_kill(int pid);

int  jky_kmod_kprobe_add(const char *symbol);
int  jky_kmod_kprobe_remove(int id);
int  jky_kmod_uprobe_add(const char *path, uint64_t offset);
int  jky_kmod_uprobe_remove(int id);
int  jky_kmod_trace_read(void *buf, int max_events);
int  jky_kmod_trace_clear(void);
int  jky_kmod_trace_list(char *buf, int max_len);

#endif
