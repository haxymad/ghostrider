# Top-level Makefile for jockey_agent
.PHONY: all agent vm kmod clean

all: agent

agent:
	$(MAKE) -C linux agent

vm:
	$(MAKE) -C linux vm

kmod:
	$(MAKE) -C linux/kmod

clean:
	$(MAKE) -C linux clean
	$(MAKE) -C linux/kmod clean
