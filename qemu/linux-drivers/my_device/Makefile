obj-m += mydevice_driver.o
PWD := $(shell pwd)
#LINUXSRC=
default:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- M=$(PWD)

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd
