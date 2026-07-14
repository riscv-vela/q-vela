# Custom ISA Extensions

Q-Vela extends the RISC-V `virt` CPU model with custom vector instructions targeting SDR (Software-Defined Robotics) workloads, in three domains:

1. **AI positional encoding** — RoPE (rotary positional embedding) for transformer inference
2. **Tokenizer acceleration** — heap (priority-queue) primitives, string compare, and byte-widening load for BPE-style tokenization
3. **Robot control** — batched PID controller computation

All custom instructions operate on the RISC-V Vector (RVV 1.0) register file and therefore require the V extension.

## Implementation Layout

| Component | Location |
|-----------|----------|
| Instruction encodings | `qemu/target/riscv/insn32.decode` |
| Decode / translation | `qemu/target/riscv/insn_trans/trans_rvv.c.inc` |
| Execution semantics | `qemu/target/riscv/vector_helper.c` |
| Test programs | `qemu/test-code/` |

Full instruction semantics are specified in the internal document *RISC-V Custom Instruction Specification v2.0*.

## Enabled Standard Extensions

The virtual CPU is launched with the following standard extensions on top of RV64GC:

```
-cpu rv64,v=true,vlen=512,vext_spec=v1.0,zba=true,zbb=true,zbs=true,zbc=true,zbkb=true,zcb=true,zicond=true
```

| Extension | Purpose |
|-----------|---------|
| `V` (RVV 1.0, VLEN=512) | Vector processing; prerequisite for all custom instructions |
| `Zba` / `Zbb` / `Zbs` / `Zbc` / `Zbkb` | Bit manipulation (address generation, basic ops, single-bit, carry-less multiply, crypto) |
| `Zcb` | Additional compressed instructions |
| `Zicond` | Conditional operations (`czero.eqz`/`czero.nez`) |

## Custom Instruction Summary

| Instruction | Encoding (opcode space) | Domain | Description |
|-------------|------------------------|--------|-------------|
| `vfrope.fvx` | funct6=010010, OP-V (`1010111`) | AI | Rotary positional embedding via 2-stage LUT |
| `vlshfte{8,16,32,64}.v` | nf=110, LOAD-FP (`0000111`) | Tokenizer | Heap ancestor-path gather load |
| `vsshfte{8,16,32,64}.v` | nf=110, STORE-FP (`0100111`) | Tokenizer | Heap ancestor-path scatter store |
| `vmspr.vx` | funct6=110000, OP-V | Tokenizer | Sign-magnitude priority-compare mask |
| `vstrcmp.vv` | funct6=111110, OP-V | Tokenizer | Vector string equality compare |
| `vlc8to32.v` | nf=100, LOAD-FP | Tokenizer | Load bytes, widen to 32-bit elements |
| `vfpid32b.vf` / `vfpid64b.vf` | funct6=000001/000011, OP-V | Control | Batched PID computation (fp32/fp64) |
| `vpidldeb.vx` / `vpidsteb.vx` | funct6=010001/010011, OP-V | Control | PID state distribute / gather across vector registers |

---

## 1. RoPE — `vfrope.fvx`

Applies the rotary positional embedding used in transformer attention. For each element pair (x₁, x₂) of the input vector `vs2`, the instruction writes the rotated pair to `vd`:

```
y1 = x1·cos(mθᵢ) − x2·sin(mθᵢ)
y2 = x1·sin(mθᵢ) + x2·cos(mθᵢ)
```

- `rs1` packs two 16-bit fields: **upper 16 bits = dimension index**, **lower 16 bits = token position m**
- θᵢ = 10000^(−2i/128) (head dimension 128)
- sin/cos are produced by a **two-stage LUT**: a coarse fp16 table (position quotient `m/64`) combined with a fine int8 table (remainder `m%64`) via angle-sum identities — modeling the intended hardware table implementation rather than calling libm directly

Tests: `test_vfrope_fvx.c`, `test_vfrope32_fvx.c`, `test_vfrope64_fvx.c`, `vfrope_precision.c` (precision analysis against double-precision reference).

## 2. Tokenizer Acceleration

BPE-style tokenization is dominated by a merge loop over a priority queue plus vocabulary string matching. These instructions vectorize its primitive operations.

### `vlshfte{8,16,32,64}.v` / `vsshfte{8,16,32,64}.v` — heap-path gather/scatter

Given a node index in `rs2`, walks the binary-heap ancestor chain `i → (i−1)/2 → … → 0` and loads (`vlshfte`) or stores (`vsshfte`) the elements along that path to/from a vector register in one instruction. A sift-up/down step becomes:

```
vlshfte  (gather ancestor path) → vmspr (compare priorities) → vsshfte (write back reordered path)
```

replacing a scalar loop of log₂N compare-and-swap iterations.

### `vmspr.vx` — priority-compare mask

Compares each element of `vs2` against scalar `rs1` in **sign-magnitude order** and produces a 0/1 mask. Sign-magnitude ordering matches the integer-bit comparison of IEEE-754 values, so float priority keys can be compared without FP hardware.

### `vstrcmp.vv` — string equality

Byte-wise comparison of two vector registers (null-terminator aware); used for vocabulary entry matching.

### `vlc8to32.v` — widening byte load

Loads consecutive bytes from memory at `rs1` and zero-extends each to a 32-bit vector element — raw UTF-8 input to token-ID-width slots in one instruction.

Tests: `test_vsshfte8/64.c`, `test_vstrcmp_vv.c`.

## 3. PID Control

### `vfpid32b.vf` / `vfpid64b.vf` — batched PID computation

Treats the vector register group as consecutive 8-element controller blocks:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| field | setpoint | measured | Kp | Ki | Kd | I-accum | prev-error | **output** |

For every block, one instruction computes

```
err = setpoint − measured
P   = Kp·err
I   = Ki·(I-accum + err·dt)      // I-accum updated in place
D   = Kd·(err − prev-error)/dt   // prev-error updated in place
output = P + I + D
```

The control period `dt` is passed in FP register **`fa0`**. Multiple controller blocks are processed per instruction, allowing all joints/actuators of a robot to be updated simultaneously.

### `vpidldeb.vx` / `vpidsteb.vx` — PID state movement

Distributes (`ld`) or collects (`st`) per-actuator PID state across vector registers, indexed by the actuator number in `rs1`.

Tests: `test_vfpid32b_vf.c`, `test_vfpid32bs_vf.c`, `test_vfpid64b_vf.c`, `test_vpidldeb8_vx.c`, `test_vpidsteb8/64_vx.c`.

---

## Related: Gemmini Opcode Space

The custom-3 opcode (`1111011`) carries the Gemmini accelerator command set (`gemmini_config`, `gemmini_mvin/mvout`, `gemmini_preload`, `compute_*`, `loop_ws` family, `loop_conv_ws` family, `k_gemv_loop_ws` family, `ternary_loop_ws`, `flush`/`fence`/trace controls). These are decoded by the CPU but **forwarded to the Gemmini device model** (`hw/misc/gemmini/`) rather than executed in the CPU — see [gemmini.md](gemmini.md).

## Known Issues / Status Notes

- `vmspr.vx`: only the 8-bit variant is active; 32/64-bit variants exist but are commented out, and the comparison direction differs between the active and commented versions — needs reconciliation against the specification.
- `vpidldeb.vx` / `vpidsteb.vx`: implementation still contains debug `printf` output and helper-internal heap allocation; functional but not cleaned up.
- `vfrope.fvx`: attention head dimension (128) is currently hard-coded; parameterization is noted as a TODO in the source.
