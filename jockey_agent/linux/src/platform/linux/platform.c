#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/stat.h>

const char *jky_platform_name(void) { return "linux"; }

int jky_hostname(char *buf, size_t len) {
    if (gethostname(buf, len) != 0) { buf[0] = 0; return -1; }
    buf[len - 1] = 0;
    return 0;
}

int jky_username(char *buf, size_t len) {
    const char *u = getenv("USER");
    if (!u) u = getenv("USERNAME");
    if (!u) u = getlogin();
    if (!u) { buf[0] = 0; return -1; }
    strncpy(buf, u, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int jky_kernel_version(char *buf, size_t len) {
    struct utsname u;
    if (uname(&u) != 0) { buf[0] = 0; return -1; }
    strncpy(buf, u.release, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int jky_arch(char *buf, size_t len) {
    struct utsname u;
    if (uname(&u) != 0) { buf[0] = 0; return -1; }
    strncpy(buf, u.machine, len - 1);
    buf[len - 1] = 0;
    return 0;
}

int64_t jky_uptime_sec(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) up = 0;
    fclose(f);
    return (int64_t)up;
}

int jky_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
}

int64_t jky_total_mem(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64]; long val; char unit[16];
    int64_t total = 0;
    while (fscanf(f, "%63s %ld %15s", key, &val, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) { total = (int64_t)val * 1024; break; }
    }
    fclose(f);
    return total;
}

int64_t jky_free_mem(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64]; long val; char unit[16];
    int64_t free_mem = 0;
    while (fscanf(f, "%63s %ld %15s", key, &val, unit) == 3) {
        if (strcmp(key, "MemAvailable:") == 0) { free_mem = (int64_t)val * 1024; break; }
        if (strcmp(key, "MemFree:") == 0 && free_mem == 0)
            free_mem = (int64_t)val * 1024;
    }
    fclose(f);
    return free_mem;
}

int64_t jky_time_sec(void) { return (int64_t)time(NULL); }

void jky_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int jky_getpid(void)  { return (int)getpid(); }
int jky_getppid(void) { return (int)getppid(); }

int jky_kill(int pid, int sig) {
    return kill(pid, sig == 0 ? SIGTERM : sig);
}

int jky_cwd(char *buf, size_t len) {
    if (!getcwd(buf, len)) { buf[0] = 0; return -1; }
    return 0;
}

int jky_chdir(const char *path) { return chdir(path); }

int jky_list_dir(const char *path, char ***out_names, int *out_count) {
    DIR *d = opendir(path);
    if (!d) return -1;

    int cap = 16, n = 0;
    char **names = malloc(cap * sizeof(char *));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (n >= cap) { cap *= 2; names = realloc(names, cap * sizeof(char *)); }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);

    for (int i = 1; i < n; i++) {
        char *k = names[i];
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], k) > 0) {
            names[j + 1] = names[j];
            j--;
        }
        names[j + 1] = k;
    }

    *out_names = names;
    *out_count = n;
    return 0;
}

void jky_free_list(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

int jky_read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    *out = buf;
    *out_len = got;
    return 0;
}

int jky_write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}

int jky_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int64_t jky_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}
