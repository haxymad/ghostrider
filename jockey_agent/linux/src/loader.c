#include "jky_loader.h"
#include <stdlib.h>
#include <string.h>

static int     rd_u8 (const uint8_t **p) { int v = **p; (*p)++; return v; }
static int32_t rd_i32(const uint8_t **p) { int32_t v; memcpy(&v, *p, 4); *p += 4; return v; }
static int64_t rd_i64(const uint8_t **p) { int64_t v; memcpy(&v, *p, 8); *p += 8; return v; }
static double  rd_f64(const uint8_t **p) { double v; memcpy(&v, *p, 8); *p += 8; return v; }

static char *rd_str(const uint8_t **p) {
    int32_t len = rd_i32(p);
    if (len < 0) return NULL;
    char *s = malloc(len + 1);
    memcpy(s, *p, len);
    s[len] = 0;
    *p += len;
    return s;
}

int vm_load(VM *vm, const uint8_t *data, size_t len) {
    const uint8_t *p = data;
    if (len < 8 || memcmp(p, "JKB1", 4) != 0) return -1;
    p += 8;

    Program *prog = calloc(1, sizeof(Program));
    vm->prog = prog;

    int32_t nc = rd_i32(&p);
    prog->consts  = calloc(nc > 0 ? nc : 1, sizeof(Value));
    prog->nconsts = nc;
    for (int i = 0; i < nc; i++) {
        int tag = rd_u8(&p);
        if      (tag == 0) prog->consts[i] = mkint(rd_i64(&p));
        else if (tag == 1) { char *s = rd_str(&p); prog->consts[i] = mkstr(s); free(s); }
        else if (tag == 2) prog->consts[i] = mkfloat(rd_f64(&p));
        else if (tag == 3) prog->consts[i] = mknone();
        else return -1;
    }

    int32_t nv = rd_i32(&p);
    prog->vars  = calloc(nv > 0 ? nv : 1, sizeof(char *));
    prog->nvars = nv;
    for (int i = 0; i < nv; i++) prog->vars[i] = rd_str(&p);

    int32_t nf = rd_i32(&p);
    prog->funcs  = calloc(nf > 0 ? nf : 1, sizeof(Function));
    prog->nfuncs = nf;
    for (int i = 0; i < nf; i++) {
        Function *fn = &prog->funcs[i];
        fn->name     = rd_str(&p);
        fn->nparams  = rd_i32(&p);
        fn->param_idx = calloc(fn->nparams > 0 ? fn->nparams : 1, sizeof(int));
        for (int j = 0; j < fn->nparams; j++) {
            char *pname = rd_str(&p);
            fn->param_idx[j] = -1;
            for (int k = 0; k < prog->nvars; k++) {
                if (strcmp(prog->vars[k], pname) == 0) {
                    fn->param_idx[j] = k;
                    break;
                }
            }
            free(pname);
            if (fn->param_idx[j] < 0) return -1;
        }
        fn->code_len = rd_i32(&p);
        fn->code = calloc(fn->code_len > 0 ? fn->code_len : 1, sizeof(Instr));
        for (int j = 0; j < fn->code_len; j++) {
            fn->code[j].op = (uint8_t)rd_u8(&p);
            fn->code[j].a1 = rd_i32(&p);
            fn->code[j].a2 = rd_i32(&p);
        }
    }

    prog->main_len  = rd_i32(&p);
    prog->main_code = calloc(prog->main_len > 0 ? prog->main_len : 1, sizeof(Instr));
    for (int i = 0; i < prog->main_len; i++) {
        prog->main_code[i].op = (uint8_t)rd_u8(&p);
        prog->main_code[i].a1 = rd_i32(&p);
        prog->main_code[i].a2 = rd_i32(&p);
    }
    return 0;
}
