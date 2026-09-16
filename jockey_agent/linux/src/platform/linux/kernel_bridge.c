#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdint.h>

#define JKY_MAGIC 'J'
#define JKY_CMD_HIDE_PID       _IOW(JKY_MAGIC, 1, struct jky_req)
#define JKY_CMD_UNHIDE_PID     _IOW(JKY_MAGIC, 2, struct jky_req)
#define JKY_CMD_HIDE_FILE      _IOW(JKY_MAGIC, 3, struct jky_req)
#define JKY_CMD_UNHIDE_FILE    _IOW(JKY_MAGIC, 4, struct jky_req)
#define JKY_CMD_GET_ROOT       _IO (JKY_MAGIC, 5)
#define JKY_CMD_KILL_PID       _IOW(JKY_MAGIC, 8, struct jky_req)
#define JKY_CMD_KPROBE_ADD     _IOWR(JKY_MAGIC, 20, struct jky_trace_req)
#define JKY_CMD_KPROBE_REMOVE  _IOW (JKY_MAGIC, 21, struct jky_trace_req)
#define JKY_CMD_UPROBE_ADD     _IOWR(JKY_MAGIC, 22, struct jky_trace_req)
#define JKY_CMD_UPROBE_REMOVE  _IOW (JKY_MAGIC, 23, struct jky_trace_req)
#define JKY_CMD_TRACE_READ     _IOWR(JKY_MAGIC, 24, struct jky_trace_req)
#define JKY_CMD_TRACE_CLEAR    _IO  (JKY_MAGIC, 25)
#define JKY_CMD_TRACE_LIST     _IOWR(JKY_MAGIC, 26, struct jky_trace_req)
#define JKY_CMD_PING           _IO (JKY_MAGIC, 99)

struct jky_req {
    int32_t  pid;
    uint64_t addr;
    uint64_t size;
    char     path[256];
    char     pad[64];
};

struct jky_trace_req {
    char     symbol[128];
    char     path[256];
    uint64_t offset;
    int32_t  id;
    int32_t  max_events;
};

static int g_fd = -1;

static int open_dev(void) {
    if (g_fd >= 0) return g_fd;
    g_fd = open("/dev/jky", O_RDWR);
    return g_fd;
}

int jky_kmod_ensure_loaded(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_PING, 0) == 0x4a4b59 ? 0 : -1;
}

void jky_kmod_close(void) {
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
}

int jky_kmod_hide_pid(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_HIDE_PID, &r);
}

int jky_kmod_unhide_pid(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_UNHIDE_PID, &r);
}

int jky_kmod_hide_file(const char *path) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    return ioctl(g_fd, JKY_CMD_HIDE_FILE, &r);
}

int jky_kmod_unhide_file(const char *path) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    return ioctl(g_fd, JKY_CMD_UNHIDE_FILE, &r);
}

int jky_kmod_get_root(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_GET_ROOT, 0);
}

int jky_kmod_kill(int pid) {
    if (open_dev() < 0) return -1;
    struct jky_req r; memset(&r, 0, sizeof(r));
    r.pid = pid;
    return ioctl(g_fd, JKY_CMD_KILL_PID, &r);
}

int jky_kmod_kprobe_add(const char *symbol) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    strncpy(r.symbol, symbol, sizeof(r.symbol) - 1);
    return ioctl(g_fd, JKY_CMD_KPROBE_ADD, &r);
}

int jky_kmod_kprobe_remove(int id) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.id = id;
    return ioctl(g_fd, JKY_CMD_KPROBE_REMOVE, &r);
}

int jky_kmod_uprobe_add(const char *path, uint64_t offset) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.offset = offset;
    return ioctl(g_fd, JKY_CMD_UPROBE_ADD, &r);
}

int jky_kmod_uprobe_remove(int id) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.id = id;
    return ioctl(g_fd, JKY_CMD_UPROBE_REMOVE, &r);
}

int jky_kmod_trace_read(void *buf, int max_events) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)(uintptr_t)buf;
    r.max_events = max_events;
    return ioctl(g_fd, JKY_CMD_TRACE_READ, &r);
}

int jky_kmod_trace_clear(void) {
    if (open_dev() < 0) return -1;
    return ioctl(g_fd, JKY_CMD_TRACE_CLEAR, 0);
}

int jky_kmod_trace_list(char *buf, int max_len) {
    if (open_dev() < 0) return -1;
    struct jky_trace_req r; memset(&r, 0, sizeof(r));
    r.offset = (uint64_t)(uintptr_t)buf;
    r.max_events = max_len;
    return ioctl(g_fd, JKY_CMD_TRACE_LIST, &r);
}
