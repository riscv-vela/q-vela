#include <stdint.h>
#include <stdio.h>

int8_t C[16 * 16] __attribute__((aligned(64)));  // DIM=16, int8_t
int8_t D[16 * 16] __attribute__((aligned(64)));

int main() {

    // RoCC 명령어 실행 (func7 = 0011001 → k_gemv_loop_ws_addrs_ab)
    register uint64_t d_addr __asm__("x10")=(uintptr_t)D;
    register uint64_t c_addr __asm__("x11")=(uintptr_t)C;
    __asm__ volatile (
        ".word 0x34B5307B\n"
        :
        :"r"(d_addr), "r"(c_addr)
    );
    printf("Qemu_Dram_D : %ld\n",(uintptr_t)D);
    printf("Qemu_Dram_C : %ld\n",(uintptr_t)C);
    return 0;
}
