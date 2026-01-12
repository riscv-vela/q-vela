#include <stdint.h>
#include <stdio.h>

int main() {
    uint64_t rs1 = 0;
    uint64_t rs2 = 0;

    // ex_accumulate=1, full_C=0, low_D=1, act=2(RELU), a/b/c_spad_id=1
    rs1 |= 1;              // ex_accumulate
    rs1 |= (0 << 1);       // full_C
    rs1 |= (1 << 2);       // low_D
    rs1 |= (2 << 8);       // act = RELU
    rs1 |= (1 << 16);      // c_spad_id
    rs1 |= (1 << 21);      // b_spad_id
    rs1 |= (1 << 24);      // a_spad_id

    // A_transpose=0, B_transpose=1, is_resadd=1
    rs2 |= (0 << 0);       // A_transpose
    rs2 |= (1 << 1);       // B_transpose
    rs2 |= (1 << 2);       // is_resadd

    register uint64_t R1 asm("x10") = rs1;
    register uint64_t R2 asm("x11") = rs2;
    
    __asm__ volatile(
        ".word 0x2EB5307B\n"
        :
        : "r"(R1), "r"(R2)
    );

    return 0;
}
