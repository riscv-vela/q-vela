# Q-vela Gemmini

> 26.01.02 v1.0 작성
> 26.07.08 v2.0 내용 반영하여 수정 (multi gemmini 지원, conv 연산 지원 등 업데이트)

## Introduction

해당 문서는 "SDR의 개방형 생태계 구축을 위한 RISC-V 시스템 소프트웨어 개발" 프로젝트의 일환으로, Gemmini의 Custom instruction에 대한 OS 환경에서의 검증환경 구축을 위해 QEMU상에서 Gemmini의 구현과, 협력 연구실에서 제시된 Custom instruction의 구현을 목표로 한다.

본 버전(qemu_thread)은 각 Gemmini Device가 **독립적인 Worker Thread에서 비동기로 명령어를 실행**하는 구현으로, 순차(동기) 실행 방식이던 기존 main 브랜치 구현과 실행 모델이 다르다. 자세한 차이는 [명령어 전달 과정](#명령어-전달-과정-instruction-flow)과 [N-Core-M-Gemmini Structure](#n-core-m-gemmini-structure)를 참고.

## Current Implementations

아래는 현재 지원하는 기능들이다.
- 환경 설정 (Gemmini_config)
- [데이터 이동](#메모리-접근-memory-access) ([MVIN(2,3), MVOUT](#mvin과-mvout))
- [단위 연산](#단위-연산-basic-computation) ([Compute](#compute-함수), [Compute_gemv](#compute_gemv-함수), [Compute_ternary](#compute_ternary-함수))
- [복합 연산](#복합-연산-computation-wrappers) ([Loop_ws](#loop_ws), [**Gemv_loop_ws\***](#gemv_loop_ws), [**ternary_gemm_auto\***](#ternary_gemm_auto), [Loop_conv_ws](#loop_conv_ws))
- [동기화](#gemmini-fence) (Gemmini_fence — 전용 RoCC 명령어, Flush)
- [프로파일링](#프로파일링-profiling) (Gemmini_trace_init/dump)
- [부하 실험용 명령어](#heavy-compute-only-workload) (Compute-only Heavy Workload)

(\* : Custom Instructions for LLM Acceleration)

아래의 기능들은 현재 지원하지 않는다.
- OS(Output Stationary) Mode 연산
- Counter 관련 기능 일체
- Double Buffering (Gemv_loop_ws의 Spad재사용을 위해 비활성화)

알려진 제한 사항은 아래와 같다.
- 직사각형(non-square) Convolution의 타일링 경로에서 결과가 부정확한 케이스가 남아있다. (im2col WS A-layout 불일치, 추후 수정 예정)

## 실행 (Launch)

Gemmini를 포함한 채로 부팅하기 위해 .sh에 gemmini 옵션을 추가해야 한다. 부착 여부와 개수를 결정해야 하며, **id는 gemmini{순번} 으로 설정하여야 한다.** 아래는 부팅 sh파일의 예시이다. 각 Gemmini가 독립 쓰레드로 실행되므로 [여러 대의 Gemmini를 사용](#n-core-m-gemmini-structure)할 때에는 multithreading tcg 옵션(`-accel tcg,thread=multi`)이 권장되며, 쓰레드 pinning을 위해 로그 옵션(`-D qemu.log`)도 필요하다.
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

부팅 후 성능 측정을 위해 vCPU 쓰레드와 Gemmini Worker 쓰레드를 물리 코어에 고정하려면, 저장소에 포함된 `pin.sh`를 사용한다. 해당 스크립트는 qemu.log에서 각 쓰레드의 TID를 파싱하여 vCPU 0..N-1을 물리 코어 0..N-1에, Gemmini Worker 0..M-1을 그 다음 코어들에 taskset으로 고정한다.
```
sudo ./pin.sh qemu.log
```

## 명령어 전달 과정 (Instruction Flow)

명령어의 디코딩까지는 기존의 RISCV ISA를 확장할 때와 동일한 방법으로 이루어지며, 이후 실행은 Worker Thread로의 Enqueue 방식으로 이루어진다.
1. `rocc_api.h`(upstream API)의 volatile asm이 포함된 함수를 호출하여 Custom opcode가 포함된 명령어를 word형태로 삽입.
2. `insn32.decode`의 decode tree에서 추가된 opcode를 인식하여 `trans_gemmini.c.inc`의 trans함수를 호출, trans 함수에서 helper함수(`helper_rocc_exec`)를 호출.
3. Fence 명령어(funct7=`0b0111111`)라면 Device로 전달하지 않고 helper에서 직접 처리한다. ([Gemmini Fence](#gemmini-fence) 참고)
4. Fence가 아니라면 [스케줄링 정책](#riscv_cpu_get_gemmini-scheduling-policy)에 따라 명령어를 실행할 Gemmini를 정하고(`GemminiState*` 획득), 해당 CPU를 그 Gemmini에 binding한다.
5. helper는 funct7, funct3, rs1, rs2를 `GemminiCommand` 구조체에 담아 해당 Gemmini에 enqueue(`has_work = true`)하고 `worker_cond`를 signal한 후 리턴한다. 단, **DRAM에 접근하는 명령어(MVIN, MVIN2, MVIN3, MVOUT, Loop_conv_ws 실행)의 경우 vCPU가 Worker의 실행 완료까지 동기 대기**한다. (vCPU의 TLB 변경과 Worker의 DRAM 접근이 race하여 비결정적 결과가 나오는 것을 막기 위함)
6. 각 Gemmini의 Worker Thread가 enqueue된 명령을 꺼내어 `gemmini_exec_cmd()`를 호출하고, switch문을 통해 funct7과 funct3에 따라 `gemmini.c`의 특정 함수를 호출하여 정해진 명령어를 수행한다.

## Gemmini QOM

Gemmini는 QOM Device로, QEMU에서 제공하는 QOM의 initialization, Realization 방식을 그대로 따른다.

Device와 관련된 파일들은 모두 `q-vela/qemu/hw/misc/gemmini/`에 포함되어 있으며, 빌드에 포함하기 위해 `hw/misc/meson.build`에 빌드 경로를 추가하였다.

Device가 Realize되면 접근을 위한 구조체와 포인터 `GemminiState*`가 생성되어 [Gemmini Manager](#gemmini-manager)에 등록되고, **해당 Device 전용의 Worker Thread(`gemmini-worker`)가 생성**된다. 구조체는 명령어 수행에 필요한 내부 메모리(Accumulator, Scratchpad, Bias_buffer 등)와 Config를 통해 설정 가능한 여러 가지 상태, 그리고 Worker Thread 동기화를 위한 `worker_lock`/`worker_cond`/`has_work`/`worker_running` 필드를 가지고 있다. Unrealize 시에는 `stopped` 플래그를 세워 Worker Thread를 join한 후 정리한다.

Worker Thread는 무한 루프를 돌며 `has_work`가 설정되기를 기다리다가, 명령이 enqueue되면 이를 꺼내어 `worker_lock`을 해제한 채로 실행하고(실행 중 CPU가 다음 명령을 enqueue할 수 있도록), 실행이 끝나면 `worker_running`을 해제하고 `worker_cond`를 broadcast하여 대기 중인 vCPU(Fence 또는 동기 대기)를 깨운다.

## 메모리 접근 (Memory Access)

### 콜백을 이용한 메모리 접근

실제 Gemmini는 Core와 연동되는 별도의 TLB와 DMA Engine을 가지고 있기에 명령어를 통해 전달되는 Virtual Address만 가지고도 메모리 접근이 가능하다. 다만 QEMU상에서 Gemmini 전용 TLB를 구현하는 것은 무리가 있기에, **모든 메모리 접근은 `gemmini_helper.c`의 콜백을 통해 처리**한다.

콜백함수 `gemmini_mem_read_cb`와 `gemmini_mem_write_cb`는 각각 `gemmini.c`내의 함수에서의 요청에 의해 cpu상태인 opaque, Virtual Address, 그리고 Read/Write크기인 size(바이트 단위)를 인자로 메모리 접근 명령을 처리한다. 내부적으로는 `cpu_memory_rw_debug()`를 사용하는데, 이는 **vCPU 쓰레드가 아닌 Worker Thread에서도 안전하게 게스트 메모리에 접근할 수 있도록 하기 위함**이다. (기존 main 브랜치는 vCPU 쓰레드에서 실행되므로 `cpu_ldub_data`/`cpu_stb_data`를 사용했다.)

다만 이 방식으로도 vCPU가 동시에 TLB를 변경하는 상황과의 race는 피할 수 없기 때문에, [명령어 전달 과정](#명령어-전달-과정-instruction-flow)에서 설명한 대로 DRAM에 접근하는 명령어는 vCPU가 Worker의 완료를 동기 대기하도록 처리되어 있다.

### MVIN과 MVOUT

MVIN은 DRAM의 특정 주소에서 Spad 혹은 Acc의 특정 주소로 원하는 크기만큼 메모리를 로드해오는 함수이고, MVOUT은 반대로 Acc혹은 Spad에서 DRAM의 특정 주소로 원하는 크기만큼 메모리를 가지고 나가는 함수이다. 두 함수 모두 row-by-row로 이루어진다.

MVIN은 Load Pipeline State를 지원하여 MVIN(state 0), MVIN2(state 1), MVIN3(state 2)의 세 가지 명령어가 각각 독립적인 stride/scale/shrunk 설정을 가진 채로 로드를 수행할 수 있다. MVIN시에는 읽어들이려는 데이터의 크기만큼 임시로 row_buffer를 만들어 `gemmini_mem_read_cb`콜백을 통해 모든 데이터를 버퍼에 로드한 후 내부 메모리로 Memcpy를 통해 복사한다.

MVOUT시에는 내부 메모리의 데이터를 row_buffer에 모두 Memcpy한 후, `gemmini_mem_write_cb`를 통해 row_buffer에서 DRAM으로 데이터를 내보낸다.

### Gemmini Fence

`gemmini_fence`는 CPU와 Gemmini간 동기화를 위한 함수로, Gemmini에서 진행하는 연산이 끝날 때 까지 CPU가 대기하도록 정의된 함수이다.

비동기 Worker Thread 방식에서는 이전 명령어의 메모리 접근이 끝나기 전에 다음 명령어가 실행되는 것을 막는 것이 매우 중요하기 때문에, 본 버전에서는 **Fence를 전용 funct7(`0b0111111`)을 가진 별개의 RoCC 명령어로 구현**하였다. Fence 명령이 실행되면 helper는 현재 CPU가 binding된 Gemmini의 Worker가 모든 작업을 마칠 때까지(`has_work == false && worker_running == false`) `worker_cond`에서 대기한 후, **CPU-Gemmini binding을 해제**한다. binding이 없는 상태의 Fence는 NOP으로 처리된다.

이 때문에 main 브랜치(단순 RISC-V `fence` 명령어 삽입 방식)와는 `gemmini_fence()`의 구현이 달라, **빌드되는 QEMU에 맞는 `include/gemmini.h` API 파일을 사용해야 한다.** main용 API를 본 버전에 사용하면 segmentation fault를, 본 버전의 API를 main에 사용하면 Illegal Instruction을 만나게 된다.

이와 별개로 upstream API와의 호환을 위한 `flush` 명령어(funct7=`0b0000111`)가 존재하며, 현재는 no-op으로 처리된다.

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

복합 연산 명령어는 `loop_ws`, `gemv_loop_ws`, `ternary_gemm_auto`, `loop_conv_ws`와 같이 단위 연산을 감싸 큰 단위의 연산을 진행하는 Wrapper 명령어들이다.

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

### Loop_conv_ws

`Loop_conv_ws`는 Convolution 연산을 위한 wrapper 명령어로, 6개의 config 명령어(funct7=`0b0010000`~`0b0010101`)로 batch/channel/kernel/padding/stride 등의 인자를 `GemminiState`의 `lcw_*` 필드에 설정한 후, 실행 명령어(funct7=`0b0001111`)로 연산을 수행한다.

다른 wrapper들과 달리 Spad 타일링을 거치지 않고, **NHWC 레이아웃의 입력/가중치/출력을 콜백을 통해 DRAM에서 직접 읽고 쓰는 direct convolution**으로 구현되어 있다. `no_bias`, `wrot180`(가중치 180도 회전), weight transpose 플래그와 Activation을 지원한다. config에서 생략되거나 placeholder 값이 들어오는 인자들(in_col_dim, in/weight/out stride 등)은 Spike/npu-opt의 관례에 맞추어 fallback 값으로 보정한다.

DRAM에 직접 접근하는 명령어이므로 [명령어 전달 과정](#명령어-전달-과정-instruction-flow)에서 설명한 vCPU 동기 대기 대상에 포함된다. 직사각형(non-square) Convolution의 일부 케이스에서 결과가 부정확한 알려진 제한이 있다.

## 프로파일링 (Profiling)

프로파일링을 위한 최소한의 기능을 지원한다.
- Qemu_log를 통해 명령어의 enqueue, Worker의 실행 시작/종료, Fence 대기/해제, Gemmini binding 등 실행 흐름을 확인할 수 있다.
- `q-vela/qemu/util/qemu-thread-posix.c`의 로그를 통해 각 vCPU와 Gemmini Worker의 Host Thread ID(TID)를 확인할 수 있으며, 이는 [pin.sh](#실행-launch)의 입력으로 사용된다.
- `gemmini_trace_init`을 통해 trace 변수들을 초기화할 수 있다. 현재는 `total_reads_in_bytes`와 `total_writes_in_bytes`를 지원한다.
- `gemmini_trace_dump`를 통해 trace 변수들을 Qemu_log에 출력할 수 있다. 현재는 위에서 언급한 바와 같이 init부터 dump까지의 Memory Read/Write를 확인할 수 있다.

### Heavy Compute-Only Workload

스케줄링/쓰레드 pinning 실험에서 메모리 접근 없이 순수 연산 부하만 발생시키기 위한 전용 명령어(funct7=`0b0111101`)를 지원한다. rs1으로 전달된 반복 횟수만큼 Worker Thread에서 scalar 연산과 pe_state 갱신을 반복하며, 게스트 메모리에는 접근하지 않는다.

## N-Core-M-Gemmini Structure

QEMU에서는 기본적으로 Multicore를 지원하며(smp옵션을 통해 설정), Gemmini Device도 부팅 시에 개수와 ID를 설정할 수 있기에 N-Core-M-Gemmini구조를 테스트할 수 있다. 본 버전에서는 각 Gemmini가 독립적인 Worker Thread로 실행되므로, tcg multithreading 및 쓰레드 pinning과 함께 사용하면 **실질적인 병렬 실행**이 가능하다.

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

### RISCV_CPU_GET_GEMMINI (Scheduling Policy)

RoCC 명령어가 실행되면, trans 함수를 거쳐 `gemmini_helper.c`의 helper함수(`helper_rocc_exec`)가 호출된다. 실행할 Gemmini를 정하는 스케줄링 정책으로 MODULO(cpu_index % gemmini 수, 결정적 1:1 매핑), RANDOM(명령어마다 무작위 — stateless 연산 전용), IDLE_FIRST의 세 가지가 구현되어 있으며, **현재 helper는 IDLE_FIRST 정책을 사용**한다.

IDLE_FIRST 정책은 CPU별 binding 테이블(`cpu_gemmini_binding`)을 유지한다. 하나의 연산은 config → mvin → compute와 같이 여러 RoCC 명령어에 걸쳐 이루어지므로, **한 연산에 속하는 모든 명령어는 같은 Gemmini로 전달되어야 한다.** 이를 위해:
1. binding이 없는 CPU에서 명령어가 오면, trylock으로 idle 상태인 Gemmini를 찾아 binding한다.
2. 모든 Gemmini가 바쁘다면 MODULO 방식으로 fallback하여 binding한 후 해당 Gemmini가 비기를 대기한다.
3. 이후 명령어들은 binding된 Gemmini로 전달되며, binding은 [Fence](#gemmini-fence)가 실행될 때 해제된다.

단, binding 테이블은 cpu_index 기준이므로 **연산 도중 게스트 쓰레드가 다른 코어로 migration되면 정확성이 깨질 수 있다.** 엄밀한 정확성이 필요하면 게스트에서 `sched_setaffinity`(taskset)로 쓰레드를 고정해야 한다. 또한 Gemmini를 하나도 부착하지 않고 RoCC 명령어를 실행하면 등록된 device가 없어 명령이 무시된다.

### Worker Thread Synchronization

Gemmini가 한 번에 하나의 연산만 수행하도록, 명령어의 enqueue와 실행 상태 확인은 각 Gemmini의 `worker_lock`(mutex)과 `worker_cond`(condition variable)로 보호된다. helper는 `worker_lock`을 잡은 채로 이전 작업의 완료를 확인(`has_work`/`worker_running`)한 후 명령을 enqueue하고, Worker Thread는 실행 전후로 상태 플래그를 갱신하며 `worker_cond`를 broadcast하여 대기 중인 vCPU를 깨운다. (`GemminiState`의 `gemmini_lock`은 기존 동기 방식에서 사용하던 레거시 필드이다.)

### Gemmini Manager

Gemmini manager는 여러 대의 Gemmini를 관리하기 위한 역할로, QOM을 통해 gemmini가 생성될 때마다 `gemmini_registry_add`를 통해 registry에 추가한다. 이외에도 `gemmini_registry_delete`, `gemmini_registry_get`, `gemmini_registry_count` 함수를 이용해 지정된 ID의 gemmini를 registry에서 제거하거나, 반환하거나, 전체 개수를 조회할 수 있다.

## 부록 (Appendix)

직접적으로 Q-Vela Gemmini의 구현에 관한 내용은 아니지만, Gemmini 자체 혹은 QEMU자체에 관하여 Q-Vela Gemmini와 관련 있는 내용들을 적어둔 챕터이다.

### WS(Weight Stationary)와 OS(Output Stationary)

WS는 Weight Stationary 모드로 PE가 연산을 할 때에 Weight에 해당하는 B행렬을 고정하여 계속 사용할 수 있도록 반복문에서 i가 가장 inner loop이 되는 형태이며 Compute 시에 pe_state에 weight를 유지한다.

OS는 Output Stationary 모드로 B를 재사용하는 대신 Output이 나가기 전에 모든 연산이 완료되도록 k가 가장 inner loop이며, pe_state에 중간합 결과를 유지한다.

구현을 목표로 한 loop_ws, gemv_loop_ws, ternary_gemm_auto 모두 WS 방식만을 사용하는 연산들이기 때문에, Compute_gemv와 compute_ternary 함수에서 **OS모드가 사용하는 경로는 예외(exit)로 처리**하였다.
