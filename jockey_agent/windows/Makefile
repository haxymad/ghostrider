CC = cl
CFLAGS = /nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601
CFLAGS += /Iinclude /Iplatform /Ikmod
LDFLAGS = /nologo kernel32.lib advapi32.lib user32.lib ws2_32.lib iphlpapi.lib secur32.lib rpcrt4.lib

BUILDDIR = obj
TARGET = jockey.exe

SRCS = src\main.c src\vm.c src\loader.c src\agent.c src\agent_c2.c src\builtins.c src\value.c src\kernel_bridge.c
SRCS += src\builtins\anti.c src\builtins\system.c src\builtins\fs.c src\builtins\process.c
SRCS += src\builtins\memory.c src\builtins\net.c src\builtins\cred.c src\builtins\kernel.c
SRCS += src\builtins\util.c src\builtins\pure.c src\builtins\crypto.c src\builtins\data.c
SRCS += src\platform\windows\platform.c

OBJS = $(patsubst %.c,$(BUILDDIR)\%.obj,$(notdir $(SRCS)))

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	if not exist $(BUILDDIR) mkdir $(BUILDDIR)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) /Fe$(TARGET) $^

$(BUILDDIR)\%.obj: src\%.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\%.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\platform\%.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

clean:
	del /Q $(TARGET)
	del /Q /S $(BUILDDIR)\*.obj
	rmdir /Q $(BUILDDIR)

.PHONY: all clean
