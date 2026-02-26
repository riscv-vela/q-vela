#include <stdint.h>
#include <stdio.h>


int main() {
    uint64_t D_stride = 16;
    uint64_t C_stride = 64;

    register uint64_t D_st asm("x10") = D_stride;
    register uint64_t C_st asm("x11") = C_stride;

    __asm__ volatile(
        ".word 0x38B5307B\n"
        :
        : "r"(D_st), "r"(C_st)

    );

    printf("D_stride = %lu, C_stride = %lu\n", D_stride, C_stride);
    return 0;
}
