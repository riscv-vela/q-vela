DRIVER = keystone-driver.ko
ifneq ($(KERNELRELEASE),)
	keystone-driver-y := \
		keystone.o \
		keystone-page.o \
		keystone-ioctl.o \
		keystone-enclave.o \
	  keystone-sbi.o
	obj-m += keystone-driver.o

ifeq ($(KEYSTONE_SDK_DIR),)
    $(error KEYSTONE_SDK_DIR not defined)
endif

	ccflags-y := -I$(KEYSTONE_SDK_DIR)/include/shared
else

PWD := $(shell pwd)
#LINUXSRC := /data/hykang/RVSS/q-vela-ras/linux
LINUXSRC := /data/hykang/RVSS/riscv-linux
#LINUXSRC := /data/hykang/RVSS/Vyond/chipyard-1.11.0/software/firemarshal/boards/default/linux

default:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- LOCALVERSION=-gea5f6ad9ad96-dirty LOCALVERSION_AUTO=n M=$(PWD)
#	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- M=$(PWD)

endif

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd
