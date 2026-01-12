#include <stdio.h>
#include "gemmini_api.h"
#include <stdbool.h>

#define MAT_DIM_I 64
#define MAT_DIM_J 64
#define ACC_ROWS 512
#define relu 1

typedef int8_t elem_t;
#define row_align(x) __attribute__((aligned(64)))

#define stride MAT_DIM_J
#define A_row_stride stride
#define B_row_stride stride
#define C_row_stride stride

int main() {
    static elem_t A[MAT_DIM_I][MAT_DIM_J] row_align(1);
    static elem_t B[MAT_DIM_I][MAT_DIM_J] row_align(1);
    static elem_t C[MAT_DIM_I][MAT_DIM_J] row_align(1);

    size_t tile_I = MAT_DIM_I;
    size_t tile_J = MAT_DIM_J;

    size_t total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                            (tile_J / DIM + (tile_J % DIM != 0));

    while (total_acc_rows > ACC_ROWS / 2) {
        if (tile_I >= tile_J || tile_J <= DIM)
            tile_I /= 2;
        else
            tile_J -= DIM;

        total_acc_rows = (tile_I / DIM + (tile_I % DIM != 0)) * DIM *
                         (tile_J / DIM + (tile_J % DIM != 0));
    }

    for (size_t i = 0; i < MAT_DIM_I; i += tile_I) {
        for (size_t j = 0; j < MAT_DIM_J; j += tile_J) {
            size_t I_tile = (i + tile_I <= MAT_DIM_I) ? tile_I : MAT_DIM_I - i;
            size_t J_tile = (j + tile_J <= MAT_DIM_J) ? tile_J : MAT_DIM_J - j;

            const elem_t *a = &A[i][0];
            const elem_t *b = &B[j][0];
            elem_t *c = &C[i][0];
	   
            int pad_I = (I_tile % DIM == 0) ? 0 : DIM - (I_tile % DIM);
            int pad_J = (J_tile % DIM == 0) ? 0 : DIM - (J_tile % DIM);

	    I_tile = (I_tile%DIM == 0) ? (int)(I_tile/DIM) : (int)(I_tile/DIM) + 1;
	    J_tile = (J_tile%DIM == 0) ? (int)(J_tile/DIM) : (int)(J_tile/DIM) + 1;
            gemmini_loop_ws(I_tile, J_tile, 0,
                            pad_I, pad_J, 0,
                            (int8_t*)a, (int8_t*)b, NULL, (int8_t*)c,
                            A_row_stride, B_row_stride, 0, C_row_stride,
                            false, false, false, false, false, relu,
                            0, 0, true);
        }
    }

    return 0;
}
