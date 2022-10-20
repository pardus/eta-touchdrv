####################################################################

MODULE = OtdDrv

GCC = gcc

INCLUDE = -I include

C_FLAGS = -Wall -O -pipe $(INCLUDE)

####################################################################

ifneq ($(KERNELRELEASE),)
	obj-m := $(MODULE).o
else
	KERNELDIR := /lib/modules/$(shell uname -r)/build
	KERNELRELEASE=$(shell uname -r)
	PWD := $(shell pwd)
all:
	[ $(shell echo $(KERNELRELEASE) | cut -f1 -d".") -ge 5 ] && $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	[ $(shell echo $(KERNELRELEASE) | cut -f1 -d".") -le 4 ] && $(MAKE) -C $(KERNELDIR) SUBDIR=$(PWD) modules

new rebuild:	clean all

clean:
	rm -f *.o *.mod.o *.ko *.mod.c *.cmd *.*~ *~ Module.* modules.*
endif
