CC      ?= gcc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Iinclude -D_GNU_SOURCE
LDFLAGS := -lm

SRC := \
    src/value.c \
    src/loader.c \
    src/vm.c \
    src/builtins.c \
    src/builtins/pure.c \
    src/builtins/system.c \
    src/builtins/fs.c \
    src/builtins/data.c \
    src/builtins/crypto.c \
    src/builtins/kernel.c \
    src/builtins/process.c \
    src/builtins/net.c \
    src/builtins/process_extra.c \
    src/builtins/memory.c \
    src/builtins/fs_extra.c \
    src/builtins/cred.c \
    src/builtins/kernel_extra.c \
    src/builtins/net_extra.c \
    src/builtins/registry.c \
    src/builtins/anti.c \
    src/builtins/util.c \
    src/builtins/ptrace.c \
    src/builtins/tracing.c \
    src/builtins/kprobe.c \
    src/builtins/uprobe.c \
    src/platform/linux/platform.c \
    src/platform/linux/kernel_bridge.c

OBJ := $(SRC:.c=.o)

.PHONY: all clean

all: jockey-vm jockey-agent

jockey-vm: $(OBJ) src/main.o
	$(CC) -o $@ $^ $(LDFLAGS)

jockey-agent: $(OBJ) src/agent.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) src/main.o src/agent.o jockey-vm jockey-agent
