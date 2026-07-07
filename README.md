# Q-Vela

Q-Vela is a QEMU-based RISC-V virtual emulation platform for validating system software features defined in the [Vela](https://github.com/riscv-vela/vela) project. It extends upstream QEMU with emulation support for three key subsystems:

- **[Gemmini](docs/gemmini.md)** — AI matrix accelerator (RoCC-based, weight-stationary dataflow)
- **[TEE](docs/tee.md)** — Trusted Execution Environment via WorldGuard memory protection and Keystone enclave framework
- **[RAS](docs/ras.md)** — Reliability, Availability, and Serviceability via RISC-V Error Record Interface (RERI)

The platform is used for function-level testing of RISC-V system software before validation on the FPGA-based hardware platform (F-Vela).

---

## Repository Structure

```
q-vela/
├── qemu/                      # Modified QEMU source (Gemmini + RERI + WorldGuard)
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
│   ├── gemmini.md
│   ├── tee.md
│   └── ras.md
│
└── examples/                  # Example applications (attestation, ROS, hello)
```

---

## Features by Subsystem

| Subsystem | QEMU Implementation | Firmware / Kernel |
|-----------|---------------------|-------------------|
| Gemmini | `qemu/hw/misc/gemmini/` | — |
| RAS (RERI) | `qemu/hw/riscv/riscv_reri_*` | `patch/*_reri.patch` |
| TEE (WorldGuard) | `qemu/hw/misc/riscv_worldguard.c` | `patch/opensbi_tee.patch` |
| TEE (Keystone) | — | `sm/`, `runtime/`, `sdk/`, `linux-keystone-driver/` |
