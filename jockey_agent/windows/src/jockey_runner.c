#include "jockey_vm.h"
#include "jky_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

#define MAX_BYTECODE (16 * 1024 * 1024)

static uint8_t *read_all(const char *path, size_t *out_len) {
    char exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        char *slash = strrchr(exe_dir, '\\');
        if (slash) *slash = '\0';
    }
    char full[MAX_PATH];
    snprintf(full, MAX_PATH, "%s\\%s", exe_dir, path);

    HANDLE h = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > MAX_BYTECODE) {
        CloseHandle(h);
        return NULL;
    }
    uint8_t *buf = malloc(sz);
    DWORD got = 0;
    ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    if (got != sz) { free(buf); return NULL; }
    *out_len = got;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.jkb>\n", argv[0]);
        return 2;
    }

    size_t len = 0;
    uint8_t *data = read_all(argv[1], &len);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    VM vm;
    vm_init(&vm);

    if (vm_load(&vm, data, len) != 0) {
        fprintf(stderr, "bad bytecode file\n");
        free(data);
        vm_free(&vm);
        return 1;
    }
    free(data);

    int r = vm_execute(&vm);
    vm_free(&vm);
    return r == 0 ? 0 : 1;
}
