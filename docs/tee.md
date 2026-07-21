# TEE — Trusted Execution Environment

## Overview

Q-Vela's TEE provides **enclaves** — isolated execution environments whose code
and data are protected from the untrusted OS. It is built on the
[Keystone](https://keystone-enclave.org/) enclave framework, with two Q-Vela
changes:

- the **Security Monitor is rewritten in Rust**, and
- **isolation is enforced with RISC-V WorldGuard** instead of PMP.

The stack spans all three privilege levels:

| Level | Component | Role |
|-------|-----------|------|
| **M** | Security Monitor (`sm/`) | Trusted core: enclave lifecycle + WorldGuard isolation |
| **M** | Firmware — OpenSBI + TEE patch (`patch/opensbi_tee.patch`) | Boots and hosts the SM, routes enclave calls to it |
| **S** | Keystone driver (`linux-keystone-driver/`) | Linux host-side interface to create/run enclaves |
| **S + U** | Application software (`sdk/`, `runtime/`, `examples/`) | SDK libraries (host·app·edge·verifier) + in-enclave runtime (S) + enclave/host apps (U), built together |

## WorldGuard (QEMU)

RISC-V **WorldGuard** splits the system into multiple *worlds*, each identified
by a **World ID (WID)**. Two pieces cooperate on every access:

| Piece | Role |
|-------|------|
| **wgMarker** | tags each bus transaction with the WID of the world that issued it |
| **wgChecker** | guards a region and, per WID, allows or blocks the read/write |

```
CPU / device  ──[ wgMarker: attach WID ]──►  [ wgChecker: allow / block per (region, WID) ]
```

> The Security Monitor runs as the *trusted world* and programs the checkers so
> enclave regions are reachable only from the trusted world — invisible to the
> untrusted OS.

The device models come from SiFive's upstream WorldGuard QEMU patchset; Q-Vela
wires them into the `virt` machine and adds the CPU-side marker extension.

### Components

**① Global config — `riscv.worldguard`** · `qemu/hw/misc/riscv_worldguard.c`

- One machine-wide object shared by all CPUs and checkers.
- `nworlds` — number of worlds (currently **32**).
- `trustedwid` — only this WID may reprogram the checkers
  (`could_access_wgblocks()`); currently **31**.
- `wid_to_mem_attrs()` / `mem_attrs_to_wid()` — move the WID between CPU and bus.

**② wgMarker — CPU extension** · `qemu/target/riscv/{cpu.c,csr.c,cpu_helper.c}`

- Added by the `smwg` (M-level) and `sswg` (S/U-level) extensions.
- CSRs: `mlwid` (0x390), `mwid` (0x391), `slwid` (0x190), `mwiddeleg` (0x748).
- `riscv_cpu_wg_get_wid()` picks the WID by privilege mode (M → `mwid`,
  S/U → `mlwid`/`slwid`) and stamps it onto each transaction.

**③ wgChecker — `riscv.wgchecker`** · `qemu/hw/misc/riscv_wgchecker.c`

- An IOMMU region in front of a guarded region: routes each access to the real
  region or to a "blocked" sink.
- **Slots** hold the rules — each has an address, a config word (`cfg.A` match
  mode: `OFF`/`TOR`/`NA4`/`NAPOT`, + `LOCK` bit), and a permission word with
  2 bits (R, W) **per world**.
- `wgc_check_access()` — allow if a matching slot grants R/W for the WID, else block.
- A blocked access latches `ERRCAUSE`/`ERRADDR` and can raise an IRQ or bus error.

### Machine integration — `hw/riscv/virt.c`

- Enabled with **`-machine virt,wg=on`** (TCG only).
- `virt_create_worldguard()` creates the config, enables the marker on every
  hart, and instantiates the wgCheckers.

## Firmware + Security Monitor (M-mode)

The **Security Monitor (SM)** is the trusted core of the TEE. It is compiled as
a Rust static library (`libvela`) and **linked into OpenSBI**, which boots it and
forwards every enclave request to it. Together they occupy M-mode:

```
S-mode (host app / enclave)  ──ecall──►  OpenSBI (TEE patch)  ──►  Security Monitor (libvela)
```

### ① Firmware (OpenSBI)

The `patch/opensbi_tee.patch` adds the glue that hosts the SM and exposes it to
S-mode:

- **Links the SM in** — `platform-ldflags-y += -lvela`; `vyond_monitor_init()`
  (called from platform init) runs the SM's `sm_init()` on boot, then registers
  a custom SBI extension.
- **Enclave call interface** — a vendor SBI extension
  `0x08424B45` ("BKE") whose handler (`vyond.c`) dispatches by function ID to the
  SM's `sbi_sm_*` entry points:

  | FID range | Caller | Calls |
  |-----------|--------|-------|
  | 2000–2999 | host | create / destroy / enter / resume enclave |
  | 3000–3999 | enclave | attest, random, sealing-key, stop, exit |
  | 4000–4999 | host | shared-memory region create / map / unmap / share |

- **Enclave trap path** — `trap_vector_enclave` (`fw_base.S`) +
  `sbi_trap_handler_keystone_enclave` (`sbi_trap.c`) route traps taken while an
  enclave runs to the SM instead of the normal OS path.

### ② Security Monitor

The SM lives in `sm/`. `sm_init()` initializes the isolator on cold boot, then
each enclave call enters through the `sbi_sm_*` C-ABI functions.

| Module | Responsibility |
|--------|----------------|
| `api.rs` | `sbi_sm_*` entry points (the ABI OpenSBI calls into) |
| `enclave.rs` | Enclave lifecycle & state, host⇄enclave context switch, shared memory |
| `isolator.rs` | Backend-agnostic isolation API; WorldGuard backend active |
| `wg.rs` | WorldGuard driver — programs wgChecker slots per region/world |
| `trap.rs`, `thread.rs`, `cpu.rs` | Trap handling, enclave threads, CSR access |

**World assignment** — each world is one WID; the SM hands isolation out per
world:

- **Trusted WID** (`NWORLDS-1`) — the SM itself.
- **OS WID** (`NWORLDS-2`) — the untrusted Linux/OS world (`mlwid` is set to it).
- **Each enclave** gets its own WID.

At boot the SM locks its own memory (`0x8000_0000`, 2 MB) to the trusted WID only
and opens the rest to the OS world.

## Driver (S-mode)

The Keystone driver (`linux-keystone-driver/`) is a loadable Linux kernel module
that lets host applications create and run enclaves. It is the stock Berkeley
Keystone driver, extended locally with the shared-memory (SHM) region API.

It sits between the host app and the SM:

```
host app  ──ioctl──►  driver (/dev/keystone_enclave)  ──ecall──►  Security Monitor
```

- **Device** — registers the misc device `/dev/keystone_enclave` exposing an
  ioctl interface (`keystone.c`, `keystone-ioctl.c`).
- **Memory** — allocates each enclave's physical memory from contiguous pages
  (CMA / `alloc_pages`): the **EPM** (enclave private memory) and **UTM**
  (untrusted shared buffer) — `keystone-page.c`, `keystone-enclave.c`.
- **SBI bridge** — turns each ioctl into an SBI call to the SM (`keystone-sbi.c`)
  under the `0x08424B45` ("BKE") extension — the same interface the firmware
  dispatches.

| ioctl | Action |
|-------|--------|
| `CREATE_ENCLAVE` | allocate EPM and set up the enclave (kernel side only) |
| `UTM_INIT` | allocate the untrusted shared buffer |
| `FINALIZE_ENCLAVE` | admit the enclave to the SM (SBI create) |
| `RUN` / `RESUME_ENCLAVE` | enter / re-enter enclave execution |
| `DESTROY_ENCLAVE` | tear down and free memory |
| `CREATE`/`MAP`/`UNMAP`/`CHANGE`/`SHARE_SHM` | shared-memory region management |

The host app never talks to the SM directly — the driver owns memory allocation
and forwards every trust decision down to M-mode.

## Application software (S + U)

The application layer is what a developer writes and runs. It is made of three
pieces that build together:

| Piece | Path | Runs as |
|-------|------|---------|
| **SDK** | `sdk/` | libraries linked into the host app (`host`, `edge`, `verifier`) and the enclave app (`app`) |
| **Runtime** — Eyrie | `runtime/` | in-enclave S-mode runtime that hosts the enclave app |
| **Examples** | `examples/` | sample enclaves (host + eapp) |

Each enclave is shipped as a `.vela` **package** bundling the enclave app, the
Eyrie runtime, and a loader, plus the host program that launches it.

### Example: `hello`

`examples/hello/` is the minimal enclave:

- **`eapp/hello.c`** — the enclave application (U-mode, inside the enclave).
- **`host/host.cpp`** — the untrusted host runner (U-mode, on Linux). Using the
  SDK it creates an `Enclave`, sets memory sizes, loads the eapp + runtime +
  loader, and calls `enclave.run()`.

The build packages them as `hello.vela`:

```
hello.vela = hello (eapp) + eyrie-rt (runtime) + loader.bin + hello-runner (host)
```

### Build

**1. Build & install the SDK** (provides the libraries and CMake macros):

```bash
cd sdk && mkdir -p build && cd build
export KEYSTONE_SDK_DIR=$PWD          # SDK install location
cmake .. && make && make install
```

**2. Build the examples** (point at the runtime source, then build):

```bash
export KEYSTONE_RUNTIME=<q-vela>/runtime
cd examples && mkdir -p build && cd build
cmake .. && make examples
```

This produces `examples/build/hello/hello.vela`.

### Run (inside the guest)

With the driver loaded, run the host program with the packaged artifacts:

```bash
insmod keystone-driver.ko
./hello-runner hello eyrie-rt loader.bin   # or: ./hello.vela
```
