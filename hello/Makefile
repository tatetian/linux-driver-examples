ifneq ($(KERNELRELEASE),)
	obj-m	:= hello.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)
#KERNELDIR ?= /home/tian/Workspace/linux-kernel/linux-3.2.0

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
