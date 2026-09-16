/* builtins_stub.c — empty builtin tables for the runner build.
 *
 * The runner (jockey_runner.exe) does not need any real builtins.
 * This file provides the 12 BUILTINS_*[] symbols so builtins.c
 * dispatch code links, but every table is empty.
 */

#include "jky_builtins.h"

const Builtin BUILTINS_PURE[]    = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_SYSTEM[]  = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_FS[]      = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_DATA[]    = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_CRYPTO[]  = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_KERNEL[]  = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_PROCESS[] = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_MEMORY[]  = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_NET[]     = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_CRED[]    = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_ANTI[]    = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_UTIL[]    = { { NULL, 0, 0, NULL } };
