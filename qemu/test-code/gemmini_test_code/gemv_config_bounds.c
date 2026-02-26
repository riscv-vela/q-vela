#include <stdint.h>

#define DIM 16

int main() {
    //A(45×32), B(32×50)
    uint16_t A_rows = 45;
    uint16_t A_cols = 32;
    uint16_t B_cols = 50;

    uint16_t I = (A_rows + DIM - 1) / DIM;
    uint16_t J = (A_cols + DIM - 1) / DIM;
    uint16_t K = (B_cols + DIM - 1) / DIM;

    uint16_t pad_I = I * DIM - A_rows;
    uint16_t pad_J = J * DIM - A_cols;
    uint16_t pad_K = K * DIM - B_cols;

    // rs1 = (pad_K << 32) | (pad_J << 16) | pad_I
    uint64_t rs1 = ((uint64_t)pad_K << 32) | ((uint64_t)pad_J << 16) | pad_I;

    // rs2 = (K << 32) | (J << 16) | I
    uint64_t rs2 = ((uint64_t)K << 32) | ((uint64_t)J << 16) | I;

    __asm__ volatile (
	"mv x10, %0\n"
	"mv x11, %1\n"
        ".word 0x30B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
    );

    return 0;
}
