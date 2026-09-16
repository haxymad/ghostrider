#include "jky_builtins.h"
#include "_stubs.h"

STUB_ARRAY(bi_reg_key)
STUB_NONE (bi_reg_value)
STUB_ARRAY(bi_reg_autoruns)
STUB_ARRAY(bi_reg_services)
STUB_DICT (bi_reg_network)
STUB_ARRAY(bi_reg_user_assist)
STUB_ARRAY(bi_reg_prefetch)
STUB_ARRAY(bi_reg_evtx)
STUB_INT0 (bi_reg_open)

const Builtin BUILTINS_REGISTRY[] = {
    { "reg_open",       1, 1, bi_reg_open        },
    { "reg_key",        1, 1, bi_reg_key         },
    { "reg_value",      2, 2, bi_reg_value       },
    { "reg_autoruns",   0, 0, bi_reg_autoruns    },
    { "reg_services",   0, 0, bi_reg_services    },
    { "reg_network",    0, 0, bi_reg_network     },
    { "reg_user_assist",0, 0, bi_reg_user_assist },
    { "reg_prefetch",   0, 0, bi_reg_prefetch    },
    { "reg_evtx",       0, 0, bi_reg_evtx        },
    { NULL, 0, 0, NULL },
};
