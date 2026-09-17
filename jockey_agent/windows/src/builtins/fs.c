/*
 * fs.c — Filesystem builtins for Windows.
 */

#include "jky_builtins.h"
#include "jky_platform.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

static BuiltinResult bi_cwd(VM *vm, Value *a, int c, Value *out)
{
    char buf[MAX_PATH];
    (void)vm; (void)a; (void)c;
    jky_cwd(buf, sizeof(buf));
    *out = mkstr(buf);
    return BUILTIN_OK;
}

static BuiltinResult bi_chdir(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkint(jky_chdir(a[0].v.s) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_list_dir(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    const char *path = (c > 0 && a[0].type == JKY_STR) ? a[0].v.s : ".";

    char **names = NULL;
    int count = 0;

    if (jky_list_dir(path, &names, &count) != 0) {
        *out = mkarray();
        return BUILTIN_OK;
    }

    Value arr = mkarray();
    for (int i = 0; i < count; i++) {
        arr_push(arr.v.arr, mkstr(names[i]));
        free(names[i]);
    }
    free(names);

    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_read_file(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkbytes_empty(); return BUILTIN_OK; }

    uint8_t *data = NULL;
    size_t len = 0;

    if (jky_read_file(a[0].v.s, &data, &len) != 0) {
        *out = mkbytes_empty();
        return BUILTIN_OK;
    }

    Value v;
    v.type = JKY_BYTES;
    v.rc = 0;
    v.v.bytes.data = data;
    v.v.bytes.len = len;
    *out = v;
    return BUILTIN_OK;
}

static BuiltinResult bi_fs_write(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkbool(0); return BUILTIN_OK; }

    const uint8_t *data = NULL;
    size_t len = 0;

    if (a[1].type == JKY_STR) {
        data = (const uint8_t *)a[1].v.s;
        len = strlen(a[1].v.s);
    } else if (a[1].type == JKY_BYTES) {
        data = a[1].v.bytes.data;
        len = a[1].v.bytes.len;
    } else {
        *out = mkbool(0);
        return BUILTIN_OK;
    }

    *out = mkint(jky_write_file(a[0].v.s, data, len) == 0);
    return BUILTIN_OK;
}

static BuiltinResult bi_file_exists(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkbool(0); return BUILTIN_OK; }
    *out = mkint(jky_file_exists(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_file_size(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint(jky_file_size(a[0].v.s));
    return BUILTIN_OK;
}

static BuiltinResult bi_fs_stat(VM *vm, Value *a, int c, Value *out)
{
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = mknone(); return BUILTIN_OK; }

    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesEx(a[0].v.s, GetFileExInfoStandard, &fa)) {
        *out = mknone();
        return BUILTIN_OK;
    }

    Value d = mkdict();
    LARGE_INTEGER size;
    size.HighPart = fa.nFileSizeHigh;
    size.LowPart = fa.nFileSizeLow;

    dict_set(d.v.dict, "size", mkint((int64_t)size.QuadPart));
    dict_set(d.v.dict, "is_dir", mkint((fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
    dict_set(d.v.dict, "hidden", mkint((fa.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0));
    dict_set(d.v.dict, "system", mkint((fa.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0));

    *out = d;
    return BUILTIN_OK;
}

const Builtin BUILTINS_FS[] = {
    { "cwd",         0, 0, bi_cwd         },
    { "chdir",       1, 1, bi_chdir       },
    { "list_dir",    0, 1, bi_list_dir    },
    { "read_file",   1, 1, bi_read_file   },
    { "fs_write",    2, 2, bi_fs_write    },
    { "file_exists", 1, 1, bi_file_exists },
    { "file_size",   1, 1, bi_file_size   },
    { "fs_stat",     1, 1, bi_fs_stat     },
    /* aliases */
    { "cwd",         0, 0, bi_cwd         },
    { "list_dir",    0, 1, bi_list_dir    },
    { "read_file",   1, 1, bi_read_file   },
    { "file_exists", 1, 1, bi_file_exists },
    { "file_size",   1, 1, bi_file_size   },
    { "file_stat",   1, 1, bi_fs_stat     },
    { "fs_hash_dir", 1, 1, bi_list_dir    },
    { "hide_file",   1, 1, bi_file_exists }, /* kernel handles actual hiding */
    { NULL, 0, 0, NULL },
};
