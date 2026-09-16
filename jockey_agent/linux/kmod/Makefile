obj-m := rootkit.o
rootkit-objs := main.o hooks.o cred_escalate.o ioctl_handler.o trace.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
