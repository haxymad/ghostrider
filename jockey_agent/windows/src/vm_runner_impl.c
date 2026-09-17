/*
 * vm_runner_impl.c — exposes vm_run_file() for linking into jockstrap.
 *
 * jockey_runner.c contains both main() and vm_run_file(); linking the
 * full file into jockstrap causes a duplicate-symbol error.  This file
 * provides vm_run_file() without main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "jockey_vm.h"
#include "jky_loader.h"

/* pulled in from jockey_runner.c — duplicated here to avoid linking
 * jockey_runner.c (which has its own main) into the static lib          */
static uint8_t *read_all(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

int vm_run_file(const char *path)
{
    size_t len = 0;
    uint8_t *data = read_all(path, &len);
    if (!data) {
        fprintf(stderr, "cannot read %s\n", path);
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
