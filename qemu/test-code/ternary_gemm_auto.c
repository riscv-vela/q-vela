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

#define I_PAD (((MAT_DIM_I + DIM - 1) / DIM) * DIM)
#define K_PAD (((MAT_DIM_K + DIM - 1) / DIM) * DIM)
#define J_PAD (((MAT_DIM_J + DIM - 1) / DIM) * DIM)
#define V_PAD (((MAT_DIM_V + DIM - 1) / DIM) * DIM)

#define IB (I_PAD / DIM)
#define KB (K_PAD / DIM)
#define JB (J_PAD / DIM)

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

    const size_t pad_I = I_PAD - MAT_DIM_I;
	const size_t pad_K = K_PAD - MAT_DIM_K;
	const size_t pad_J = J_PAD - MAT_DIM_J;

    gemmini_extended_config_ex(WS, ACTIVATION, 0, 1, false, false);
	gemmini_extended_config_st(MAT_DIM_J * sizeof(elem_t), ACTIVATION, ACC_SCALE_IDENTITY);

	gemmini_extended3_config_ld(MAT_DIM_K * sizeof(elem_t), MVIN_SCALE_IDENTITY,false,0);
	gemmini_extended3_config_ld(MAT_DIM_J/4 * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);					
	gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 2);

    ternary_gemm_auto(IB, JB, KB, pad_I, pad_J, pad_K,
        (elem_t *)full_A, (elem_t *)full_B,
        NO_BIAS ? NULL : (acc_t *)full_D, (elem_t *)full_C,
        A_STRIDE, B_STRIDE,
        D_STRIDE, C_STRIDE,
        false, false, false, false, false, ACTIVATION, 0, 0, 0, false);
    
    gemmini_fence();

    full_printMatrix(full_C);

    return 0;
}