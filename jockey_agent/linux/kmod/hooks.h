#ifndef JKY_HOOKS_H
#define JKY_HOOKS_H

int  jky_hooks_install(void);
void jky_hooks_remove(void);

int  jky_hide_pid(int pid);
int  jky_unhide_pid(int pid);
int  jky_hide_file(const char *path);
int  jky_unhide_file(const char *path);

#endif
