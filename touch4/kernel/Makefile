MODULE := OtdDrv

ccflags-y += -O3

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODULE).o
else
	KVER ?= $(shell uname -r)
	KDIR ?= /lib/modules/$(KVER)/build
	PWD := $(CURDIR)
	KBUILD ?= LLVM=1

.PHONY: all clean new rebuild

all:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD) clean

new rebuild: clean all
endif
