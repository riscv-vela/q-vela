# RISC-V Ras-ERI emulation in QEMU
## How to build
- git clone --recursive https://github.com/riscv-vela/q-vela-ras 
- First, Go to [patch/README](patch/README.md)
- Build dependency
```shell
apt install -y isal libslirp0 file unzip rsync pip python3-sphinx python3-sphinx-rtd-theme
ninja-build pkg-config libglib2.0-dev gcc-riscv64-linux-gnu vim build-essential bc
libncurses5-dev bin86 libssl-dev bison flex libelf-dev
```
### QEMU build
```shell
./configure --target-list=riscv64-softmmu --enable-slirp
make -j $(nproc)
make install -j $(nproc)
```
## Quick Start
If you build dependency, you can find
- /root/Build/RiscVVirtQemu/REALEASE_GCC5/FV/RISCV_VIRT_CODE.fd
- /root/Build/RiscVVirtQemu/REALEASE_GCC5/FV/RISCV_VIRT_VARS.fd
- /root/opensbi/build/platform/generic/firmware
- /root/buildroot/output/images/rootfs.cpio
- /root/build/kernel/arch/riscv/boot/Image

--- 
```shell
/path/to/qemu-system-riscv64 \
-s -accel tcg -m 4096 -smp 2 \
-serial mon:stdio \
-d guest_errors -D ./qemu.log \
-bios /root/opensbi/build/platform/generic/firmware/fw_dynamic.bin \
-device virtio-gpu-pci -full-screen \
-device qemu-xhci \
-device usb-kbd \
-netdev user,id=net0,host=10.0.2.10,hostfwd=tcp::221-:22 \
-device virtio-net-device,netdev=net0 \
-blockdev node-name=pflash0,driver=file,read-only=on,filename=/root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd \
-blockdev node-name=pflash1,driver=file,filename=/root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd \
-M virt,pflash0=pflash0,pflash1=pflash1,rpmi=true,reri=true,aia=aplic-imsic \
-kernel q-vela-ras/linux/arch/riscv/boot/Image \
-initrd /root/buildroot/output/images/rootfs.cpio \
-append "root=/dev/ram rw console=ttyS0 earlycon=uart8250,mmio,0x10000000"
```
if you successfuly boot,
```shell
ssh root@[host_ip] -p 221
```


```shell
// RERI config register programming
devmem 0x4010040 32 0x2a1

// TLB Error
devmem 0x4010048 32 0x9001404
devmem 0x4010044 8 1

// Internal Hart Error
devmem 0x4010048 32 0xC001702
devmem 0x4010044 8 1
```
