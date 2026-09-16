#ifndef JKY_VALUE_H
#define JKY_VALUE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VT_INT    = 0,
    VT_FLOAT  = 1,
    VT_STR    = 2,
    VT_BOOL   = 3,
    VT_NONE   = 4,
    VT_ARRAY  = 5,
    VT_DICT   = 6,
    VT_BYTES  = 7,
} ValueType;

typedef struct Array     Array;
typedef struct Dict      Dict;
typedef struct DictEntry DictEntry;

struct DictEntry {
    char         *key;
    struct Value *value;
    DictEntry    *next;
};

struct Array {
    struct Value *items;
    int           len, cap;
    int           rc;
};

struct Dict {
    DictEntry *head;
    int        rc;
};

typedef struct Value {
    ValueType type;
    int       rc;
    union {
        int64_t  i;
        double   f;
        char    *s;
        Array   *arr;
        Dict    *dict;
        struct { uint8_t *data; size_t len; } bytes;
    } v;
} Value;

/* constructors */
Value mkint  (int64_t i);
Value mkfloat(double f);
Value mkstr  (const char *s);
Value mkstrn (const char *s, size_t n);
Value mkbool (int b);
Value mknone (void);
Value mkarray(void);
Value mkdict (void);

/* lifetime */
void  vfree (Value *v);
Value vcopy (Value v);      /* deep copy */
Value vshare(Value v);      /* share heap refs (arrays/dicts); copy scalars */

/* containers */
void  arr_push(Array *a, Value v);
void  dict_set(Dict *d, const char *key, Value v);
Value dict_get(Dict *d, const char *key);   /* returns a shared value */

/* semantic helpers */
int   value_truthy(Value v);
char *value_to_str(Value v);

Value value_add  (Value a, Value b);
Value value_sub  (Value a, Value b);
Value value_mul  (Value a, Value b);
Value value_div  (Value a, Value b);
Value value_mod  (Value a, Value b);

Value value_eq   (Value a, Value b);
Value value_ne   (Value a, Value b);
Value value_cmp  (Value a, Value b, int op);

Value value_not  (Value v);
Value value_neg  (Value v);
Value value_bitop(Value a, Value b, int op);

Value value_len  (Value v);
Value value_index(Value obj, Value idx);
int   value_set_index(Value obj, Value idx, Value val);

/* error slot */
void        value_clear_error(void);
int         value_has_error(void);
const char *value_get_error(void);
void        value_set_error(const char *fmt, ...);

#endif
