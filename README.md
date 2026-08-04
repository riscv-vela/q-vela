# Q-Vela

Q-Vela is a QEMU-based RISC-V virtual emulation platform for validating system software features defined in the [Vela](https://github.com/riscv-vela/vela) project. It extends upstream QEMU with emulation support for three key subsystems:

- **[VelaNPU](qemu/hw/misc/gemmini/README.md)** — Systolic array-based NPU (matrix-matrix multiplication accelerator) with ternary GEMM support
- **[Custom Vector ISA](docs/isa.md)** — Custom vector instructions for RoPE, tokenizer acceleration, and PID control
- **[TEE](docs/tee.md)** — Trusted Execution Environment via WorldGuard memory protection and Keystone enclave framework
- **[RAS](docs/ras.md)** — Reliability, Availability, and Serviceability via RISC-V Error Record Interface (RERI)

The platform is used for function-level testing of RISC-V system software before validation on the FPGA-based hardware platform (i.e., [i-vela](https://github.com/riscv-vela/i-vela), [f-vela](https://github.com/riscv-vela/f-vela)). In addition, _VelaNPU_ and _Custom Vector ISA_ replicate the functional behavior of the [f-vela](https://github.com/riscv-vela/f-vela)'s NPU/VPU within the QEMU environment, providing equivalent emulation.

---

## Repository Structure

```
q-vela/
├── qemu/                      # Modified QEMU source (VelaNPU + RERI + WorldGuard)
│
├── patch/                     # Patches for firmware and kernel components
│   ├── opensbi_reri.patch     # OpenSBI: RERI support
│   ├── opensbi_tee.patch      # OpenSBI: TEE/WorldGuard support
│   ├── edk2_reri.patch        # EDK2 UEFI: RERI support
│   ├── kernel_reri.patch      # Linux kernel: RERI support
│   └── *.sh                   # Patch application scripts
│
├── buildroot/                 # Submodule — lightweight root filesystem builder
├── edk2/                      # Submodule — UEFI firmware
├── linux/                     # Submodule — Linux kernel
├── opensbi/                   # Submodule — RISC-V SBI firmware (M-mode bootloader)
│
├── reri register generator/   # RERI register specification tool (Excel-based)
│
│   # Keystone TEE stack
├── linux-keystone-driver/     # Linux kernel driver for Keystone host interface
├── runtime/                   # Enclave-side runtime (runs inside the enclave)
├── sdk/                       # Host-side SDK for enclave application development
├── sm/                        # Security Monitor in Rust (runs in M-mode)
│
├── docs/                      # Per-subsystem documentation
│   ├── isa.md
│   ├── tee.md
│   └── ras.md
│
└── examples/                  # Example applications (attestation, ROS, hello)
```

---

## Features by Subsystem

| Subsystem | QEMU Implementation | Firmware / Kernel |
|-----------|---------------------|-------------------|
| VelaNPU | `qemu/hw/misc/gemmini/` | — |
| Custom Vector ISA | `qemu/target/riscv` | — |
| RAS (RERI) | `qemu/hw/riscv/riscv_reri_*` | `patch/*_reri.patch` |
| TEE (WorldGuard) | `qemu/hw/misc/riscv_worldguard.c` | `patch/opensbi_tee.patch` |
| TEE (Keystone) | — | `sm/`, `runtime/`, `sdk/`, `linux-keystone-driver/` |

### Getting Started

```bash
git clone https://github.com/riscv-vela/q-vela.git
cd q-vela && ./configure --target-list=riscv64-softmmu && make -j$(nproc)
```

