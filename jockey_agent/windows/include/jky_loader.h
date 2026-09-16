#ifndef JKY_LOADER_H
#define JKY_LOADER_H

#include "jockey_vm.h"
#include <stddef.h>
#include <stdint.h>

int vm_load(VM *vm, const uint8_t *data, size_t len);

#endif