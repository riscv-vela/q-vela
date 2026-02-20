## Gemmini Development Guide

### Table of Contents

- [Adding a Custom Instruction for Gemmini](#adding-a-custom-instruction-for-gemmini)
- [Configuring Specs of Gemmini](#configuring-specs-of-gemmini)
- [Memory Movements](#memory-movements)

### Adding a Custom Instruction for Gemmini

Gemmini를 동작시키는 간단한 Toy Instruction을 구현하는 과정이다.

#### Updating Decode Tree

`q-vela/qemu/target/riscv/insn32.decode` 에 추가하려는 명령어의 이름과 Binary를 추가한다. 

명령어는 대개 R-type으로 선언한다.
```
gemmini_toy_insn        1111111 ..... ..... 011 ..... 1111011 @r
```

#### Declaring Trans Function

`q-vela/qemu/target/riscv/insn_trans/trans_gemmini.c.inc`에 아래와 같이 Trans함수를 추가한다.  
함수명은 trans_{명령어이름}으로 고정하며, Funct7과 Funct3 값을 인자로 넘긴다.
```c
static bool trans_gemmini_toy_insn(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b1111111, 0b011);
}
```

#### Declaring Device Function

`q-vela/qemu/hw/misc/gemmini/gemmini.h`에 추가하려는 함수의 함수원형을 선언한다. Trans Function과 달리 이름에 제약이 없다.
```c
void gemmini_toy_insn(GemminiState *gm)
```
어떤 Gemmini를 대상으로 명령어가 실행될지는 반드시 필요한 정보이므로, 함수 인자에 `GemminiState*`는 필수이다. 이외에도 `rs1`, `rs2`를 인자로 받을 수 있다.

#### Gemmini Decoder Update

`q-vela/qemu/hw/misc/gemmini/gemmini.c`최하단의 `switch`문을 수정한다. `funct7`과 `funct3`를 기반으로 선언해둔 함수가 호출될 수 있도록 한다.
```c
void gemmini_exec_cmd(GemminiState *s, uint32_t funct7, uint32_t funct3, uint64_t rs1, u_int64_t rs2, CPUState *env)
{
    switch (funct7) {
/* Existing Code */
        case 0b1111111:
            gemmini_toy_insn(s);
            break;
/* Existing Code */
    }
}
```

#### Implement Device Code

`q-vela/qemu/hw/misc/gemmini/gemmini.c`에 함수를 정의한 후, 구현한다.
```c
void gemmini_toy_insn(GemminiState *gm) {
    // Desired Action
}
```

#### (Optional) Add to RoCC API

컴파일러를 수정하지 않는 이상, 새로 추가된 명령어는 volatile asm을 통해 word형태로 직접 넣어주어야 한다. 해당 과정을 함수로 추상화하는 것이 rocc_api.h의 함수들이다.
```c
static inline void gemmini_toy_insn(void)
{  
    asm volatile (                                   
        ".word 0xFEB5307B\n"
        //1111111 01011 01010 011 00000 1111011
        : : : "memory"                        
    );
}
```

### Configuring Specs of Gemmini

Gemmini의 Configurable한 Parameter들은 대부분 `q-vela/qemu/hw/misc/gemmini/gemmini_params.h`에서 관리한다.
|주요 변수|역할|
|---|---|
|DIM|Systolic Array가 한 차원에 몇 개의 PE를 가질 지에 관한 변수이다. 한 번에 DIM*DIM만큼의 연산이 이루어진다.|
|BANK_NUM|Gemmini가 내부 저장소로 몇 개의 분리된 메모리를 가질 지에 관한 인자이다. Scratchpad과 Accumulator에 둘 다 적용된다.|
|BANK_ROWS|한 BANK에 몇 개의 Element가 들어갈 수 있는지에 관한 인자이다. 메모리 부족으로 Segmentation Fault가 일어난다면 해당 변수를 늘리는 것이 권장된다.|


### Memory Movements

Gemmini의 MVIN과 MVOUT은 CPU(Helper)로의 Callback으로 구현되어 있다.  
다른 함수에서 메인 메모리에의 접근과 이동이 필요할 때에는 콜백함수를 직접 호출하는 대신, 미리 선언된 `mvin`과 `mvout`함수를 이용하는 것을 권장한다.
```c
void gemmini_toy_insn(){
    /* Other Code */
    mvin(dram_addr, (rows << 48) | (cols << 32) | sp_addr, state_id, gm);
    /* Other Code */
    mvout(dram_addr, (rows << 48) | (cols << 32) | sp_addr, gm);
    /* Other Code */
}
```
다만 데이터의 이동 양상이 기존의 MVIN, MVOUT과 맞지 않을 경우 아래와 같이 콜백함수를 포함하는 새로운 함수를 생성하여 처리할 수 있다. (MVOUT의 Norm과 Activation이 필요하지 않은 경우 등)
```c
void custom_data_movement_ftn(){
    /* Other Code */
    mvin_varable = gm->mem_read(gm->mem_opaque, dram_addr + row * load_stride + col, read_size);
    gm->total_reads_in_bytes++;
    /* Other Code */
    gm->mem_write(gm->mem_opaque, dram_row_addr + i, row_buf[i], 1);
    gm->total_writes_in_bytes++;
    /* Other Code */
}
```