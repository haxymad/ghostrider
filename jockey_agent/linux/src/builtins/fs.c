#include "jky_builtins.h"
#include "jky_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static BuiltinResult bi_cwd(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    char buf[4096] = {0};
    if (jky_cwd(buf, sizeof(buf)) != 0) {
        value_set_error("fs_cwd failed");
        return BUILTIN_ERROR;
    }
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_chdir(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_chdir: string required");
        return BUILTIN_ERROR;
    }
    *out = mkbool(jky_chdir(a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_list_dir(VM *vm, Value *a, int c, Value *out) {
    (void)vm;
    const char *path = ".";
    if (c == 1) {
        if (a[0].type != VT_STR) {
            value_set_error("fs_list_dir: string required");
            return BUILTIN_ERROR;
        }
        path = a[0].v.s;
    }

    char **names = NULL;
    int n = 0;
    if (jky_list_dir(path, &names, &n) != 0) {
        *out = mkarray();
        return BUILTIN_OK;
    }
    Value arr = mkarray();
    for (int i = 0; i < n; i++) arr_push(arr.v.arr, mkstr(names[i]));
    jky_free_list(names, n);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_read(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_read: string required");
        return BUILTIN_ERROR;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    if (jky_read_file(a[0].v.s, &data, &len) != 0) {
        *out = mknone();
        return BUILTIN_OK;
    }
    Value v;
    v.type = VT_BYTES;
    v.rc   = 0;
    v.v.bytes.data = data;
    v.v.bytes.len  = len;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_write(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_write: path must be string");
        return BUILTIN_ERROR;
    }

    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[1].type == VT_STR) {
        data = (const uint8_t *)a[1].v.s;
        len  = strlen(a[1].v.s);
    } else if (a[1].type == VT_BYTES) {
        data = a[1].v.bytes.data;
        len  = a[1].v.bytes.len;
    } else {
        value_set_error("fs_write: data must be string or bytes");
        return BUILTIN_ERROR;
    }

    *out = mkbool(jky_write_file(a[0].v.s, data, len) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_exists(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkbool(jky_file_exists(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_size(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint(jky_file_size(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_stat(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) {
        value_set_error("fs_stat: string required");
        return BUILTIN_ERROR;
    }
    struct stat st;
    if (stat(a[0].v.s, &st) != 0) {
        *out = mknone();
        return BUILTIN_OK;
    }
    Value d = mkdict();
    dict_set(d.v.dict, "size",  mkint((int64_t)st.st_size));
    dict_set(d.v.dict, "mode",  mkint((int64_t)st.st_mode));
    dict_set(d.v.dict, "mtime", mkint((int64_t)st.st_mtime));
    dict_set(d.v.dict, "ctime", mkint((int64_t)st.st_ctime));
    dict_set(d.v.dict, "atime", mkint((int64_t)st.st_atime));
    dict_set(d.v.dict, "uid",   mkint((int64_t)st.st_uid));
    dict_set(d.v.dict, "gid",   mkint((int64_t)st.st_gid));
    dict_set(d.v.dict, "inode", mkint((int64_t)st.st_ino));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_hash_dir(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkdict();
    return BUILTIN_OK;
}

static BuiltinResult bi_hide(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(0);
    return BUILTIN_OK;
}

static BuiltinResult bi_unhide(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    *out = mkbool(0);
    return BUILTIN_OK;
}

const Builtin BUILTINS_FS[] = {
    { "fs_cwd",      0, 0, bi_cwd      },
    { "fs_chdir",    1, 1, bi_chdir    },
    { "fs_list_dir", 0, 1, bi_list_dir },
    { "fs_read",     1, 1, bi_read     },
    { "fs_write",    2, 2, bi_write    },
    { "fs_exists",   1, 1, bi_exists   },
    { "fs_size",     1, 1, bi_size     },
    { "fs_stat",     1, 1, bi_stat     },
    { "fs_hash_dir", 1, 1, bi_hash_dir },
    { "fs_hide",     1, 1, bi_hide     },
    { "fs_unhide",   1, 1, bi_unhide   },
    /* web UI aliases */
    { "cwd",         0, 0, bi_cwd      },
    { "chdir",       1, 1, bi_chdir    },
    { "list_dir",    0, 1, bi_list_dir },
    { "read_file",   1, 1, bi_read     },
    { "file_exists", 1, 1, bi_exists   },
    { "file_size",   1, 1, bi_size     },
    { NULL, 0, 0, NULL },
};
