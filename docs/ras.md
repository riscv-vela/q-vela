# RISC-V RAS-ERI Emulation in QEMU

The **RAS (Reliability, Availability, Serviceability)** part of q-vela.

It emulates the RISC-V **RERI (RAS Error-record Register Interface)** on the QEMU `virt` machine and
delivers injected/detected hardware errors to Linux through the following path:

```
RERI MMIO (QEMU)  →  OpenSBI (+ sm)  →  SBI SSE  →  GHES / ACPI APEI  →  Linux "[Hardware Error]" report
```

## 1. Repository layout (RAS-related)

| Path | Role |
|---|---|
| `qemu/` | Custom QEMU. Provides the `riscv.reri.hart` MMIO device, enabled by `-M virt,reri=true` (one device per hart) |
| `opensbi/` | M-mode firmware. Receives RERI errors and reports them to the OS via SBI SSE |
| `sm/` | Rust security monitor. Built as `libvela.a` and linked into OpenSBI by `opensbi_tee.patch` → **must be built before opensbi** |
| `linux/` | Linux kernel with the RERI / APEI path enabled (`RISCV_SBI_SSE`, `ACPI_APEI_GHES`, ...) |
| `edk2/` | UEFI firmware, produces the pflash images `RISCV_VIRT_CODE.fd` / `RISCV_VIRT_VARS.fd` |
| `buildroot/` | Guest userland → `rootfs.cpio` (initramfs; contains busybox, including `devmem`) |
| `patch/` | Patches and apply scripts for the four submodules — **start here**: [patch/README.md](../patch/README.md) |
| `reri register generator/` | RERI register map generator (spreadsheet + notes) |

> `runtime/`, `sdk/`, `examples/`, `linux-keystone-driver/` belong to the Keystone **TEE** work — see the TEE README.
> (`sm/` is shared: the RAS build links it into OpenSBI, so it must be built even for a RAS-only setup.)

## 2. Build

### 2.1 Dependencies

```shell
apt install -y isal libslirp0 file unzip rsync pip python3-sphinx python3-sphinx-rtd-theme \
ninja-build pkg-config libglib2.0-dev gcc-riscv64-linux-gnu vim build-essential bc \
libncurses5-dev bin86 libssl-dev bison flex libelf-dev
```

For **TEE / sm** (Rust):

```shell
apt install -y rustup cargo
```

### 2.2 Build order

Apply the patches first, then build in this order. The order matters: **`sm` must precede `opensbi`**
(OpenSBI links `libvela.a`), and QEMU/edk2 can be built at any point after the patches.

```
patch/  →  buildroot  →  sm  →  opensbi  →  linux  →  qemu  →  edk2
```

Per-component commands (patch scripts, buildroot menuconfig options, kernel CONFIG checks, edk2 environment):
[patch/README.md](../patch/README.md)

### 2.3 QEMU

```shell
cd qemu
./configure --target-list=riscv64-softmmu --enable-slirp
make -j $(nproc)
# make install   # optional, requires root. The Quick Start below runs ./qemu/build/ directly.
```

Check that the RERI machine option is present:

```shell
build/qemu-system-riscv64 -M virt,help | grep reri     # → reri=<bool>
```

## 3. Quick Start

After a successful build the artifacts are (paths relative to the repository root):

- `Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd`
- `Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd`
- `opensbi/build/platform/generic/firmware/fw_dynamic.bin`
- `linux/arch/riscv/boot/Image`
- `buildroot/output/images/rootfs.cpio`

Run from the repository root:

```shell
qemu/build/qemu-system-riscv64 \
-s -accel tcg -m 4096 -smp 2 \
-serial mon:stdio \
-d guest_errors -D ./qemu.log \
-machine virt,wg=on \
-bios ./opensbi/build/platform/generic/firmware/fw_dynamic.bin \
-device virtio-gpu-pci -full-screen \
-device qemu-xhci \
-device usb-kbd \
-netdev user,id=net0,host=10.0.2.10,hostfwd=tcp::2221-:22 \
-device virtio-net-device,netdev=net0 \
-blockdev node-name=pflash0,driver=file,read-only=on,filename=./Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd \
-blockdev node-name=pflash1,driver=file,filename=./Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd \
-M virt,pflash0=pflash0,pflash1=pflash1,rpmi=true,reri=true,aia=aplic-imsic \
-kernel ./linux/arch/riscv/boot/Image \
-initrd ./buildroot/output/images/rootfs.cpio \
-append "root=/dev/ram rw console=ttyS0 earlycon=uart8250,mmio,0x10000000" \
-semihosting-config enable=on,userspace=on
```

Notes:

- Boot chain: OpenSBI → edk2 (UEFI) → Linux → busybox init → login prompt on the serial console.
  Log in as **`root` with no password**.
- Host port **2221** is used for SSH forwarding. Ports below 1024 (e.g. 221) can only be bound by root.
- The `rootfs.cpio` initramfs does **not** include an SSH daemon; use the serial console.
  (For SSH, boot with the full `rootfs.ext4` image instead of the initramfs.)
- Exit QEMU with **`Ctrl-A X`**; switch to the QEMU monitor with `Ctrl-A C`
  (`info mtree` shows the `riscv.reri.hart` regions).
- This setup boots via **ACPI** — there is no device tree (`/sys/firmware/acpi/tables`).

## 4. RERI smoke test

RERI registers are memory-mapped; hart 0 has its 4 KiB region at `0x0401_0000`
(hart 1 at `0x0401_1000`, and so on — one region per hart).
Relevant offsets: `+0x40` config, `+0x44` trigger (8-bit), `+0x48` error code.

Run in the guest shell:

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

Then check `dmesg`. Expected report per injection:

| Injected code | dmesg `[Hardware Error]` |
|---|---|
| `0x9001404` (TLB) | `error_type: 0x02`, `TLB error`, `operation: 1, data read`, severity `recoverable` |
| `0xC001702` (hart) | `error_type: 0x08`, `micro-architectural error`, `operation: 2, data write`, severity `corrected` |

A healthy boot also shows the RAS chain coming up in `dmesg`:
`sse: SBI SSE extension detected` → `GHES: Registered SSE notification` → `EINJ: Error INJection is initialized.`
