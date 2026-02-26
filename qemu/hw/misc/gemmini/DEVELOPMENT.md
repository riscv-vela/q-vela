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

## Gemmini API

### Upstream과의 통합

Q-vela의 구현 목표는 Gemmini의 하드웨어 동작을 QEMU로 모사하는 것이다. Workload로부터 RoCC 명령을 실행하는 API는 Upstream으로부터 받아와야, 추후 workload에 대한 검증 등이 가능하다. 이를 위해 software-hardware API 는 Upstream(협력 연구실)에서 사용하는 것을 최소한의 변형만으로 사용하도록 한다.

필요한 파일들은 `test-code/gemmini_test_code/include`에 포함되어 있으며, `gemmini.h`는 함수를 통해 데이터를 전처리하고 RoCC 명령어를 호출하는 역할을, `xcustom.h`는 호출된 RoCC명령을 Assembly Instruction으로 바꾸어 주입하는 역할을, `gemmini_params.h`와 `gemmini_utils.h`는 `gemmini.h`의 전처리에 필요한 하드웨어 규격과 유틸리티 함수를 제공하는 역할을 한다.

따라서 하드웨어 규격을 변경할 일이 생긴다면 (DIM을 16에서 32로 키우는 등)**`hw/misc/gemmini/gemmini_params.h`뿐만 아니라 `include/gemmini_params.h`의 인자들도 모두 변경해주어야, 소프트웨어와 하드웨어 모두 변경사항을 적용할 수 있다.**

결과적으로, 테스트 코드의 위치에 위 네개의 파일이 포함된 `include/`폴더를 두고, 
```c
#include "include/gemmini.h"
```
만 포함시켜주면, gemmini.h를 거쳐 Q-vela에서 제공하는 명령어들을 실행할 수 있다. 혹여나 새로운 명령어가 추가된다면 직접 API를 구현하려 하지 말고 되도록 잦은 소통을 통해 협력 연구실에서 최신 API를 받아오는 방향이 바람직할 것 같다.

### 미구현 영역들

`include/gemmini.h`를 보면 주석처리가 되어있는 부분을 많이 볼 수 있다. 이는 현재 q-vela상에서 지원하지 않는 기능들을 주석처리 한 것으로, 현재 q-vela는 loop_ws, gemv_loop_ws, ternary_gemm_auto를 지원하고 있으며, **counter에 관한 기능 일체 또는 OS mode의 명령어들, convolution관련된 명령어들은 지원하지 않는다.** 따라서 컴파일에러를 피하기 위해 해당 영역은 주석처리가 되어 있으며, 추후 실행하는 Workload에 해당 기능들이 필요하다면 구현할 필요가 있을 것으로 보인다.

Convolution과 같이 Gemmini가 기본적으로 지원하나, q-vela에 아직 구현되지 않은 기능의 경우, 본래 Gemmini의 Cycle Accurate 시뮬레이션을 위한 [Spike Simulator의 코드](https://github.com/ucb-bar/libgemmini/blob/master/gemmini.cc)를 참고하면 구현에 도움이 될 것으로 보인다.

## Gemmini Threaded

### 개요

M-Core-N-Gemmini 구조를 테스트하기 위해, gemmini_threaded 브랜치에서는 각각의 Gemmini Device에 독립적인 Qemu Thread를 할당하여, 특정 쓰레드를 지정한 Core에 고정하는 taskset과 함께 사용하였을 때 실질적인 병렬 실행을 가능케 하도록 구현되어 있다. QEMU를 부팅한 후, 아래의 명령어를 이용해 현재 실행 중인 qemu_thread들의 pid와 tid를 구할 수 있으나, 글자수 제한에 걸려 쓰레드 이름을 온전하게 볼 수 없는 경우가 있다.
```
ps -eLo pid,tid,comm | grep qemu
```
그러한 경우에는 Qemu_log를 통해(`q-vela/qemu/util/qemu-thread-posix.c`에서 변경) 각 Core와 Gemmini가 실행중인 Host Thread ID를 확인할 수 있으며, 아래와 같이 Taskset명령어를 통해 특정 코어에 고정할 수 있다.
```
sudo taskset -cp [고정할 물리 코어 번호] [고정할 vCPU/Gemmini의 TID]
```
이후 각 TID의 PSR 값이 지정한 물리 코어에 고정되었는지 확인한다.
```
ps -eLo pid,tid,psr,comm | grep qemu
```

### Gemmini Fence

Threaded Gemmini의 경우 `gemmini_fence()`의 활용이 정말 중요한데, **순차 실행을 보장하는 main branch와 달리, 각각의 gemmini가 독립적인 쓰레드로 실행되는 비동기 방식이기 때문에 메모리 문제에 매우 취약하다.** 따라서 메모리 접근(주로 `loop_ws`와 같은 CISC명령어를 모두 포함)이 있은 후마다 gemmini_fence를 통해, 이전 명령어의 메모리 접근이 끝나기 전에 다음 명령어가 실행되지 않도록 막아주는 것이 중요하다. 메모리 접근 후에 fence로 막아주지 않으면 십중팔구 segmentation fault를 볼 수 있다..

이러한 특성 때문에 Main과 Gemmini_threaded 브랜치에서는 `gemmini_fence()`가 구현된 방법 자체가 다른데, 기존의 Upstream API (현재 Main 브랜치에서 사용)는 `gemmini_fence()`를 단순히 RISC-V 명령어인 `fence`를 삽입하는 방식으로 사용하지만, 자체적 비동기 방식을 사용하는 gemmini_threaded의 경우 fence에 별개의 RoCC 명령어를 할당하여, 이전 명령어가 Mutex를 해제하기 전까지 fence에 의해 막혀있는 방식을 택한다(`target/riscv/gemmini_helper.c`참고).

즉 **Main과 Gemmini_threaded 브랜치에서 `gemmini_fence()`의 구현이 다르기 때문에, 빌드되는 QEMU에 맞추어 그에 맞는 `include/gemmini.h`파일을 사용해야 한다.** Main의 API를 Threaded에 사용한다면 segmentation fault가, Threaded의 API를 Main에 사용한다면 Illegal Instruction을 만나게 될 것이다.

또한 현재 Gemmini Fence의 경우 단일 Gemmini를 향하는 명령어이기 때문에 여러 Gemmini를 동기화해야 하는 경우 별도의 Instruction을 추가해야 한다. 현재 생각나는 방법은 Gemmini_Manager 역시 QOM과 쓰레드로 구현하여 Gemmini들을 관리할 수 있도록 하는게 가장 편할 것으로 보인다.

## 뭔가 적어두면 도움이 될 것 같은 자잘한 TMI
- `Loop_ws`와 `Ternary_gemm_auto` 는 `rs1`의 20번째 비트인 `mpgemm`으로 구분하는데, 하나의 함수로 내부에서 조건분기하면 너무 길어지기에 둘 다 `switch_ternary`를 호출하도록 하여 각각의 함수를 호출하는 방식을 택하였다.
- 테스트 코드는 `gemmini_stress_test.c`가 종합적으로 `gemv_loop_ws`, `ternary_gemm_auto`의 두 명령어에 대해 여러 가지 Dimension, Bias, Random Init등 다양한 경우의 테스트를 해주기 때문에 해당 테스트만 통과한다면 어느 정도 기능적으로 안정적이라고 볼 수 있다.
- 현재 마당한 Scheduling Policy가 없어 사실상의 M:M 연결인 Modulo정책을 사용중인데, Gemmini를 부착하지 않고 RoCC명령어를 실행하면 %0에 의해 DivisionByZeroException이 발생할 수 있다.
- 협력 연구실에 문의한 결과 `Gemv_loop_ws`와 `Ternary_gemm_auto`는 1월 19일 기준 Activation을 지원하지 않으며, Test도 No Activation위주로 이루어졌다.
- `GemminiState`에서 `gemmini_lock은` 기존 비동기 방식을 사용할 때의 Mutex Lock이고, Worker Thread를 이용한 비동기 방식은 `Worker_lock`을 사용한다.
