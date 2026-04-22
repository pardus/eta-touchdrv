MODULE := OtdDrv

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODULE).o
else
	KVER ?= $(shell uname -r)
	KDIR ?= /lib/modules/$(KVER)/build
	PWD := $(CURDIR)

.PHONY: all clean new rebuild

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

new rebuild: clean all
endif
