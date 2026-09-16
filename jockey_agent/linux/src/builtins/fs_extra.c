#include "jky_builtins.h"
#include "jky_platform.h"
#include "_stubs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static BuiltinResult bi_file_stat(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mknone(); return BUILTIN_OK; }
    struct stat st;
    if (stat(a[0].v.s, &st) != 0) { *out = mknone(); return BUILTIN_OK; }
    Value d = mkdict();
    dict_set(d.v.dict, "size",  mkint((int64_t)st.st_size));
    dict_set(d.v.dict, "mode",  mkint((int64_t)st.st_mode));
    dict_set(d.v.dict, "uid",   mkint((int64_t)st.st_uid));
    dict_set(d.v.dict, "gid",   mkint((int64_t)st.st_gid));
    dict_set(d.v.dict, "inode", mkint((int64_t)st.st_ino));
    dict_set(d.v.dict, "mtime", mkint((int64_t)st.st_mtime));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_file_inode(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkint(-1); return BUILTIN_OK; }
    struct stat st;
    if (stat(a[0].v.s, &st) != 0) { *out = mkint(-1); return BUILTIN_OK; }
    *out = mkint((int64_t)st.st_ino);
    return BUILTIN_OK;
}

static BuiltinResult bi_file_superblock(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkdict(); return BUILTIN_OK; }
    struct statvfs sv;
    if (statvfs(a[0].v.s, &sv) != 0) { *out = mkdict(); return BUILTIN_OK; }
    Value d = mkdict();
    dict_set(d.v.dict, "block_size", mkint((int64_t)sv.f_bsize));
    dict_set(d.v.dict, "blocks",     mkint((int64_t)sv.f_blocks));
    dict_set(d.v.dict, "free",       mkint((int64_t)sv.f_bfree));
    dict_set(d.v.dict, "files",      mkint((int64_t)sv.f_files));
    dict_set(d.v.dict, "fsid",       mkint((int64_t)sv.f_fsid));
    *out = d;
    return BUILTIN_OK;
}

static BuiltinResult bi_file_timeline(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR) { *out = mkarray(); return BUILTIN_OK; }
    struct stat st;
    if (stat(a[0].v.s, &st) != 0) { *out = mkarray(); return BUILTIN_OK; }
    Value arr = mkarray();
    Value e1 = mkdict();
    dict_set(e1.v.dict, "event", mkstr("access")); dict_set(e1.v.dict, "time", mkint((int64_t)st.st_atime));
    Value e2 = mkdict();
    dict_set(e2.v.dict, "event", mkstr("modify")); dict_set(e2.v.dict, "time", mkint((int64_t)st.st_mtime));
    Value e3 = mkdict();
    dict_set(e3.v.dict, "event", mkstr("change")); dict_set(e3.v.dict, "time", mkint((int64_t)st.st_ctime));
    arr_push(arr.v.arr, e1); arr_push(arr.v.arr, e2); arr_push(arr.v.arr, e3);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_file_compare(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != VT_STR || a[1].type != VT_STR) { *out = mkbool(0); return BUILTIN_OK; }
    FILE *f1 = fopen(a[0].v.s, "rb");
    FILE *f2 = fopen(a[1].v.s, "rb");
    if (!f1 || !f2) { if (f1) fclose(f1); if (f2) fclose(f2); *out = mkbool(0); return BUILTIN_OK; }
    int same = 1;
    for (;;) {
        unsigned char b1, b2;
        size_t r1 = fread(&b1, 1, 1, f1);
        size_t r2 = fread(&b2, 1, 1, f2);
        if (r1 != r2) { same = 0; break; }
        if (r1 == 0) break;
        if (b1 != b2) { same = 0; break; }
    }
    fclose(f1); fclose(f2);
    *out = mkbool(same);
    return BUILTIN_OK;
}

STUB_DICT(bi_file_acl)
STUB_ARRAY(bi_file_ads)
STUB_ARRAY(bi_file_carve)
STUB_DICT(bi_file_recover)
STUB_DICT(bi_file_mft)
STUB_ARRAY(bi_file_journal)
STUB_DICT(bi_file_dentry)
STUB_DICT(bi_file_metadata)
STUB_STR0(bi_file_bodyfile)

const Builtin BUILTINS_FS_EXTRA[] = {
    { "file_hash",      1, 1, bi_file_stat     },  /* alias for fs_hash by same name */
    { "file_stat",      1, 1, bi_file_stat     },
    { "file_acl",       1, 1, bi_file_acl      },
    { "file_ads",       1, 1, bi_file_ads      },
    { "file_timeline",  1, 1, bi_file_timeline },
    { "file_bodyfile",  1, 1, bi_file_bodyfile },
    { "file_carve",     2, 2, bi_file_carve    },
    { "file_recover",   1, 1, bi_file_recover  },
    { "file_mft",       1, 1, bi_file_mft      },
    { "file_journal",   1, 1, bi_file_journal  },
    { "file_compare",   2, 2, bi_file_compare  },
    { "file_watch",     1, 1, bi_file_bodyfile },  /* stub via str0 */
    { "file_inode",     1, 1, bi_file_inode    },
    { "file_dentry",    1, 1, bi_file_dentry   },
    { "file_superblock",1, 1, bi_file_superblock},
    { "file_metadata",  1, 1, bi_file_metadata },
    { NULL, 0, 0, NULL },
};
