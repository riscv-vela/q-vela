# Building prerequisite
QEMU RERI require 4 dependency
This markdown helps to build dependency and apply patching the dependency
## prerequisite
- buildroot
- edk2
- opensbi
- linux

> Run every `patch/*.sh` script **from the repository root** (`q-vela-ras/`), not from inside `patch/`.
> Build order: `patch → buildroot → sm → opensbi → linux` (`sm` must precede `opensbi`, see below).

## How to build buildroot
```shell
cd buildroot
make qemu_riscv64_virt_defconfig
make menuconfig
### Need To Enable
# Target packages -> Networking Application -> iproute2 / openssh
# Target packages -> Package managers -> opkg
# Target packages -> Text editors and viewers -> nano
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
# PasswordAuthentication yes
# PermitEmptyPasswords yes
###
make -j $(nproc)
```
- The second `make` only repackages the root filesystem images, so it finishes in a few minutes.
- The `sshd_config` settings apply to the **ext4 image** (`output/images/rootfs.ext4`).
  The initramfs (`rootfs.cpio`) is a minimal dracut image that contains busybox (including `devmem`)
  but **no SSH daemon**, so a Quick Start boot with `-initrd rootfs.cpio` is reached through the serial console.

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
- Run these in **one shell**: `edksetup.sh` exports the environment and puts `build` on `PATH`.
- `truncate` is required because QEMU pflash needs the backing file size to match the flash size exactly (32 MiB).
- Output: `Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_{CODE,VARS}.fd` (created under the repository root, not under `edk2/`).

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
make CROSS_COMPILE=riscv64-linux-gnu- PLATFORM_RISCV_XLEN=64 PLATFORM=generic PLATFORM_RISCV_ABI=lp64d
```
- **`sm` must be built first**: `opensbi_tee.patch` makes `platform/generic/objects.mk` link
  `sm/target/riscv64gc-unknown-linux-gnu/debug/libvela.a`, so an OpenSBI build without it fails to link.
- `rust-toolchain.toml` in `sm/` selects the required nightly toolchain and the `riscv64gc` target automatically.
- Output: `opensbi/build/platform/generic/firmware/fw_dynamic.bin`

## How to build linux kernel
```shell
# ./patch/kernel_patch.sh
```
- While patching the kernel you will see `No file to patch. Skipping patch.` for a few `.git/...` paths,
  and `.orig` backup files (e.g. `MAINTAINERS.orig`) may be created. Both are harmless side effects
  of how the patch was generated; source hunks apply cleanly.

```shell
# in linux directory
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- mrproper
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- menuconfig
# Need to check (default, but need to check)
# CONFIG_RISCV_SBI_SSE=y
# CONFIG_HAVE_ACPI_APEI=y
# CONFIG_ACPI_APEI=y
# CONFIG_ACPI_APEI_GHES=y
# CONFIG_ACPI_APEI_SSE=y
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j $(nproc)
```
- The patched `arch/riscv/configs/defconfig` already enables the five options above, so `menuconfig` is
  only a confirmation step. You can check them without opening the UI:
  ```shell
  grep -E '^CONFIG_(RISCV_SBI_SSE|HAVE_ACPI_APEI|ACPI_APEI|ACPI_APEI_GHES|ACPI_APEI_SSE)=' .config
  ```
- Output: `linux/arch/riscv/boot/Image`
