# Building prerequisite
QEMU RERI require 4 dependency
This markdown helps to build dependency and apply patching the dependency
## prerequisite
- buildroot
- edk2
- opensbi
- linux


## How to build buildroot
```shell
cd buildroot
make qemu_riscv64_virt_defconfig
make menuconfig
### Need To Enable
# Target packages -> Networking Application -> iproute2 / openssh
# Target packages -> Package managers -> opkg
# Target packages -> Text editors and viewers -> vim
# Filesystem images -> ext2/3/4 variant - ext4 / exact size - 1g
# Filesystem images -> cpio the root filesystem (for use initramfs)
###
### Need To Disable
# Kernel -> Linux Kernel
###
make -j $(nproc)
vi output/target/etc/ssh/sshd_config
### modify
# PermitRootLogin yes
# PasswordAuthentization yes
# PermitEmptyPasswords yes
###
make -j $(nproc)
```

## How to build edk2
- Run edk2_patch.sh
```shell
# ./patch/edk2_patch.sh
```
- After
```shell
# in q-vela-ras directory
export WORKSPACE=`pwd`
export GCC5_RISCV64_PREFIX=riscv64-linux-gnu-
export PACKAGES_PATH=$WORKSPACE/edk2
export EDK_TOOLS_PATH=$WORKSPACE/edk2/BaseTools
source edk2/edksetup.sh --reconfig
make -C edk2/BaseTools
source edk2/edksetup.sh BaseTools
build -a RISCV64 --buildtarget RELEASE -p OvmfPkg/RiscVVirt/RiscVVirtQemu.dsc -t GCC5
truncate -s 32M $WORKSPACE/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd
truncate -s 32M $WORKSPACE/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd
```
## How to build opensbi
- Run opensbi_patch.sh
```shell
# ./patch/opensbi_patch.sh
```
- After
```shell
# in sm directory
cargo build

# in opensbi directory
make CROSS_COMPILE=riscv64-linux-gnu- PLATFORM_RISCV_XLEN=64 PLATFORM=generic PLATFORM_RISCV_ABI="lp64d"
```
## How to build linux kernel
```shell
# ./patch/kernel_patch.sh
```
```shell
# in kernel directory
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- mrproper
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- menuconfig
# Need to check (default, but need to check)
# CONFIG_RISCV_SSE=y
# CONFIG_HAVE_ACPI_APEI=y
# CONFIG_ACPI_APEI=y
# CONFIG_ACPI_APEI_GHES=y
# CONFIG_ACPI_APEI_SSE=y
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j $(nproc)
```
