#include "jockey_vm.h"
#include "jky_builtins.h"
#include "jky_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define TRACE_ON() (getenv("JKY_TRACE") != NULL)

static void push(VM *vm, Value v) {
    if (vm->slen >= vm->scap) {
        vm->scap  = vm->scap ? vm->scap * 2 : VM_STACK_INIT;
        vm->stack = realloc(vm->stack, vm->scap * sizeof(Value));
    }
    vm->stack[vm->slen++] = v;
}

static Value popv(VM *vm) {
    if (vm->slen <= 0) return mknone();
    return vm->stack[--vm->slen];
}

static void pop_free(VM *vm) { Value v = popv(vm); vfree(&v); }

int vm_error(VM *vm, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vm->errmsg, sizeof(vm->errmsg), fmt, ap);
    va_end(ap);
    vm->err = 1;
    fprintf(stderr, "vm error: %s\n", vm->errmsg);
    return -1;
}

static void frame_free(Frame *f, int nvars) {
    if (f->locals) {
        for (int i = 0; i < nvars; i++)
            if (f->set[i]) vfree(&f->locals[i]);
        free(f->locals);
        free(f->set);
        f->locals = NULL;
        f->set    = NULL;
    }
}

void vm_init(VM *vm) { memset(vm, 0, sizeof(*vm)); vm->out = stdout; }
void vm_set_output(VM *vm, FILE *f) { vm->out = f; }

void vm_free(VM *vm) {
    free(vm->stack);
    if (vm->prog) {
        for (int i = 0; i < vm->prog->nconsts; i++) vfree(&vm->prog->consts[i]);
        free(vm->prog->consts);
        for (int i = 0; i < vm->prog->nvars; i++) free(vm->prog->vars[i]);
        free(vm->prog->vars);
        for (int i = 0; i < vm->prog->nfuncs; i++) {
            free(vm->prog->funcs[i].name);
            free(vm->prog->funcs[i].param_idx);
            free(vm->prog->funcs[i].code);
        }
        free(vm->prog->funcs);
        free(vm->prog->main_code);
        free(vm->prog);
    }
    memset(vm, 0, sizeof(*vm));
}

static Function *find_fn(VM *vm, const char *name) {
    for (int i = 0; i < vm->prog->nfuncs; i++)
        if (strcmp(vm->prog->funcs[i].name, name) == 0)
            return &vm->prog->funcs[i];
    return NULL;
}

static int do_call(VM *vm, Frame *caller, int a1, int a2) {
    const char *name = vm->prog->vars[a1];
    if (a2 < 0 || a2 > VM_CALL_ARGS)
        return vm_error(vm, "bad arg count %d", a2);

    Value args[VM_CALL_ARGS];
    for (int i = 0; i < a2; i++) args[i] = mknone();
    for (int i = a2 - 1; i >= 0; i--) args[i] = popv(vm);

    Value out = mknone();
    BuiltinResult br = builtin_call(vm, name, args, a2, &out);

    if (br == BUILTIN_OK) {
        for (int i = 0; i < a2; i++) vfree(&args[i]);
        push(vm, out);
        caller->pc++;
        return 0;
    }
    if (br == BUILTIN_ERROR) {
        for (int i = 0; i < a2; i++) vfree(&args[i]);
        return vm_error(vm, "%s", value_get_error());
    }

    Function *fn = find_fn(vm, name);
    if (!fn) {
        for (int i = 0; i < a2; i++) vfree(&args[i]);
        return vm_error(vm, "undefined function '%s'", name);
    }
    if (fn->nparams != a2) {
        for (int i = 0; i < a2; i++) vfree(&args[i]);
        return vm_error(vm, "%s() takes %d args, got %d", name, fn->nparams, a2);
    }
    if (vm->fcount >= VM_FRAME_MAX) {
        for (int i = 0; i < a2; i++) vfree(&args[i]);
        return vm_error(vm, "call stack overflow");
    }

    caller->pc++;

    Frame *nf = &vm->frames[vm->fcount++];
    nf->code       = fn->code;
    nf->len        = fn->code_len;
    nf->pc         = 0;
    nf->stack_base = vm->slen;
    nf->locals     = calloc(vm->prog->nvars > 0 ? vm->prog->nvars : 1, sizeof(Value));
    nf->set        = calloc(vm->prog->nvars > 0 ? vm->prog->nvars : 1, 1);

    for (int i = 0; i < a2; i++) {
        int idx = fn->param_idx[i];
        if (idx < 0 || idx >= vm->prog->nvars) {
            for (int k = 0; k < a2; k++) vfree(&args[k]);
            frame_free(nf, vm->prog->nvars);
            vm->fcount--;
            return vm_error(vm, "bad param index for '%s'", name);
        }
        nf->locals[idx] = args[i];
        nf->set[idx]    = 1;
    }
    return 0;
}

static Value load_var(VM *vm, Frame *f, int idx) {
    if (idx < 0 || idx >= vm->prog->nvars) return mknone();
    if (f->set[idx]) return vshare(f->locals[idx]);
    if (vm->fcount > 1) {
        Frame *g = &vm->frames[0];
        if (g->set[idx]) return vshare(g->locals[idx]);
    }
    return mknone();
}

static int run(VM *vm) {
    while (vm->fcount > 0) {
        Frame *f = &vm->frames[vm->fcount - 1];

        if (f->pc < 0 || f->pc >= f->len) {
            if (vm->fcount == 1) return 0;
            while (vm->slen > f->stack_base) pop_free(vm);
            frame_free(f, vm->prog->nvars);
            vm->fcount--;
            push(vm, mknone());
            continue;
        }

        Instr ins = f->code[f->pc];

        if (TRACE_ON())
            fprintf(stderr, "pc=%d op=%d a1=%d a2=%d slen=%d fcount=%d\n",
                    f->pc, ins.op, ins.a1, ins.a2, vm->slen, vm->fcount);

        switch (ins.op) {
        case OP_PUSH_CONST: push(vm, vshare(vm->prog->consts[ins.a1])); f->pc++; break;

        case OP_PUSH_STR: {
            Value c = vm->prog->consts[ins.a1];
            if (c.type != JKY_STR) { push(vm, mkstr("")); f->pc++; break; }
            size_t n = strlen(c.v.s);
            char *s = malloc(n + 1);
            if (ins.a2)
                for (size_t k = 0; k < n; k++) s[k] = c.v.s[k] ^ (char)ins.a2;
            else
                memcpy(s, c.v.s, n);
            s[n] = 0;
            push(vm, mkstr(s));
            free(s);
            f->pc++; break;
        }

        case OP_PUSH_INT: {
            int64_t x = vm->prog->consts[ins.a1].v.i;
            if (ins.a2) x ^= ins.a2;
            push(vm, mkint(x));
            f->pc++; break;
        }

        case OP_LOAD_VAR:  push(vm, load_var(vm, f, ins.a1)); f->pc++; break;

        case OP_STORE_VAR: {
            int idx = ins.a1;
            Value v = popv(vm);
            if (idx >= 0 && idx < vm->prog->nvars) {
                if (f->set[idx]) vfree(&f->locals[idx]);
                f->locals[idx] = v;
                f->set[idx]    = 1;
            } else vfree(&v);
            f->pc++; break;
        }

        case OP_POP: pop_free(vm); f->pc++; break;

        case OP_ADD: { Value b=popv(vm),a=popv(vm); Value r=value_add(a,b); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_SUB: { Value b=popv(vm),a=popv(vm); Value r=value_sub(a,b); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_MUL: { Value b=popv(vm),a=popv(vm); Value r=value_mul(a,b); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_DIV: { Value b=popv(vm),a=popv(vm); Value r=value_div(a,b); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_MOD: { Value b=popv(vm),a=popv(vm); Value r=value_mod(a,b); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_EQ:  { Value b=popv(vm),a=popv(vm); Value r=value_eq(a,b);  vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_NE:  { Value b=popv(vm),a=popv(vm); Value r=value_ne(a,b);  vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_LT:  { Value b=popv(vm),a=popv(vm); Value r=value_cmp(a,b,OP_LT); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_GT:  { Value b=popv(vm),a=popv(vm); Value r=value_cmp(a,b,OP_GT); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_LE:  { Value b=popv(vm),a=popv(vm); Value r=value_cmp(a,b,OP_LE); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_GE:  { Value b=popv(vm),a=popv(vm); Value r=value_cmp(a,b,OP_GE); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_BIT_OR:  { Value b=popv(vm),a=popv(vm); Value r=value_bitop(a,b,OP_BIT_OR);  vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_BIT_AND: { Value b=popv(vm),a=popv(vm); Value r=value_bitop(a,b,OP_BIT_AND); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_BIT_XOR: { Value b=popv(vm),a=popv(vm); Value r=value_bitop(a,b,OP_BIT_XOR); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }

        case OP_AND: { Value b=popv(vm),a=popv(vm); Value r=mkbool(value_truthy(a)&&value_truthy(b)); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_OR:  { Value b=popv(vm),a=popv(vm); Value r=mkbool(value_truthy(a)||value_truthy(b)); vfree(&a);vfree(&b); push(vm,r); f->pc++; break; }
        case OP_NOT: { Value v=popv(vm); Value r=value_not(v); vfree(&v); push(vm,r); f->pc++; break; }
        case OP_NEG: { Value v=popv(vm); Value r=value_neg(v); vfree(&v); push(vm,r); f->pc++; break; }

        case OP_BIT_NOT: {
            Value v = popv(vm);
            if (v.type != JKY_INT) { vfree(&v); return vm_error(vm, "bitwise not requires int"); }
            Value r = mkint(~v.v.i);
            vfree(&v); push(vm, r); f->pc++; break;
        }

        case OP_LEN: { Value v=popv(vm); Value r=value_len(v); vfree(&v); push(vm,r); f->pc++; break; }
        case OP_INDEX: { Value idx=popv(vm),obj=popv(vm); Value r=value_index(obj,idx); vfree(&idx);vfree(&obj); push(vm,r); f->pc++; break; }

        case OP_SET_INDEX: {
            Value val=popv(vm), idx=popv(vm), obj=popv(vm);
            int rc = value_set_index(obj, idx, val);
            if (rc != 0) {
                vfree(&val); vfree(&idx); vfree(&obj);
                return vm_error(vm, "%s", value_get_error());
            }
            vfree(&idx); vfree(&obj);
            f->pc++; break;
        }

        case OP_BUILD_ARRAY: {
            int n = ins.a1;
            if (n < 0 || n > vm->slen)
                return vm_error(vm, "stack underflow (BUILD_ARRAY n=%d slen=%d)", n, vm->slen);
            Value arr = mkarray();
            for (int i = 0; i < n; i++)
                arr_push(arr.v.arr, vm->stack[vm->slen - n + i]);
            vm->slen -= n;
            push(vm, arr);
            f->pc++; break;
        }

        case OP_BUILD_DICT: {
            int n = ins.a1;
            if (n < 0 || 2 * n > vm->slen)
                return vm_error(vm, "stack underflow (BUILD_DICT n=%d slen=%d)", n, vm->slen);
            Value d = mkdict();
            for (int i = 0; i < n; i++) {
                Value v = popv(vm), k = popv(vm);
                if (k.type != JKY_STR) {
                    vfree(&k); vfree(&v);
                    return vm_error(vm, "dict keys must be strings");
                }
                dict_set(d.v.dict, k.v.s, v);
                vfree(&k);
            }
            push(vm, d);
            f->pc++; break;
        }

        case OP_JUMP: f->pc = ins.a1; break;
        case OP_JUMP_IF_FALSE: { Value c=popv(vm); int t=value_truthy(c); vfree(&c); f->pc = t ? f->pc+1 : ins.a1; break; }
        case OP_JUMP_IF_TRUE:  { Value c=popv(vm); int t=value_truthy(c); vfree(&c); f->pc = t ? ins.a1 : f->pc+1; break; }
        case OP_NOP: f->pc++; break;

        case OP_CALL: if (do_call(vm, f, ins.a1, ins.a2) != 0) return -1; break;

        case OP_RETURN: {
            if (vm->fcount <= 1) return 0;
            Value rv = mknone();
            if (vm->slen > f->stack_base) rv = popv(vm);
            while (vm->slen > f->stack_base) pop_free(vm);
            frame_free(f, vm->prog->nvars);
            vm->fcount--;
            push(vm, rv);
            break;
        }

        case OP_GET_ATTR: {
            Value v = popv(vm);
            const char *name = vm->prog->vars[ins.a1];
            Value r = mknone();
            if (v.type == JKY_DICT) r = dict_get(v.v.dict, name);
            vfree(&v);
            push(vm, r);
            f->pc++; break;
        }

        case OP_PUSH_VAR: { push(vm, load_var(vm, f, ins.a1)); f->pc++; break; }
        case OP_POP_VAR: {
            Value v = popv(vm);
            int idx = ins.a1;
            if (idx >= 0 && idx < vm->prog->nvars) {
                if (f->set[idx]) vfree(&f->locals[idx]);
                f->locals[idx] = v;
                f->set[idx]    = 1;
            } else vfree(&v);
            f->pc++; break;
        }
        case OP_ADD2: {
            Value b = load_var(vm, f, ins.a2);
            Value a = load_var(vm, f, ins.a1);
            Value r = value_add(a, b);
            vfree(&a); vfree(&b);
            push(vm, r); f->pc++; break;
        }
        case OP_SUB2: {
            Value b = load_var(vm, f, ins.a2);
            Value a = load_var(vm, f, ins.a1);
            Value r = value_sub(a, b);
            vfree(&a); vfree(&b);
            push(vm, r); f->pc++; break;
        }
        case OP_MUL2: {
            Value b = load_var(vm, f, ins.a2);
            Value a = load_var(vm, f, ins.a1);
            Value r = value_mul(a, b);
            vfree(&a); vfree(&b);
            push(vm, r); f->pc++; break;
        }
        case OP_DIV2: {
            Value b = load_var(vm, f, ins.a2);
            Value a = load_var(vm, f, ins.a1);
            Value r = value_div(a, b);
            vfree(&a); vfree(&b);
            push(vm, r); f->pc++; break;
        }
        case OP_CMP2: {
            Value b = load_var(vm, f, ins.a2);
            Value a = load_var(vm, f, ins.a1);
            Value eq = value_eq(a, b);
            vfree(&a); vfree(&b);
            push(vm, mkint(value_truthy(eq) ? 1 : 0));
            vfree(&eq);
            f->pc++; break;
        }
        case OP_ADD_CONST: {
            Value c = vshare(vm->prog->consts[ins.a1]);
            Value top = popv(vm);
            Value r = value_add(top, c);
            vfree(&top); vfree(&c);
            push(vm, r); f->pc++; break;
        }
        case OP_SUB_CONST: {
            Value c = vshare(vm->prog->consts[ins.a1]);
            Value top = popv(vm);
            Value r = value_sub(top, c);
            vfree(&top); vfree(&c);
            push(vm, r); f->pc++; break;
        }
        case OP_LOAD_CONST: {
            push(vm, vshare(vm->prog->consts[ins.a1]));
            f->pc++; break;
        }
        case OP_CALL_BUILTIN: {
            int arity = ins.a2;
            Value *args = calloc(arity, sizeof(Value));
            for (int i = arity - 1; i >= 0; i--) args[i] = popv(vm);
            const char *fname = vm->prog->vars[ins.a1];
            Value out = mknone();
            BuiltinResult br = builtin_call(vm, fname, args, arity, &out);
            for (int i = 0; i < arity; i++) vfree(&args[i]);
            free(args);
            if (br == BUILTIN_ERROR) {
                vfree(&out);
                return -1;
            }
            push(vm, out);
            f->pc++; break;
        }
        case OP_SHL: {
            Value b = popv(vm), a = popv(vm);
            if (a.type != JKY_INT || b.type != JKY_INT) {
                vfree(&a); vfree(&b);
                return vm_error(vm, "shift requires int operands");
            }
            push(vm, mkint(a.v.i << b.v.i));
            vfree(&a); vfree(&b);
            f->pc++; break;
        }
        case OP_SHR: {
            Value b = popv(vm), a = popv(vm);
            if (a.type != JKY_INT || b.type != JKY_INT) {
                vfree(&a); vfree(&b);
                return vm_error(vm, "shift requires int operands");
            }
            push(vm, mkint(a.v.i >> b.v.i));
            vfree(&a); vfree(&b);
            f->pc++; break;
        }
        case OP_DUP: {
            if (vm->slen > 0) {
                Value top = vm->stack[vm->slen - 1];
                push(vm, vshare(top));
            }
            f->pc++; break;
        }

        default:
            return vm_error(vm, "unknown opcode %d at pc %d", ins.op, f->pc);
        }
    }
    return 0;
}

int vm_execute(VM *vm) {
    if (!vm || !vm->prog) return -1;
    vm->err    = 0;
    vm->slen   = 0;
    vm->fcount = 0;

    Frame *f0 = &vm->frames[vm->fcount++];
    f0->code       = vm->prog->main_code;
    f0->len        = vm->prog->main_len;
    f0->pc         = 0;
    f0->stack_base = 0;
    f0->locals     = calloc(vm->prog->nvars > 0 ? vm->prog->nvars : 1, sizeof(Value));
    f0->set        = calloc(vm->prog->nvars > 0 ? vm->prog->nvars : 1, 1);

    int r = run(vm);

    while (vm->fcount > 0) {
        frame_free(&vm->frames[vm->fcount - 1], vm->prog->nvars);
        vm->fcount--;
    }
    return r;
}

void vm_dump_stack(VM *vm) {
    fprintf(stderr, "[stack %d]\n", vm->slen);
    for (int i = 0; i < vm->slen; i++) {
        char *s = value_to_str(vm->stack[i]);
        fprintf(stderr, "  [%d] %s\n", i, s);
        free(s);
    }
}

const char *opcode_name(uint8_t op) {
    switch (op) {
        case 0:  return "PUSH_CONST";
        case 1:  return "PUSH_STR";
        case 2:  return "PUSH_INT";
        case 3:  return "LOAD_VAR";
        case 4:  return "STORE_VAR";
        case 5:  return "POP";
        case 6:  return "ADD";
        case 7:  return "SUB";
        case 8:  return "MUL";
        case 9:  return "DIV";
        case 10: return "MOD";
        case 11: return "EQ";
        case 12: return "NE";
        case 13: return "LT";
        case 14: return "GT";
        case 15: return "LE";
        case 16: return "GE";
        case 17: return "AND";
        case 18: return "OR";
        case 19: return "NOT";
        case 20: return "NEG";
        case 21: return "BIT_NOT";
        case 22: return "BIT_OR";
        case 23: return "BIT_AND";
        case 24: return "BIT_XOR";
        case 25: return "CALL";
        case 26: return "LEN";
        case 27: return "RETURN";
        case 28: return "GET_ATTR";
        case 29: return "BUILD_ARRAY";
        case 30: return "BUILD_DICT";
        case 31: return "JUMP";
        case 32: return "JUMP_IF_FALSE";
        case 33: return "JUMP_IF_TRUE";
        case 34: return "NOP";
        case 35: return "INDEX";
        case 36: return "SET_INDEX";
        case 37: return "PUSH_VAR";
        case 38: return "POP_VAR";
        case 39: return "ADD2";
        case 40: return "SUB2";
        case 41: return "MUL2";
        case 42: return "DIV2";
        case 43: return "CMP2";
        case 44: return "ADD_CONST";
        case 45: return "SUB_CONST";
        case 46: return "LOAD_CONST";
        case 47: return "CALL_BUILTIN";
        case 48: return "SHL";
        case 49: return "SHR";
        case 50: return "DUP";
        default:  return "UNKNOWN";
    }
}