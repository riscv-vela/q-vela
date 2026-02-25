#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "rocc_api.h"

#define ACTIVATION NO_ACTIVATION
#define NO_BIAS 1
#define REPEATING_BIAS 0

#define A_TRANSPOSE 0
#define B_TRANSPOSE 0

#define MAT_DIM_I 64
#define MAT_DIM_J 64
#define MAT_DIM_K 64

#define A_STRIDE MAT_DIM_K //number of columns of A
#define B_STRIDE MAT_DIM_J //number of columns of B
#define D_STRIDE MAT_DIM_J //number of columns of D
#define C_STRIDE MAT_DIM_J //number of columns of C

void full_printMatrix(elem_t m[MAT_DIM_I][MAT_DIM_J]) {
    for (size_t i = 0; i < MAT_DIM_I; i++) {
        for (size_t j = 0; j < MAT_DIM_J; j++) {
            printf("%d ", m[i][j]);
        }
        printf("\n");
    }
}

static void init_mats_packed(elem_t A[MAT_DIM_I][MAT_DIM_K], elem_t B[MAT_DIM_K][MAT_DIM_J/4]){
    srand(time(NULL));

    for (size_t i = 0; i < MAT_DIM_I; i++) {
        for (size_t k = 0; k < MAT_DIM_K; k++) {
            A[i][k] = (elem_t)(rand() % 4);
        }
    }

    for (size_t k = 0; k < MAT_DIM_K; k++) {
        for (size_t j_packed = 0; j_packed < MAT_DIM_J/4; j_packed++) {
            uint8_t packed_val = 0;
            for(int i = 0; i < 4; i++) {
                elem_t ternary_val = rand() % 3 - 1;
                // elem_t ternary_val = j_packed == 0 ? 1 : 0;
                uint8_t two_bit_val = 0;
                if (ternary_val == -1) {
                    two_bit_val = 0b11;
                } else if (ternary_val == 0) {
                    two_bit_val = 0b00;
                } else if (ternary_val == 1) {
                    two_bit_val = 0b01;
                }
                packed_val |= ((two_bit_val & 0x03) << (i * 2));
            }
            B[k][j_packed] = packed_val;
        }
    }
}

int main(void){
#ifdef BAREMETAL
    if(mlockall(MCL_CURRENT | MCL_FUTURE) != 0){
        printf("mlockall failed!\n");
        exit(1);
    }
#endif
    
    static elem_t full_A[MAT_DIM_I][MAT_DIM_K] row_align(1);
    static elem_t full_B[MAT_DIM_K][MAT_DIM_J/4] row_align(1);
    static elem_t full_C[MAT_DIM_I][MAT_DIM_J] row_align(1);
    static acc_t full_D[MAT_DIM_I][MAT_DIM_J] row_align_acc(1);

    init_mats_packed(full_A, full_B);

    gemmini_fence();

    ternary_gemm_auto(MAT_DIM_I, MAT_DIM_J, MAT_DIM_K,
        (elem_t *)full_A, (elem_t *)full_B,
        NO_BIAS ? NULL : (acc_t *)full_D, (elem_t *)full_C,
        A_STRIDE, B_STRIDE,
        D_STRIDE, C_STRIDE,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        ACTIVATION, ACC_SCALE_IDENTITY, 0, REPEATING_BIAS,
        false, false, false);
    
    gemmini_fence();

    full_printMatrix(full_C);

    return 0;
}