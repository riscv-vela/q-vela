DRIVER = keystone-driver.ko
ifneq ($(KERNELRELEASE),)
	keystone-driver-y := \
		keystone.o \
		keystone-page.o \
		keystone-ioctl.o \
		keystone-enclave.o \
	  keystone-sbi.o
	obj-m += keystone-driver.o

PWD := $(shell pwd)
#LINUXSRC := /path/to/linux
#KEYSTONE_SDK_DIR := /path/to/sdk

ifeq ($(KEYSTONE_SDK_DIR),)
    $(error KEYSTONE_SDK_DIR not defined)
endif

	ccflags-y := -I$(KEYSTONE_SDK_DIR)/include/shared
else

default:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- M=$(PWD)

endif

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd
