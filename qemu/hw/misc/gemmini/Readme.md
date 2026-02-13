# Q-vela Gemmini

> 26.01.02 ~ now

## Introduction

해당 문서는 "SDR의 개방형 생태계 구축을 위한 RISC-V 시스템 소프트웨어 개발" 프로젝트의 일환으로, Gemmini의 Custom instruction에 대한 OS 환경에서의 검증환경 구축을 위해 QEMU상에서 Gemmini의 구현과, 협력 연구실에서 제시된 Custom instruction의 구현을 목표로 한다.

## Current Implementations

아래는 현재 지원하는 기능들이다.
- 환경 설정 (Gemmini_config)
- [데이터 이동](#메모리-접근-memory-access) ([MVIN, MVOUT](#mvin과-mvout))
- [단위 연산](#단위-연산-basic-computation) ([Compute](#compute-함수), [Compute_gemv](#compute_gemv-함수), [Compute_ternary](#compute_ternary-함수))
- [복합 연산](#복합-연산-computation-wrappers) ([Loop_ws](#loop_ws), [**Gemv_loop_ws\***](#gemv_loop_ws), [**ternary_gemm_auto\***](#ternary_gemm_auto))
- [프로파일링](#프로파일링-profiling) (Gemmini_trace_init/dump)

(\* : Custom Instructions for LLM Acceleration)

아래의 기능들은 현재 지원하지 않는다.
- 기타 복합 연산 (Conv_ws 등)
- Double Buffering (Gemv_loop_ws의 Spad재사용을 위해 비활성화)

아래의 기능들은 부분적으로 지원한다.
- Syncronization ([Gemmini_fence](#gemmini-fence))

## 실행 (Launch)

Gemmini를 포함한 채로 부팅하기 위해 .sh에 gemmini 옵션을 추가해야 한다. 부착 여부와 개수를 결정해야 하며, **id는 gemmini{순번} 으로 설정하여야 한다.** 아래는 부팅 sh파일의 예시이다. [여러 대의 Gemmini를 사용](#n-core-m-gemmini-structure)할 때에 성능 측정 등을 위해 multithreading tcg 옵션 역시 권장된다. 
```
./q-vela/qemu/build/qemu-system-riscv64 \
	-monitor telnet:127.0.0.1:4444,server,nowait \
	-machine virt -nographic -m 16384 -smp 4 \
	-bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin \
	-kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
	-cpu rv64,v=true,vlen=512,vext_spec=v1.0,zba=true,zbb=true,zbs=true,zbc=true,zbkb=true,zcb=true,zicond=true \
	-netdev user,id=eth0 -device virtio-net-pci,netdev=eth0 \
	-accel tcg,thread=multi \
	-device virtio-rng-pci \
	-drive file=./ubuntu-24.04-preinstalled-server-riscv64.img,format=raw,if=virtio \
	-device gemmini,id=gemmini0 \
	-device gemmini,id=gemmini1 \
	-device gemmini,id=gemmini2 \
	-device gemmini,id=gemmini3 \
	-D qemu.log \
```

## 명령어 전달 과정 (Instruction Flow)

명령어 전달 과정은 기존의 RISCV ISA를 확장할 때와 동일한 방법으로 이루어진다.
1. `rocc_api.h`의 volatile asm이 포함된 함수를 호출하여 Custom opcode가 포함된 명령어를 word형태로 삽입.
2. `insn32.decode`의 decode tree에서 추가된 opcode를 인식하여 `trans_gemmini.c.inc`의 trans함수를 호출, trans 함수에서 helper함수를 호출.
3. `gemmini_helper.c`의 `riscv_cpu_get_gemmini()`를 통해 스케줄링 정책에 따라(현재는 단순 modulo 연산으로 matching) 정해진 `gemmini_state`의 구조체 포인터를 반환
4. funct7, funct3 정보와 `riscv_cpu_get_gemmini()`를 통해 받아온 포인터를 토대로 `gemmini_exec_cmd()`를 호출하여 실제 명령어를 수행.
5. `gemmini_exec_cmd()`는 switch문을 통해 funct7과 func3에 따라 `gemmini.c`의 특정 함수를 호출하여 정해진 명령어를 수행

## Gemmini QOM

Gemmini는 QOM Device로, QEMU에서 제공하는 QOM의 initialization, Realization 방식을 그대로 따른다.

Device와 관련된 파일들은 모두 `q-vela/qemu/hw/misc/gemmini/`에 포함되어 있으며, 빌드에 포함하기 위해 `hw/misc/meson.build`에 빌드 경로를 추가하였다.

Device가 Initialize되면 접근을 위한 구조체와 포인터 `GemminiState*`가 생성된다. 구조체는 명령어 수행에 필요한 내부 메모리(Accumulator, Scratchpad, Bias_buffer 등)과 Config를 통해 설정 가능한 여러 가지 상태를 가지고 있다.

## 메모리 접근 (Memory Access)

### 콜백을 이용한 메모리 접근

실제 Gemmini는 Core와 연동되는 별도의 TLB와 DMA Engine을 가지고 있기에 명령어를 통해 전달되는 Virtual Address만 가지고도 메모리 접근이 가능하다. 다만 QEMU상에서 Gemmini 전용 TLB를 구현하는 것은 무리가 있기에, **모든 메모리 접근은 `gemmini_helper.c`로의 콜백을 통해 코어에서 직접 처리**한다.

콜백함수 `gemmini_mem_read_cb`와 `gemmini_mem_write_cb`는 각각 `gemmini.c`내의 함수에서의 요청에 의해 cpu상태인 opaque, Virtual Address, 그리고 Read/Write크기인 size(바이트 단위)를 인자로 QEMU에서 제공하는 메모리 접근 함수인 `cpu_ldub_data`와 `cpu_sdb_data`를 통해 메모리 접근 명령을 처리하게 된다.

### MVIN과 MVOUT

MVIN은 DRAM의 특정 주소에서 Spad 혹은 Acc의 특정 주소로 원하는 크기만큼 메모리를 로드해오는 함수이고, MVOUT은 반대로 Acc혹은 Spad에서 DRAM의 특정 주소로 원하는 크기만큼 메모리를 가지고 나가는 함수이다. 두 함수 모두 row-by-row로 이루어진다.

MVIN시에는 읽어들이려는 데이터의 크기만큼 임시로 row_buffer를 만들어 `gemmini_mem_read_cb`콜백을 통해 모든 데이터를 버퍼에 로드한 후 내부 메모리로 Memcpy를 통해 복사한다.

MVOUT시에는 내부 메모리의 데이터를 row_buffer에 모두 Memcpy한 후, `gemmini_mem_write_cb`를 통해 row_buffer에서 DRAM으로 데이터를 내보낸다.

### Gemmini Fence

`gemmini_fence`는 원래 CPU와 Gemmini간 동기화를 위한 함수로, Gemmini에서 진행하는 연산이 끝날 때 까지 CPU가 대기하도록 정의된 함수이다. 본 프로젝트에서는 Volatile asm을 통해 메모리 변경 표시자를 삽입해, 메모리 변경까지 CPU가 대기하도록 하여 우회적으로 해당 기능을 구현하였다.

## 단위 연산 (Basic Computation)

### Compute 함수

연산에 있어 가장 기본이 되는 함수이며, 연산 과정이 달라지는 `gemv_loop_ws`의 경우에는 compute의 변형 함수인 `compute_gemv`를, `ternary_gemm_auto`의 경우에는 `compute_ternary`함수를 추가적으로 사용한다. 함수의 내부 진행과정은 아래와 같다.

#### 1\) Preload

Preload(bool)이 True라면 `pe_state`들을 Config를 통해 지정된 preload address의 값들로 교체한다. Compute함수를 사용하는 `loop_ws`명령어의 경우, WS([Weight Stationary](#wsweight-stationary와-osoutput-stationary))함수이기 때문에 Iteration이 반복되는 동안 Weight인 B가 오랫동안 PE에서 내려가지 않는다. 이러한 경우에는 PE를 업데이트할 필요가 없기 때문에 Preload=false가 되고, 해당 weight들을 사용하는 연산이 모두 끝나 다음 weight들을 PE에 실어야 할 때 다시 true가 된다. **따라서 코드에서는 반복문의 첫 반복에만 preload가 true가 되는 경우가 많다.**

#### 2\) Computation
Matrix-Multiplication을 진행한다. DIM*DIM에 해당하는 임시 결과 행렬을 만들어 행렬곱의 결과를 담는다. 이후 Accumulator혹은 Spad로 결과를 내보낸다.

실제 Gemmini의 경우 Systolic Array로 구현되어 있어 내부의 값들이 한 줄씩 이동하며 값을 더하지만 본 프로젝트에서는 레퍼런스로 사용한 Gemmini Spike Simulator와 동일하게 기능 검증에 초점을 맞추어, 단순 반복문을 이용한 행렬곱을 진행하며 이는 추후 등장하는 Compute_gemv와 Compute_Ternary에서도 동일하게 적용된다.

#### 3\) Write Back
계산된 결과를 다시 Scratchpad나 Accumulator로 내보낸다.

### Compute_gemv 함수

Compute함수의 변형으로, Custom instruction인 `gemv_loop_ws`를 지원하기 위해 추가된 함수이다. Accumulator에 합산하던 기존의 Compute와 달리 하드웨어를 따라 Bias_buffer에 합을 저장하고, 연산이 끝날 때에 Scratchpad로 보낼 수 있도록 한다.

### Compute_ternary 함수

`ternary_gemm_auto`에서는 DIM\*DIM의 행렬곱을 수행하는 `loop_ws`와 달리 한 번에 4\*DIM\*DIM의 행렬곱을 수행한다. 이를 위해 경량 Ternary PE가 추가적으로 필요하며, 해당 연산을 지원하도록 변형된 함수이다.

## 복합 연산 (Computation Wrappers)

복합 연산 명령어는 `loop_ws`, `gemv_loop_ws`, `ternary_gemm_auto`와 같이 단위 연산을 감싸 큰 단위의 연산을 진행하는 Wrapper 명령어들이다.

### Loop_ws

#### 1\) Preparation
`rs1`과 `rs2`의 값들을 기반으로 연산에 필요한 인자들을 정리한다. 대부분 미리 설정된 Configuration을 그대로 따르지만, Address 설정에 신경을 써야 한다. `Loop_ws`의 경우 후술될 `gemv_loop_ws`와는 달리 Bank를 재사용하지 않기 때문에 C(결과)와 D(Bias)는 Spad에 저장될 일이 거의 없으며, A의 시작 주소는 0부터, B의 시작 주소는 전체 크기의 절반에 도달하도록 설정된다.
C와 D는 Address의 상위 비트들이 1로 설정되기 때문에 가르키는 주소는 Spad가 아니라 Accumulator이다.

#### 2\) Is resadd
`is_resadd` 여부를 확인하여 true라면 단순히 A와 B를 더한 후 C의 주소로 내보내고 return한다.

#### 3\) Bias Load
Bias가 존재하는 연산이라면(`loop_ws_D != 0`) D의 값들을 mvin해서 accumulator에 미리 담는다. 이후 Load된 Accumulator 위에 행렬곱이 더해지게 된다.

#### 4\) Computation
DIM\*DIM 블록들을 순회하며 각 타일의 결과를 합하여 저장하는 부분으로, 각 타일의 연산에 필요한 A, B 부분들을 `MVIN`해온다. A와 B가 Spad에 담긴 이후 순차적으로 `Preload`와 `Compute`를 호출한다. `Preload`는 `Gemmini_State`에 `preload`에 필요한 정보들(B의 weight가 저장된 주소 등)을 담고 `Compute`는 선술한 기본적 연산이 일어나 Accumulator에 부분합이 저장된다.

#### 5\) MVOUT
단위 연산이 종료(k = K-1)된 후 output address가 유효하다면 해당 주소로 Accumulator에서 데이터를 `MVOUT`한다. 이 과정에서 Activation이 일어난다.
(Activation은 DIM이 아닌 Row나 Col 단위로 적용되기 때문에, 마지막 Row가 아니라면 `MVOUT`이 호출되어도 실제 write는 일어나지 않고 Norm의 update만 일어날 수 있다.)

### Gemv_loop_ws

`Gemv_loop_ws`는 기존의 `loop_ws`와 유사하지만, 연산된 결과를 DRAM으로 바로 내보내는 대신 다음 연산을 위해 Spad에 저장하여 데이터 이동 비용을 절감할 수 있다.
이를 위해 타일 연산의 순회 순서가 바뀌며 연산 간 결과를 보전하기 위해 Weight Matrix (B)에 `Reorder_matrix`를 해주어야 한다. (Test Code 및 Specification에 상술)

또한 `Gemv_loop_ws`는 기존의 `loop_ws`와 달리 연산합을 accumulator가 아닌 별도의 bias_buffer에 저장하기 때문에, 이에 대한 실행흐름을 단순화하기 위해 별도의 `compute_gemv()` 함수를 추가적으로 사용한다. `C_spad_id`가 0이라면 기존의 `compute()`를, `c_spad_id`가 0이 아니라면 `compute_gemv`를 실행한다.  `Compute_gemv()`는 `gemv_loop_ws`연산만을 위한 함수이므로, WS 경로만 존재하며 [OS Mode](#wsweight-stationary와-osoutput-stationary)에서 실행 시 에러와 함께 exit한다. 하드웨어와 마찬가지로 Activation은 따로 지원하지 않는다.

### Ternary_gemm_auto

`Ternary_gemm_auto`는 `Gemv_loop_ws`와 마찬가지로 6개의 RoCC명령어를 실행시키는 wrapper 명령어이며, `rocc_api.h`에 함수로 구현되어있다. 타일을 순회할 때에 기존과 다르게 Col을 4개 단위로 순회하며, 연산에 `Compute_ternary`를 사용한다. `Compute_ternary`는 INT8에 4개의 ternary weight(-1, 0, 1)이 packing된 형태를 위한 연산 함수이다. 내부에서 4개의 weight로 unpacking을 진행하며, 3개의 연산은 Ternary_PE에서, 하나의 연산은 기존 PE에서 진행해 `Compute_Ternary`호출 한 번당 4개(4 \* DIM \* DIM)의 연산이 동시에 이루어진다.

`Gemv_loop_ws`와 마찬가지로 OS Mode를 지원하지 않아 OS Mode에서 실행 시 에러와 함께 exit한다.

## 프로파일링 (Profiling)

프로파일링을 위한 최소한의 기능을 지원한다.
- Qemu_log를 통해 각 RoCC 명령어가 실행되는 데에 소요된 Host Clock Tick를 확인할 수 있다.
- Qemu_log를 통해 각 Core가 지정한 Gemmini를 소요하기까지(Lock을 가져갈 때까지) 걸린 Host Clock Tick를 확인할 수 있다.
- `gemmini_trace_init`을 통해 trace 변수들을 초기화할 수 있다. 현재는 `memory_read_in_bytes`와 `memory_write_in_bytes`를 지원한다.
- `gemmini_trace_dump`를 통해 trace 변수들을 Qemu_log에 출력할 수 있다. 현재는 위에서 언급한 바와 같이 init부터 dump까지의 Memory Read/Write를 확인할 수 있다. 

## N-Core-M-Gemmini Structure

QEMU에서는 기본적으로 Multicore를 지원하며(smp옵션을 통해 설정), Gemmini Device도 부팅 시에 개수와 ID를 설정할 수 있기에 N-Core-M-Gemmini구조를 테스트할 수 있다. 해당 프로젝트에는 **N-Core-M-Gemmini구조를 동작시키기 위한 최소한의 기능을 지원**한다. 복잡한 Scheduling과 Dependency는 지원하지 않는다. 

### Initialization

위에서 언급한 바와 같이, 부팅 시에 여러 대의 Gemmini를 부착할 수 있다.
```
-device gemmini,id=gemmini0 \
-device gemmini,id=gemmini1 \
-device gemmini,id=gemmini2 \
-device gemmini,id=gemmini3 \
```
로그에 다음과 같이 뜬다면 정상적으로 Device가 생성된 것이다.
```
Gemmini: Added instance with ID 0 (total: 1)
Gemmini: Added instance with ID 1 (total: 2)
Gemmini: Added instance with ID 2 (total: 3)
Gemmini: Added instance with ID 3 (total: 4)
```

### RISCV_CPU_GET_GEMMINI

RoCC 명령어가 실행되면, trans 함수를 거쳐 `gemmini_helper.c`의 helper함수가 호출된다. Helper 함수인 `helper_rocc_exec`에서는 먼저 실행할 gemmini를 찾기 위해 `riscv_cpu_get_gemmini`를 호출하여, 적절한 `GemminiState*`를 반환한다.

### Mutex

이때 Gemmini가 한 번에 하나의 연산만 할 수 있도록, 접근하는 코어는 하나로 제한되어야 한다. 이를 QEMU에서 제공하는 `qemu_mutex_lock`를 이용해, 다른 코어가 gemmini를 소유하고 있지 않을 때만 명령어를 실행할 수 있도록 한다.

이후 `gemmini_exec_cmd`를 통해 실제 Device코드를 실행하고, 연산이 종료된 후 `qemu_mutex_unlock`를 통해 다른 코어가 접근가능하도록 lock을 해제한다.

### Gemmini Manager

Gemmini manager는 여러 대의 Gemmini를 관리하기 위한 역할로, QOM을 통해 gemmini가 생성될 때마다 `registry_add`를 통해 배열에 추가한다. 이외에도 `delete`와 `get`함수를 이용해 지정된 ID의 gemmini를 배열에서 제거하거나, 반환할 수 있다.

## 부록 (Appendix)

직접적으로 Q-Vela Gemmini의 구현에 관한 내용은 아니지만, Gemmini 자체 혹은 QEMU자체에 관하여 Q-Vela Gemmini와 관련 있는 내용들을 적어둔 챕터이다.

### WS(Weight Stationary)와 OS(Output Stationary)

WS는 Weight Stationary 모드로 PE가 연산을 할 때에 Weight에 해당하는 B행렬을 고정하여 계속 사용할 수 있도록 반복문에서 i가 가장 inner loop이 되는 형태이며 Compute 시에 pe_state에 weight를 유지한다.

OS는 Weight Stationary 모드로 B를 재사용하는 대신 Output이 나가기 전에 모든 연산이 완료되도록 k가 가장 inner loop이며, pe_state에 중간합 결과를 유지한다.

구현을 목표로 한 loop_ws, gemv_loop_ws, ternary_gemm_auto 모두 WS 방식만을 사용하는 연산들이기 때문에, Compute_gemv와 compute_ternary 함수에서 **OS모드가 사용하는 경로는 예외(exit)로 처리**하였다.