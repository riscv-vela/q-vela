# Q-Vela

Q-Vela is a QEMU-based RISC-V virtual emulation platform for validating system software features defined in the [riscv-vela](https://github.com/riscv-vela/) project. 

It extends upstream QEMU with emulation support for the following four key features:

- **[VelaNPU](qemu/hw/misc/gemmini/README.md)** — A software-emulated accelerator modeling Chipyard Gemmini's matrix-matrix multiplication, with additional support for the ternary GEMM implemented in [f-vela](https://github.com/riscv-vela/f-vela)
- **[Custom Vector ISA](docs/isa.md)** — Emulation of the custom vector instructions implemented in [f-vela](https://github.com/riscv-vela/f-vela), covering RoPE, tokenizer acceleration, and PID control
- **[TEE](docs/tee.md)** — Trusted Execution Environment via WorldGuard memory protection and Keystone enclave framework
- **[RAS](docs/ras.md)** — Reliability, Availability, and Serviceability via RISC-V Error Record Interface (RERI)

The platform is used for function-level testing of RISC-V system software before validation on the FPGA-based hardware platform (i.e., [i-vela](https://github.com/riscv-vela/i-vela), [f-vela](https://github.com/riscv-vela/f-vela)). In addition, _VelaNPU_ and _Custom Vector ISA_ replicate the functional behavior of the [f-vela](https://github.com/riscv-vela/f-vela)'s NPU/VPU within the QEMU environment, providing equivalent emulation.

---

## Repository Structure

```
q-vela/
├── qemu/       # Modified QEMU source (VelaNPU + Custom Vector ISA + RERI + WorldGuard)
└── docs/       # Per-subsystem documentation — isa.md, tee.md, ras.md
```

Firmware, kernel, applications and the guest image are maintained in
[vela-fws](https://github.com/riscv-vela/vela-fws),
[vela-linux](https://github.com/riscv-vela/vela-linux),
[vela-apps](https://github.com/riscv-vela/vela-apps) and
[vela](https://github.com/riscv-vela/vela). Build them from those repositories.

---

## Features by Subsystem

| Subsystem | QEMU implementation |
|-----------|---------------------|
| VelaNPU | `qemu/hw/misc/gemmini/` |
| Custom Vector ISA | `qemu/target/riscv/` |
| RAS (RERI) | `qemu/hw/riscv/riscv_reri_*` |
| TEE (WorldGuard) | `qemu/hw/misc/riscv_worldguard.c` |

---

## Getting Started

### Prerequisites

```bash
sudo apt install git build-essential ninja-build pkg-config \
    libglib2.0-dev libpixman-1-dev libslirp-dev python3 python3-venv flex bison
```

### Clone

```bash
git clone https://github.com/riscv-vela/q-vela.git
cd q-vela
```

### Build QEMU

```bash
cd qemu
./configure --target-list=riscv64-softmmu --enable-slirp
make -j$(nproc)
cd ..
```

> `--enable-slirp` is required. The Launch command below uses `-netdev user`,
> which is not compiled in without it — QEMU then fails at startup with
> `network backend 'user' is not compiled into this binary`.

### Build Output

| File | Description |
|------|-------------|
| `qemu/build/qemu-system-riscv64` | RISC-V 64-bit system emulator with VelaNPU, Custom Vector ISA, RERI, and WorldGuard support |
| `qemu/build/qemu-img` | Disk image creation/conversion utility |

Verify the build:

```bash
./qemu/build/qemu-system-riscv64 --version                     # QEMU version
./qemu/build/qemu-system-riscv64 -device help | grep gemmini   # VelaNPU device
./qemu/build/qemu-system-riscv64 -M virt,help | grep reri      # RERI machine option
./qemu/build/qemu-system-riscv64 -netdev help | grep -x user   # slirp enabled
```

### Launch

Booting a guest additionally needs firmware, a kernel and a root filesystem
image, built from vela-fws, vela-linux and vela. The paths below assume those
repositories are cloned next to `q-vela/`.

Save the following as `run.sh` in the repository root and execute it:

```bash
#!/bin/bash
./qemu/build/qemu-system-riscv64 \
    -machine virt,wg=on -nographic -m 16384 -smp 4 \
    -bios ../vela-fws/opensbi/build/platform/generic/firmware/fw_dynamic.bin \
    -kernel ../vela-linux/linux/arch/riscv/boot/Image \
    -append "root=/dev/vda1 rw console=ttyS0 earlycon=sbi" \
    -cpu rv64,v=true,vlen=512,vext_spec=v1.0,zba=true,zbb=true,zbs=true,zbc=true,zbkb=true,zcb=true,zicond=true \
    -accel tcg,thread=multi \
    -semihosting-config enable=on,target=native,userspace=on \
    -netdev user,id=eth0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=eth0 \
    -device virtio-rng-pci \
    -drive file=../vela/ubuntu-noble-riscv64.img,format=raw,if=virtio \
    -device gemmini,id=gemmini0 \
    -device gemmini,id=gemmini1 \
    -device gemmini,id=gemmini2 \
    -device gemmini,id=gemmini3 \
    -D qemu.log
```

- `-device gemmini,id=gemmini{N}` attaches N VelaNPU instances; adjust the count as needed (IDs must be `gemmini0`, `gemmini1`, ...).
- `-accel tcg,thread=multi` is recommended when attaching multiple VelaNPU devices, so that each device runs on its own worker thread.
- `-semihosting-config` is required by the TEE firmware.
- Once the guest has booted, log in as `vela` (no password), or connect via SSH: `ssh -p 2222 vela@localhost`
- Execution logs (VelaNPU command traces, thread IDs) are written to `qemu.log`.
- Quit the emulator with **Ctrl-A X**.

> **This command reports no RERI errors.** RAS travels over ACPI (HEST/GHES) and
> therefore needs the EDK2 UEFI firmware and `reri=true` — see
> [docs/ras.md](docs/ras.md) §3 for the RAS launch command.
