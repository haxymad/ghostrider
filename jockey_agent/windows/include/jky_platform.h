#ifndef JKY_PLATFORM_H
#define JKY_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

const char *jky_platform_name(void);
int  jky_hostname(char *buf, size_t len);
int  jky_username(char *buf, size_t len);
int  jky_kernel_version(char *buf, size_t len);
int  jky_arch(char *buf, size_t len);
uint64_t jky_uptime_sec(void);
int  jky_cpu_count(void);
uint64_t jky_total_mem(void);
uint64_t jky_free_mem(void);

int64_t jky_time_sec(void);
void    jky_sleep_ms(int ms);

int jky_getpid(void);
int jky_getppid(void);
int jky_kill(int pid, int sig);

int  jky_cwd(char *buf, size_t len);
int  jky_chdir(const char *path);
int  jky_list_dir(const char *path, char ***out_names, int *out_count);
void jky_free_list(char **names, int count);
int  jky_read_file(const char *path, uint8_t **out, size_t *out_len);
int  jky_write_file(const char *path, const uint8_t *data, size_t len);
int  jky_file_exists(const char *path);
int64_t jky_file_size(const char *path);

#endif