MODULE := OpticalDrv2

ccflags-y += -O2
ccflags-y += -I$(src)/../../include

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODULE).o
else
	KVER ?= $(shell uname -r)
	KDIR ?= /lib/modules/$(KVER)/build
	PWD := $(CURDIR)

.PHONY: all clean new rebuild

all:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KBUILD) clean

new rebuild: clean all
endif
