#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "rocc_api.h"

#define T_I 1
#define T_K (DIM + 3)
#define T_J (2 * DIM + 1)

#define I_PAD (((T_I + DIM - 1) / DIM) * DIM)
#define K_PAD (((T_K + DIM - 1) / DIM) * DIM)
#define J_PAD (((T_J + DIM - 1) / DIM) * DIM)

#define IB (I_PAD / DIM)
#define KB (K_PAD / DIM)
#define JB (J_PAD / DIM)

#define MVIN_SCALE_IDENTITY 1.0
#define ACC_SCALE_IDENTITY 1.0
#define NO_ACTIVATION 0

typedef float acc_scale_t;

static elem_t A[T_I][T_K] row_align(1);
static elem_t B[T_K][T_J] row_align(1);
static elem_t B_T[T_J][T_K] row_align(1);
static elem_t B_reordered[JB * DIM][KB * DIM] row_align(1);
static elem_t B_T_reordered[KB * DIM][JB * DIM] row_align(1);
static elem_t B_reordered_ref[JB * DIM][KB * DIM] row_align(1);

static elem_t C_hw[T_I][T_J] row_align(1);
static elem_t C_ref[T_I][T_J] row_align(1);

static acc_t Bias[T_I][T_J] row_align_acc(1);

static void reorder_K(elem_t *dst, elem_t *src, int stride, int row_tile, int col_tile) {
    size_t out = 0;
    for (size_t jb = 0; jb < (size_t)col_tile; ++jb) {
        for (size_t k = 0; k < DIM; ++k) {
            for (size_t kb = 0; kb < (size_t)row_tile; ++kb) {
                for (size_t j = 0; j < DIM; ++j) {
                    const size_t k_idx = kb * DIM + k;
                    const size_t j_idx = jb * DIM + j;
                    dst[out++] = (k_idx < T_K && j_idx < T_J)
                        ? src[k_idx * (size_t)stride + j_idx]
                        : 0;
                }
            }
        }
    }
}

static void reorder_K_from_transposed(elem_t *dst, elem_t *src, int stride, int row_tile, int col_tile) {
    const size_t dst_stride = (size_t)col_tile * DIM;
    for (size_t kb = 0; kb < (size_t)row_tile; ++kb) {
        for (size_t j = 0; j < DIM; ++j) {
            for (size_t jb = 0; jb < (size_t)col_tile; ++jb) {
                for (size_t k = 0; k < DIM; ++k) {
                    const size_t k_idx = kb * DIM + k;
                    const size_t j_idx = jb * DIM + j;
                    const size_t dst_row = kb * DIM + j;
                    const size_t dst_col = jb * DIM + k;
                    dst[dst_row * dst_stride + dst_col] = (k_idx < T_K && j_idx < T_J)
                        ? src[j_idx * (size_t)stride + k_idx]
                        : 0;
                }
            }
        }
    }
}

static void reorder_K_ref(elem_t *dst, elem_t *src, int stride, int row_tile, int col_tile) {
    for (size_t jb = 0; jb < (size_t)col_tile; ++jb) {
        for (size_t kb = 0; kb < (size_t)row_tile; ++kb) {
            for (size_t k = 0; k < DIM; ++k) {
                for (size_t j = 0; j < DIM; ++j) {
                    size_t out = (((jb * DIM + k) * (size_t)row_tile + kb) * DIM + j);
                    const size_t k_idx = kb * DIM + k;
                    const size_t j_idx = jb * DIM + j;
                    dst[out] = (k_idx < T_K && j_idx < T_J)
                        ? src[k_idx * (size_t)stride + j_idx]
                        : 0;
                }
            }
        }
    }
}

static void fill_inputs_pattern(void) {
    for (size_t k = 0; k < T_K; ++k) {
        A[0][k] = (elem_t)((k % 4) + 1);
    }

    for (size_t k = 0; k < T_K; ++k) {
        for (size_t j = 0; j < T_J; ++j) {
            B[k][j] = (elem_t)((((int)k + 2 * (int)j) % 5) == 0 ? 1 : 0);
        }
    }
}

static void fill_inputs_random(unsigned seed) {
    srand(seed);
    for (size_t k = 0; k < T_K; ++k) {
        A[0][k] = (elem_t)((rand() % 11) - 5);
    }

    for (size_t k = 0; k < T_K; ++k) {
        for (size_t j = 0; j < T_J; ++j) {
            B[k][j] = (elem_t)((rand() % 11) - 5);
        }
    }
}

static void fill_transposed_B(void) {
    for (size_t j = 0; j < T_J; ++j) {
        for (size_t k = 0; k < T_K; ++k) {
            B_T[j][k] = B[k][j];
        }
    }
}

static void fill_bias_pattern(void) {
    for (size_t j = 0; j < T_J; ++j) {
        Bias[0][j] = (acc_t)(j % 3); 
    }
}

static void fill_bias_random(unsigned seed) {
    srand(seed);
    for (size_t j = 0; j < T_J; ++j) {
        Bias[0][j] = (acc_t)((rand() % 21) - 10);
    }
}

static void cpu_ref_gemv(bool use_bias) {
    memset(C_ref, 0, sizeof(C_ref));
    for (size_t j = 0; j < T_J; ++j) {
        int32_t sum = 0;
        for (size_t k = 0; k < T_K; ++k) {
            sum += (int32_t)A[0][k] * (int32_t)B[k][j];
        }
        if (use_bias) sum += (int32_t)Bias[0][j];
        C_ref[0][j] = (elem_t)sum;
    }
}

static int assert_equal_vectors(const elem_t *got, const elem_t *exp, size_t n, const char *tag) {
    for (size_t i = 0; i < n; ++i) {
        if (got[i] != exp[i]) {
            printf("[FAIL] %s mismatch at idx=%zu got=%d exp=%d\n",
                   tag, i, (int)got[i], (int)exp[i]);
            return -1;
        }
    }
    printf("[PASS] %s\n", tag);
    return 0;
}

static void print_vector(const char *label, const elem_t *vec, size_t n) {
    printf("%s [", label);
    for (size_t i = 0; i < n; ++i) {
        printf("%d", (int)vec[i]);
        if (i + 1 < n) {
            printf(", ");
        }
    }
    printf("]\n");
}

static void print_case_outputs(const char *case_name) {
    printf("\n[CASE] %s\n", case_name);
    print_vector("  HW :", (const elem_t *)&C_hw[0][0], T_J);
    print_vector("  REF:", (const elem_t *)&C_ref[0][0], T_J);
}

static int run_gemv_hw(bool use_bias, elem_t *B_hw, uint64_t B_stride, bool transpose_B) {
    const size_t pad_I = I_PAD - T_I;
    const size_t pad_K = K_PAD - T_K;
    const size_t pad_J = J_PAD - T_J;

    memset(C_hw, 0, sizeof(C_hw));

    gemmini_fence();
    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, transpose_B);
    gemmini_extended_config_st(T_J * sizeof(elem_t), NO_ACTIVATION, ACC_SCALE_IDENTITY);

    gemmini_extended3_config_ld(T_K * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(B_stride * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(T_J * sizeof(acc_t), MVIN_SCALE_IDENTITY, false, 2);

    gemv_loop_ws(
        IB, JB, KB,
        pad_I, pad_J, pad_K,
        (elem_t *)A,
        B_hw,
        use_bias ? (void *)&Bias[0][0] : NULL,
        (elem_t *)C_hw,
        T_K,            // A_stride
        B_stride,
        T_J,            // D_stride
        T_J,            // C_stride
        false, transpose_B, // transpose flags
        false, false,   // full_C, low_D
        false,          // ex_accumulate
        NO_ACTIVATION,
        0, 0, 0,        // spad ids
        false           // is_resadd
    );

    gemmini_fence();
    return 0;
}

static int test_reorder_k_layout(void) {
    fill_inputs_pattern();
    memset(B_reordered, 0, sizeof(B_reordered));
    memset(B_reordered_ref, 0, sizeof(B_reordered_ref));

    reorder_K((elem_t *)B_reordered, (elem_t *)B, T_J, KB, JB);
    reorder_K_ref((elem_t *)B_reordered_ref, (elem_t *)B, T_J, KB, JB);

    return assert_equal_vectors((elem_t *)B_reordered, (elem_t *)B_reordered_ref,
                                (size_t)(JB * DIM * KB * DIM),
                                "reorder_K layout");
}

static int test_gemv_vector_A_no_bias(void) {
    if (T_I != 1) {
        printf("[FAIL] A must be a vector (I==1)\n");
        return -1;
    }

    fill_inputs_pattern();
    reorder_K((elem_t *)B_reordered, (elem_t *)B, T_J, KB, JB);

    cpu_ref_gemv(false);
    run_gemv_hw(false, (elem_t *)B_reordered, KB * DIM, false);
    print_case_outputs("gemv_loop_ws vector-A no-bias");

    return assert_equal_vectors((elem_t *)&C_hw[0][0], (elem_t *)&C_ref[0][0], T_J,
                                "gemv_loop_ws vector-A no-bias");
}

static int test_gemv_vector_A_with_bias(void) {
    if (T_I != 1) {
        printf("[FAIL] A must be a vector (I==1)\n");
        return -1;
    }

    fill_inputs_pattern();
    fill_bias_pattern();
    reorder_K((elem_t *)B_reordered, (elem_t *)B, T_J, KB, JB);

    cpu_ref_gemv(true);
    run_gemv_hw(true, (elem_t *)B_reordered, KB * DIM, false);
    print_case_outputs("gemv_loop_ws vector-A with-bias");

    return assert_equal_vectors((elem_t *)&C_hw[0][0], (elem_t *)&C_ref[0][0], T_J,
                                "gemv_loop_ws vector-A with-bias");
}

static int test_gemv_vector_A_transposed_B(void) {
    if (T_I != 1) {
        printf("[FAIL] A must be a vector (I==1)\n");
        return -1;
    }

    fill_inputs_pattern();
    fill_transposed_B();
    memset(B_T_reordered, 0, sizeof(B_T_reordered));
    reorder_K_from_transposed((elem_t *)B_T_reordered, (elem_t *)B_T, T_K, KB, JB);

    cpu_ref_gemv(false);
    run_gemv_hw(false, (elem_t *)B_T_reordered, JB * DIM, true);
    print_case_outputs("gemv_loop_ws vector-A transposed-B");

    return assert_equal_vectors((elem_t *)&C_hw[0][0], (elem_t *)&C_ref[0][0], T_J,
                                "gemv_loop_ws vector-A transposed-B");
}

static int test_gemv_vector_A_random_bias_transposed_B(void) {
    if (T_I != 1) {
        printf("[FAIL] A must be a vector (I==1)\n");
        return -1;
    }

    fill_inputs_random(12345);
    fill_bias_random(98765);
    fill_transposed_B();
    memset(B_T_reordered, 0, sizeof(B_T_reordered));
    reorder_K_from_transposed((elem_t *)B_T_reordered, (elem_t *)B_T, T_K, KB, JB);

    cpu_ref_gemv(true);
    run_gemv_hw(true, (elem_t *)B_T_reordered, JB * DIM, true);
    print_case_outputs("gemv_loop_ws vector-A random+bias+transposed-B");

    return assert_equal_vectors((elem_t *)&C_hw[0][0], (elem_t *)&C_ref[0][0], T_J,
                                "gemv_loop_ws vector-A random+bias+transposed-B");
}

#define TS_I 64
#define TS_J 64
#define TS_K 64
#define TS_J_PACKED ((TS_J + 3) / 4)

#define TR_I 13
#define TR_J 36
#define TR_K 21
#define TR_J_PACKED ((TR_J + 3) / 4)

#define TR19_I 19
#define TR19_J 36
#define TR19_K 21
#define TR19_J_PACKED ((TR19_J + 3) / 4)

#define TRL_I 19
#define TRL_J 72
#define TRL_K 21
#define TRL_J_PACKED ((TRL_J + 3) / 4)

static elem_t TS_A[TS_I][TS_K] row_align(1);
static int8_t TS_B_logical[TS_K][TS_J];
static elem_t TS_B_packed[TS_K][TS_J_PACKED] row_align(1);
static elem_t TS_C_hw[TS_I][TS_J] row_align(1);
static elem_t TS_C_ref[TS_I][TS_J] row_align(1);

static elem_t TR_A[TR_I][TR_K] row_align(1);
static int8_t TR_B_logical[TR_K][TR_J];
static elem_t TR_B_packed[TR_K][TR_J_PACKED] row_align(1);
static elem_t TR_C_hw[TR_I][TR_J] row_align(1);
static elem_t TR_C_ref[TR_I][TR_J] row_align(1);
static acc_t TR_D[TR_I][TR_J] row_align_acc(1);

static elem_t TR19_A[TR19_I][TR19_K] row_align(1);
static int8_t TR19_B_logical[TR19_K][TR19_J];
static elem_t TR19_B_packed[TR19_K][TR19_J_PACKED] row_align(1);
static elem_t TR19_C_hw[TR19_I][TR19_J] row_align(1);
static elem_t TR19_C_ref[TR19_I][TR19_J] row_align(1);
static acc_t TR19_D[TR19_I][TR19_J] row_align_acc(1);

static elem_t TRL_A[TRL_I][TRL_K] row_align(1);
static int8_t TRL_B_logical[TRL_K][TRL_J];
static elem_t TRL_B_packed[TRL_K][TRL_J_PACKED] row_align(1);
static elem_t TRL_C_hw[TRL_I][TRL_J] row_align(1);
static elem_t TRL_C_ref[TRL_I][TRL_J] row_align(1);
static acc_t TRL_D[TRL_I][TRL_J] row_align_acc(1);

static uint8_t tg_ternary_to_2bit(int8_t v) {
    if (v == -1) return 0b11;
    if (v == 0) return 0b00;
    return 0b01;
}

static void tg_print_first_last_rows(const char *name, const elem_t *hw, const elem_t *ref,
                                     size_t I, size_t J) {
    const size_t first = 0;
    const size_t last = I - 1;

    printf("\n[CASE] %s\n", name);

    printf("  HW  row%zu: [", first);
    for (size_t j = 0; j < J; ++j) {
        printf("%d", (int)hw[first * J + j]);
        if (j + 1 < J) printf(", ");
    }
    printf("]\n");

    printf("  REF row%zu: [", first);
    for (size_t j = 0; j < J; ++j) {
        printf("%d", (int)ref[first * J + j]);
        if (j + 1 < J) printf(", ");
    }
    printf("]\n");

    printf("  HW  row%zu: [", last);
    for (size_t j = 0; j < J; ++j) {
        printf("%d", (int)hw[last * J + j]);
        if (j + 1 < J) printf(", ");
    }
    printf("]\n");

    printf("  REF row%zu: [", last);
    for (size_t j = 0; j < J; ++j) {
        printf("%d", (int)ref[last * J + j]);
        if (j + 1 < J) printf(", ");
    }
    printf("]\n");
}

static void tg_pack_B(size_t K, size_t J, const int8_t *B_logical, elem_t *B_packed) {
    const size_t J_packed = (J + 3) / 4;
    for (size_t k = 0; k < K; ++k) {
        for (size_t jp = 0; jp < J_packed; ++jp) {
            uint8_t packed = 0;
            for (size_t lane = 0; lane < 4; ++lane) {
                const size_t j = jp * 4 + lane;
                const int8_t v = (j < J) ? B_logical[k * J + j] : 0;
                packed |= (tg_ternary_to_2bit(v) << (lane * 2));
            }
            B_packed[k * J_packed + jp] = (elem_t)packed;
        }
    }
}

static int run_ternary_square_64(void) {
    const size_t I = TS_I, J = TS_J, K = TS_K;

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TS_A[i][k] = (elem_t)((i + k) % 3 - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TS_B_logical[k][j] = (int8_t)(((k + 2 * j) % 3) - 1);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TS_B_logical, (elem_t *)TS_B_packed);

    memset(TS_C_ref, 0, sizeof(TS_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = 0;
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TS_A[i][k] * (int32_t)TS_B_logical[k][j];
            }
            TS_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TS_C_hw, 0, sizeof(TS_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TS_A,
        (const elem_t *)TS_B_packed,
        NULL,
        (elem_t *)TS_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_square_64",
                             (const elem_t *)TS_C_hw,
                             (const elem_t *)TS_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TS_C_hw[i][j] != TS_C_ref[i][j]) {
                printf("[FAIL] ternary_square_64 mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TS_C_hw[i][j], (int)TS_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_square_64\n");
    return 0;
}

static int run_ternary_rect(void) {
    const size_t I = TR_I, J = TR_J, K = TR_K;

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TR_A[i][k] = (elem_t)((i + 2 * k) % 3 - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TR_B_logical[k][j] = (int8_t)(((2 * k + j) % 3) - 1);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TR_B_logical, (elem_t *)TR_B_packed);

    memset(TR_C_ref, 0, sizeof(TR_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = 0;
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TR_A[i][k] * (int32_t)TR_B_logical[k][j];
            }
            TR_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TR_C_hw, 0, sizeof(TR_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TR_A,
        (const elem_t *)TR_B_packed,
        NULL,
        (elem_t *)TR_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_rect",
                             (const elem_t *)TR_C_hw,
                             (const elem_t *)TR_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TR_C_hw[i][j] != TR_C_ref[i][j]) {
                printf("[FAIL] ternary_rect mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TR_C_hw[i][j], (int)TR_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_rect\n");
    return 0;
}

static int run_ternary_rect_random(void) {
    const size_t I = TR_I, J = TR_J, K = TR_K;
    srand(20260221);

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TR_A[i][k] = (elem_t)((rand() % 3) - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TR_B_logical[k][j] = (int8_t)((rand() % 3) - 1);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TR_B_logical, (elem_t *)TR_B_packed);

    memset(TR_C_ref, 0, sizeof(TR_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = 0;
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TR_A[i][k] * (int32_t)TR_B_logical[k][j];
            }
            TR_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TR_C_hw, 0, sizeof(TR_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TR_A,
        (const elem_t *)TR_B_packed,
        NULL,
        (elem_t *)TR_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_rect_random",
                             (const elem_t *)TR_C_hw,
                             (const elem_t *)TR_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TR_C_hw[i][j] != TR_C_ref[i][j]) {
                printf("[FAIL] ternary_rect_random mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TR_C_hw[i][j], (int)TR_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_rect_random\n");
    return 0;
}

static int run_ternary_rect_random_bias(void) {
    const size_t I = TR_I, J = TR_J, K = TR_K;
    srand(20260222);

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TR_A[i][k] = (elem_t)((rand() % 3) - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TR_B_logical[k][j] = (int8_t)((rand() % 3) - 1);
        }
    }
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            TR_D[i][j] = (acc_t)((rand() % 11) - 5);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TR_B_logical, (elem_t *)TR_B_packed);

    memset(TR_C_ref, 0, sizeof(TR_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = (int32_t)TR_D[i][j];
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TR_A[i][k] * (int32_t)TR_B_logical[k][j];
            }
            TR_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TR_C_hw, 0, sizeof(TR_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TR_A,
        (const elem_t *)TR_B_packed,
        (const acc_t *)TR_D,
        (elem_t *)TR_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_rect_random_bias",
                             (const elem_t *)TR_C_hw,
                             (const elem_t *)TR_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TR_C_hw[i][j] != TR_C_ref[i][j]) {
                printf("[FAIL] ternary_rect_random_bias mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TR_C_hw[i][j], (int)TR_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_rect_random_bias\n");
    return 0;
}

static int run_ternary_rect_random_bias_row19(void) {
    const size_t I = TR19_I, J = TR19_J, K = TR19_K;
    srand(20260223);

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TR19_A[i][k] = (elem_t)((rand() % 3) - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TR19_B_logical[k][j] = (int8_t)((rand() % 3) - 1);
        }
    }
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            TR19_D[i][j] = (acc_t)((rand() % 11) - 5);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TR19_B_logical, (elem_t *)TR19_B_packed);

    memset(TR19_C_ref, 0, sizeof(TR19_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = (int32_t)TR19_D[i][j];
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TR19_A[i][k] * (int32_t)TR19_B_logical[k][j];
            }
            TR19_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TR19_C_hw, 0, sizeof(TR19_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TR19_A,
        (const elem_t *)TR19_B_packed,
        (const acc_t *)TR19_D,
        (elem_t *)TR19_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_rect_random_bias_row19",
                             (const elem_t *)TR19_C_hw,
                             (const elem_t *)TR19_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TR19_C_hw[i][j] != TR19_C_ref[i][j]) {
                printf("[FAIL] ternary_rect_random_bias_row19 mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TR19_C_hw[i][j], (int)TR19_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_rect_random_bias_row19\n");
    return 0;
}

static int run_ternary_rect_random_bias_row19_j72(void) {
    const size_t I = TRL_I, J = TRL_J, K = TRL_K;
    srand(20260224);

    for (size_t i = 0; i < I; ++i) {
        for (size_t k = 0; k < K; ++k) {
            TRL_A[i][k] = (elem_t)((rand() % 3) - 1);
        }
    }
    for (size_t k = 0; k < K; ++k) {
        for (size_t j = 0; j < J; ++j) {
            TRL_B_logical[k][j] = (int8_t)((rand() % 3) - 1);
        }
    }
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            TRL_D[i][j] = (acc_t)((rand() % 11) - 5);
        }
    }
    tg_pack_B(K, J, (const int8_t *)TRL_B_logical, (elem_t *)TRL_B_packed);

    memset(TRL_C_ref, 0, sizeof(TRL_C_ref));
    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            int32_t sum = (int32_t)TRL_D[i][j];
            for (size_t k = 0; k < K; ++k) {
                sum += (int32_t)TRL_A[i][k] * (int32_t)TRL_B_logical[k][j];
            }
            TRL_C_ref[i][j] = (elem_t)sum;
        }
    }

    memset(TRL_C_hw, 0, sizeof(TRL_C_hw));

    gemmini_fence();
    ternary_gemm_auto(
        I, J, K,
        (const elem_t *)TRL_A,
        (const elem_t *)TRL_B_packed,
        (const acc_t *)TRL_D,
        (elem_t *)TRL_C_hw,
        K,
        J,
        J,
        J,
        MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        0,
        false,
        false,
        false,
        false
    );
    gemmini_fence();

    tg_print_first_last_rows("ternary_rect_random_bias_row19_j72",
                             (const elem_t *)TRL_C_hw,
                             (const elem_t *)TRL_C_ref,
                             I, J);

    for (size_t i = 0; i < I; ++i) {
        for (size_t j = 0; j < J; ++j) {
            if (TRL_C_hw[i][j] != TRL_C_ref[i][j]) {
                printf("[FAIL] ternary_rect_random_bias_row19_j72 mismatch (%zu,%zu): hw=%d ref=%d\n",
                       i, j, (int)TRL_C_hw[i][j], (int)TRL_C_ref[i][j]);
                return -1;
            }
        }
    }
    printf("[PASS] ternary_rect_random_bias_row19_j72\n");
    return 0;
}

int main(void) {
    int fails = 0;

    fails += (test_reorder_k_layout() != 0);
    fails += (test_gemv_vector_A_no_bias() != 0);
    fails += (test_gemv_vector_A_with_bias() != 0);
    fails += (test_gemv_vector_A_transposed_B() != 0);
    fails += (test_gemv_vector_A_random_bias_transposed_B() != 0);

    fails += (run_ternary_square_64() != 0);
    fails += (run_ternary_rect() != 0);
    fails += (run_ternary_rect_random() != 0);
    fails += (run_ternary_rect_random_bias() != 0);
    fails += (run_ternary_rect_random_bias_row19() != 0);
    fails += (run_ternary_rect_random_bias_row19_j72() != 0);

    if (fails == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    } else {
        printf("\nTests failed: %d\n", fails);
        return 1;
    }
}
