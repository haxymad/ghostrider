#ifndef JOCKEY_VM_H
#define JOCKEY_VM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "jky_value.h"
#include "jky_opcodes.h"

#define VM_STACK_INIT  64
#define VM_FRAME_MAX   256
#define VM_CALL_ARGS   64

typedef struct {
    uint8_t op;
    int32_t a1;
    int32_t a2;
} Instr;

typedef struct {
    char    *name;
    int     *param_idx;
    int      nparams;
    Instr   *code;
    int      code_len;
} Function;

typedef struct {
    char    **vars;      int nvars;
    Value    *consts;    int nconsts;
    Function *funcs;     int nfuncs;
    Instr    *main_code; int main_len;
} Program;

typedef struct {
    Instr   *code;
    int      len;
    int      pc;
    int      stack_base;
    Value   *locals;
    uint8_t *set;
} Frame;

typedef struct {
    Program *prog;
    Value   *stack;
    int      slen, scap;
    Frame    frames[VM_FRAME_MAX];
    int      fcount;
    int      err;
    char     errmsg[256];
    FILE    *out;
} VM;

void vm_init(VM *vm);
void vm_free(VM *vm);
int  vm_execute(VM *vm);
void vm_set_output(VM *vm, FILE *f);

int  vm_error(VM *vm, const char *fmt, ...);
void vm_dump_stack(VM *vm);

#endif