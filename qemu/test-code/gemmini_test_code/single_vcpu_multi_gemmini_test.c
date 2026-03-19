/*
 * Single vCPU → Multiple Gemmini Test (M:N binding, IDLE_FIRST policy)
 *
 * One guest thread (pinned to vCPU 0) submits independent GEMM tiles to
 * two different Gemmini accelerators back-to-back, then drains both with
 * a single gemmini_fence().
 *
 * Flow per repetition:
 *   1. Configure + compute on Gemmini A  (auto-selected, IDLE_FIRST)
 *   2. gemmini_release()                 → release A, keep in bitmask
 *   3. Configure + compute on Gemmini B  (IDLE_FIRST picks a different one)
 *   4. gemmini_release()                 → release B (optional, tidy)
 *   5. gemmini_fence()                   → drain both A and B, clear bitmask
 *
 * Compile (inside the guest):
 *   riscv64-linux-gnu-gcc -O2 -pthread -o single_vcpu_multi_gemmini_test \
 *       single_vcpu_multi_gemmini_test.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "include/gemmini.h"

#define N_REPEAT      10000  /* repetitions per run */
#define NO_ACTIVATION 0

typedef float acc_scale_t;

/* Use DIM×DIM tiles (single tile per GEMM) */
#define G_I  DIM
#define G_K  DIM
#define G_J  DIM

/* Two independent GEMM problems, different data */
static elem_t A0[G_I][G_K] row_align(1);
static elem_t B0[G_K][G_J] row_align(1);
static elem_t C0_hw[G_I][G_J] row_align(1);
static elem_t C0_ref[G_I][G_J];

static elem_t A1[G_I][G_K] row_align(1);
static elem_t B1[G_K][G_J] row_align(1);
static elem_t C1_hw[G_I][G_J] row_align(1);
static elem_t C1_ref[G_I][G_J];

static void fill_matrices(void)
{
    for (int i = 0; i < G_I; i++)
        for (int k = 0; k < G_K; k++) {
            A0[i][k] = (elem_t)((i * G_K + k + 1) % 5);
            A1[i][k] = (elem_t)((i * G_K + k + 3) % 5);
        }
    for (int k = 0; k < G_K; k++)
        for (int j = 0; j < G_J; j++) {
            B0[k][j] = (elem_t)((k + j * 2)     % 3 - 1);
            B1[k][j] = (elem_t)((k + j * 2 + 1) % 3 - 1);
        }
}

static void cpu_ref(elem_t A[G_I][G_K], elem_t B[G_K][G_J], elem_t C[G_I][G_J])
{
    for (int i = 0; i < G_I; i++)
        for (int j = 0; j < G_J; j++) {
            int32_t s = 0;
            for (int k = 0; k < G_K; k++)
                s += (int32_t)A[i][k] * (int32_t)B[k][j];
            C[i][j] = (elem_t)s;
        }
}

static int verify(const char *tag,
                  elem_t C_hw[G_I][G_J],
                  elem_t C_ref[G_I][G_J])
{
    for (int i = 0; i < G_I; i++)
        for (int j = 0; j < G_J; j++)
            if (C_hw[i][j] != C_ref[i][j]) {
                printf("[FAIL] %s [%d][%d]: hw=%d ref=%d\n",
                       tag, i, j, (int)C_hw[i][j], (int)C_ref[i][j]);
                return -1;
            }
    return 0;
}

/* Issue a full single-tile GEMM to whichever Gemmini is currently active */
static void issue_gemm(const elem_t *A, const elem_t *B, elem_t *C)
{
    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);
    gemmini_extended_config_st(G_J * sizeof(elem_t), NO_ACTIVATION, ACC_SCALE_IDENTITY);
    gemmini_extended3_config_ld(G_K * sizeof(elem_t),        MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(1   * DIM * sizeof(elem_t),  MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(G_J * sizeof(acc_t),         MVIN_SCALE_IDENTITY, false, 2);

    tiled_matmul_auto(
        G_I, G_J, G_K,
        A, B,
        NULL,   /* no bias */
        C,
        G_K,    /* stride_A */
        G_J,    /* stride_B */
        G_J,    /* stride_D */
        G_J,    /* stride_C */
        MVIN_SCALE_IDENTITY,
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        NO_ACTIVATION,
        ACC_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        false,  /* repeating_bias */
        false,  /* transpose A */
        false,  /* transpose B */
        false,  /* full_C */
        false,  /* low_D */
        0,      /* weightA */
        WS
    );
}

int main(void)
{
    /* Pin this thread to vCPU 0 */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    printf("=== Single vCPU Multi-Gemmini Test ===\n");
    printf("    vCPU 0 → two Gemminis, %d×%d GEMM × %d reps\n\n",
           G_I, G_J, N_REPEAT);

    fill_matrices();

    /* CPU reference (once) */
    cpu_ref(A0, B0, C0_ref);
    cpu_ref(A1, B1, C1_ref);

    /* Warm-up fence: ensure clean state */
    gemmini_fence();

    int fails = 0;

    for (int rep = 0; rep < N_REPEAT; rep++) {

        memset(C0_hw, 0, sizeof(C0_hw));
        memset(C1_hw, 0, sizeof(C1_hw));

        /*
         * Step 1: issue GEMM for problem 0 → auto-binds to an idle Gemmini (A)
         */
        issue_gemm((const elem_t *)A0, (const elem_t *)B0, (elem_t *)C0_hw);

        /*
         * Step 2: release Gemmini A (keeps it in bitmask so fence drains it)
         *         The next RoCC goes to a fresh idle Gemmini (B).
         */
        gemmini_release();

        /*
         * Step 3: issue GEMM for problem 1 → auto-binds to a different Gemmini (B)
         */
        issue_gemm((const elem_t *)A1, (const elem_t *)B1, (elem_t *)C1_hw);

        /*
         * Step 4: release Gemmini B (optional, keeps bitmask tidy)
         */
        gemmini_release();

        /*
         * Step 5: fence — wait for BOTH Gemminis to finish, then clear bitmask
         */
        gemmini_fence();

        /* Verify both outputs */
        if (verify("GEMM0", C0_hw, C0_ref) != 0) { fails++; break; }
        if (verify("GEMM1", C1_hw, C1_ref) != 0) { fails++; break; }

        if (rep % (N_REPEAT / 10) == 0)
            printf("  rep %4d / %d  vCPU=%d\n", rep, N_REPEAT, sched_getcpu());
    }

    if (fails == 0)
        printf("\n[PASS] All %d × 2 GEMMs correct on vCPU %d using 2 Gemminis\n",
               N_REPEAT, sched_getcpu());
    else
        printf("\n[FAIL] %d error(s)\n", fails);

    return fails ? 1 : 0;
}
