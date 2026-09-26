MODULE = OpticalDrv

ccflags-y += -I$(src)/include


C_FLAGS = -Wall

ifneq ($(KERNELRELEASE),)
obj-m := $(MODULE).o
else
KVER ?= $(shell uname -r)
KERNELDIR ?= /lib/modules/$(KVER)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

new rebuild: clean all

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
endif
