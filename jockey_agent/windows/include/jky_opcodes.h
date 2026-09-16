#ifndef JKY_OPCODES_H
#define JKY_OPCODES_H

#include <stdint.h>

typedef enum {
    OP_PUSH_CONST     = 0,
    OP_PUSH_STR       = 1,
    OP_PUSH_INT       = 2,
    OP_LOAD_VAR       = 3,
    OP_STORE_VAR      = 4,
    OP_POP            = 5,
    OP_ADD            = 6,
    OP_SUB            = 7,
    OP_MUL            = 8,
    OP_DIV            = 9,
    OP_MOD           = 10,
    OP_EQ            = 11,
    OP_NE            = 12,
    OP_LT            = 13,
    OP_GT            = 14,
    OP_LE            = 15,
    OP_GE            = 16,
    OP_AND           = 17,
    OP_OR            = 18,
    OP_NOT           = 19,
    OP_NEG           = 20,
    OP_BIT_NOT       = 21,
    OP_BIT_OR        = 22,
    OP_BIT_AND       = 23,
    OP_BIT_XOR       = 24,
    OP_CALL          = 25,
    OP_LEN           = 26,
    OP_RETURN        = 27,
    OP_GET_ATTR      = 28,
    OP_BUILD_ARRAY   = 29,
    OP_BUILD_DICT    = 30,
    OP_JUMP          = 31,
    OP_JUMP_IF_FALSE = 32,
    OP_JUMP_IF_TRUE  = 33,
    OP_NOP           = 34,
    OP_INDEX         = 35,
    OP_SET_INDEX     = 36,
    OP__COUNT
} Opcode;

const char *opcode_name(uint8_t op);

#endif