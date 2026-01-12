#include <stdint.h>
#include <stdio.h>

int8_t A[16 * 16] __attribute__((aligned(64)));  // DIM=16, int8_t
int8_t B[16 * 16] __attribute__((aligned(64)));

int main() {

    // RoCC 명령어 실행 (func7 = 0011001 → k_gemv_loop_ws_addrs_ab)
    register uint64_t a_addr __asm__("x10")=(uintptr_t)A;
    register uint64_t b_addr __asm__("x11")=(uintptr_t)B;
    __asm__ volatile (
        ".word 0x32B5307B\n"
        :
        :"r"(a_addr), "r"(b_addr)
    );
    printf("Qemu_Dram_A : %ld\n",(uintptr_t)A);
    printf("Qemu_Dram_B : %ld\n",(uintptr_t)B);
    return 0;
}
