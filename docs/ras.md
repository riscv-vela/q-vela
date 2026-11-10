# RISC-V RAS-ERI Emulation in QEMU

The **RAS (Reliability, Availability, Serviceability)** part of q-vela.

It emulates the RISC-V **RERI (RAS Error-record Register Interface)** on the QEMU `virt` machine and
delivers injected/detected hardware errors to Linux through the following path:

```
RERI MMIO (QEMU)  →  OpenSBI (+ sm)  →  SBI SSE  →  GHES / ACPI APEI  →  Linux "[Hardware Error]" report
```

## 1. Where each piece lives

q-vela provides the emulator. The rest of the chain is built from the other
repositories, cloned next to `q-vela/`.

| Piece | Repository | Role |
|---|---|---|
| `qemu/` | **q-vela** (this) | Provides the `riscv.reri.hart` MMIO device, enabled by `-M virt,reri=true` (one device per hart) |
| OpenSBI + Security Monitor | [vela-fws](https://github.com/riscv-vela/vela-fws) | M-mode firmware. Receives RERI errors and reports them to the OS via SBI SSE |
| EDK2 | [vela-fws](https://github.com/riscv-vela/vela-fws) | UEFI firmware, produces the pflash images `RISCV_VIRT_CODE.fd` / `RISCV_VIRT_VARS.fd`, and the HEST/EINJ ACPI tables |
| buildroot | [vela-fws](https://github.com/riscv-vela/vela-fws) | Guest userland → `rootfs.cpio` (initramfs; busybox provides `devmem`) |
| Linux | [vela-linux](https://github.com/riscv-vela/vela-linux) | Kernel with the RERI / APEI path enabled (`RISCV_SBI_SSE`, `ACPI_APEI_GHES`, ...) |
| Injection and collection tools | [vela-apps](https://github.com/riscv-vela/vela-apps) `ras/` | rasdaemon, injection scripts, the RERI register generator |

## 2. Build

### 2.1 Dependencies (QEMU)

```shell
sudo apt install git build-essential ninja-build pkg-config \
    libglib2.0-dev libpixman-1-dev libslirp-dev python3 python3-venv flex bison
```

The firmware, kernel and root filesystem have their own prerequisites — see the
vela-fws and vela-linux READMEs.

### 2.2 Build order

```
vela-fws:   sm → opensbi → edk2 → buildroot
vela-linux: linux
q-vela:     qemu
```

Within vela-fws, **`sm` must precede `opensbi`**: OpenSBI links `libvela.a`.
QEMU can be built at any point.

### 2.3 QEMU

```shell
cd qemu
./configure --target-list=riscv64-softmmu --enable-slirp
make -j $(nproc)
```

Check that the RERI machine option is present:

```shell
build/qemu-system-riscv64 -M virt,help | grep reri     # → reri=<bool>
```

## 3. Quick Start

Artifacts, with the repositories cloned side by side:

- `../vela-fws/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd`
- `../vela-fws/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd`
- `../vela-fws/opensbi/build/platform/generic/firmware/fw_dynamic.bin`
- `../vela-fws/buildroot/output/images/rootfs.cpio`
- `../vela-linux/linux/arch/riscv/boot/Image`

Run from `q-vela/`:

```shell
cp ../vela-fws/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd ./VARS.fd

qemu/build/qemu-system-riscv64 \
-s -accel tcg -m 4096 -smp 2 \
-serial mon:stdio \
-d guest_errors -D ./qemu.log \
-bios ../vela-fws/opensbi/build/platform/generic/firmware/fw_dynamic.bin \
-device virtio-gpu-pci -full-screen \
-device qemu-xhci \
-device usb-kbd \
-netdev user,id=net0,host=10.0.2.10,hostfwd=tcp::2221-:22 \
-device virtio-net-device,netdev=net0 \
-blockdev node-name=pflash0,driver=file,read-only=on,filename=../vela-fws/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd \
-blockdev node-name=pflash1,driver=file,filename=./VARS.fd \
-M virt,wg=on,pflash0=pflash0,pflash1=pflash1,rpmi=true,reri=true,aia=aplic-imsic \
-kernel ../vela-linux/linux/arch/riscv/boot/Image \
-initrd ../vela-fws/buildroot/output/images/rootfs.cpio \
-append "root=/dev/ram rw console=ttyS0 earlycon=uart8250,mmio,0x10000000" \
-semihosting-config enable=on,userspace=on
```

Four options decide whether RERI reporting exists at all, and dropping any one
of them silently produces a guest with no RAS:

| Option | Why |
|---|---|
| `reri=true` | Creates the `riscv.reri.hart` MMIO device. Without it there is nothing to write to. |
| `pflash0` / `pflash1` | RAS reporting is ACPI-based. HEST and EINJ come from EDK2; a device-tree boot has neither. |
| `fw_dynamic.bin` | `fw_jump.bin` does not hand over to EDK2. |
| `wg=on` | OpenSBI links the Vela security monitor, which touches the WorldGuard CSR `mlwid` (`0x390`) during init. Without it the guest dies in M-mode before EDK2 runs: `Failed to access CSR 0x390 from M-mode`, `mcause=0x2 mtval=0x39051073`. |

Notes:

- **`pflash1` is written by QEMU.** The command above points it at a copy so the
  vela-fws build artifact is left alone.
- Boot chain: OpenSBI → edk2 (UEFI) → Linux → busybox init → login prompt on the serial console.
  Log in as **`root` with no password**.
- Host port **2221** is used for SSH forwarding. Ports below 1024 (e.g. 221) can only be bound by root.
- The `rootfs.cpio` initramfs does **not** include an SSH daemon; use the serial console.
- Exit QEMU with **`Ctrl-A X`**; switch to the QEMU monitor with `Ctrl-A C`
  (`info mtree` shows the `riscv.reri.hart` regions).
- This setup boots via **ACPI** — there is no device tree (`/sys/firmware/acpi/tables`).
- A healthy boot shows the RAS chain coming up in `dmesg`:
  `sse: SBI SSE extension detected` → `GHES: Registered SSE notification` → `EINJ: Error INJection is initialized.`

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

All three writes are needed: writing the error code alone does nothing, the
write to `+0x44` is what fires the injection.

### Arm the bank before every session, and per hart

`+0x40` resets to `0`, which means error logging is off. An injection into an
unarmed bank is dropped in silence — no error from `devmem`, nothing in `dmesg`,
no trace event. There is nothing to tell you the injection did not happen, so arm
first and read the value back:

```shell
devmem 0x4010040 32 0x2a1
devmem 0x4010040 32            # expect 0x000002A1
```

Two things to remember. The register is per hart, so arming hart 0 does not arm
hart 3 — use `0x4010040 + hart * 0x1000`. And it has to be redone every time the
QEMU process starts; on an ACPI-booted guest a reboot currently cannot be used to
get back to a fresh state anyway (#62).

### hart numbers are not Linux CPU numbers

RERI and GHES report hart ids. Linux numbers its CPUs separately, and the two do
not line up: the boot hart becomes CPU 0 wherever it sits in the hart space, and
which hart boots changes from run to run. On this machine with `-smp 8` we have
seen boot hart 7, 4 and 2 on different boots.

```shell
$ grep -E '^processor|^hart' /proc/cpuinfo | paste - -
processor : 0    hart : 7       # boot hart took CPU 0
processor : 1    hart : 0
processor : 2    hart : 1
...
```

So read the mapping at runtime instead of assuming it. Taking the reported hart
number as a CPU number means that acting on a fault — offlining the core, pinning
work away from it — hits the wrong CPU while the faulting hart keeps running.

Then check `dmesg`. Expected report per injection:

| Injected code | dmesg `[Hardware Error]` |
|---|---|
| `0x9001404` (TLB) | `error_type: 0x02`, `TLB error`, `operation: 1, data read`, severity `recoverable` |
| `0xC001702` (hart) | `error_type: 0x08`, `micro-architectural error`, `operation: 2, data write`, severity `corrected` |

For error injection beyond this smoke test — rasdaemon, the `ras:non_standard_event`
tracepoint, the full error-code table and the register generator — see
[vela-apps `ras/`](https://github.com/riscv-vela/vela-apps/tree/main/ras).
