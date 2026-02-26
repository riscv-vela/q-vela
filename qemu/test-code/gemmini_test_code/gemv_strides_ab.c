#include <stdint.h>
#include <stdio.h>


int main() {
    uint64_t A_stride = 16;
    uint64_t B_stride = 64;

    register uint64_t A_st asm("x10") = A_stride;
    register uint64_t B_st asm("x11") = B_stride;

    __asm__ volatile(
        ".word 0x36B5307B\n"
        :
        : "r"(A_st), "r"(B_st)
    );

    printf("A_stride = %lu, B_stride = %lu\n", A_stride, B_stride);
    return 0;
}
