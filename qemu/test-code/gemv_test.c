#include <stdio.h>
#include <stdbool.h>
#include "gemmini_api.h"

#define VEC_DIM 64
#define MAT_DIM_J 64
#define ACC_ROWS 512
#define relu 1

typedef int8_t elem_t;
#define row_align(x) __attribute__((aligned(64)))

#define A_row_stride VEC_DIM     // 1행짜리 벡터
#define B_row_stride MAT_DIM_J   // K행 × J열
#define C_row_stride MAT_DIM_J   // 결과는 1행 × J열

int main() {
    static elem_t A[1][VEC_DIM] row_align(1);        // 1 × K (벡터)
    static elem_t B[VEC_DIM][MAT_DIM_J] row_align(1); // K × J (행렬)
    static elem_t C[1][MAT_DIM_J] row_align(1);       // 1 × J (결과)

    // A = [1, 1, ..., 1]
    for (int k = 0; k < VEC_DIM; k++)
        A[0][k] = 1;

    // B = 2 everywhere
    for (int k = 0; k < VEC_DIM; k++)
        for (int j = 0; j < MAT_DIM_J; j++)
            B[k][j] = 1;

    // C 초기화
    for (int j = 0; j < MAT_DIM_J; j++)
        C[0][j] = 0;

    // 타일 설정
    size_t I = 1;
    size_t J = MAT_DIM_J;
    size_t K = VEC_DIM;

    int pad_I = (I % DIM == 0) ? 0 : DIM - (I % DIM);
    int pad_J = (J % DIM == 0) ? 0 : DIM - (J % DIM);
    int pad_K = (K % DIM == 0) ? 0 : DIM - (K % DIM);

    size_t I_t = (I + DIM - 1) / DIM;
    size_t J_t = (J + DIM - 1) / DIM;
    size_t K_t = (K + DIM - 1) / DIM;

    gemmini_loop_ws(
        I_t, J_t, K_t,
        pad_I, pad_J, pad_K,
        (int8_t*)A, (int8_t*)B, NULL, (int8_t*)C,
        A_row_stride, B_row_stride, 0, C_row_stride,
        /* A_transpose */ false,
        /* B_transpose */ false,
        /* full_C */ false,
        /* low_D */ false,
        /* ex_accumulate */ false,
        /* activation */ relu,
        0, 0,
        /* is_resadd */ true
    );

    // 결과 출력
    for(int i=0;i<64;i++)
    	printf("C[0][%d] = %d\n",i, C[0][i]);

    return 0;
}
