/*
 * Parallel Gemmini GEMM Test (2 threads, IDLE_FIRST policy)
 *
 * Each thread runs a full square GEMM:
 *   A[DIM][DIM] x B[DIM][DIM] = C[DIM][DIM]
 * repeated N_REPEAT times.
 *
 * No CPU pinning — QEMU's IDLE_FIRST policy automatically assigns each
 * thread to an idle Gemmini when its computation begins, and releases
 * the binding when the thread calls gemmini_fence().
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <stdatomic.h>
#include "include/gemmini.h"

#define NUM_THREADS   2
#define N_REPEAT      10000   /* repeat GEMM this many times per thread */
#define NO_ACTIVATION 0

typedef float acc_scale_t;

/* Medium GEMM: 64x64 (4x4 tiles, fits safely in accumulator) */
#define G_I  64
#define G_K  64
#define G_J  64
#define G_IB (G_I / DIM)
#define G_KB (G_K / DIM)
#define G_JB (G_J / DIM)

/* Per-thread static buffers */
static elem_t g_A[NUM_THREADS][G_I][G_K]    row_align(1);
static elem_t g_B[NUM_THREADS][G_K][G_J]    row_align(1);
static elem_t g_C_hw[NUM_THREADS][G_I][G_J] row_align(1);
static elem_t g_C_ref[NUM_THREADS][G_I][G_J];

typedef struct {
    int thread_id;
    int result;        /* 0 = pass, -1 = fail */
    double elapsed_ms; /* wall time for HW computation */
    int vcpu_start;    /* vCPU when computation began */
    int vcpu_end;      /* vCPU after fence (may have migrated) */
    int gemmini_id;    /* Gemmini assigned (IDLE_FIRST: sequential from 0) */
} ThreadArg;

static atomic_int next_gemmini_id = 0;

static void *gemm_thread(void *arg)
{
    ThreadArg *ta = (ThreadArg *)arg;
    int tid = ta->thread_id;

    printf("[Thread %d] started on vCPU %d — GEMM %dx%d x %dx%d, %d repeats\n",
           tid, sched_getcpu(), G_I, G_K, G_K, G_J, N_REPEAT);

    /* --- Fill inputs with a thread-unique pattern --- */
    for (int i = 0; i < G_I; i++)
        for (int k = 0; k < G_K; k++)
            g_A[tid][i][k] = (elem_t)((i * G_K + k + tid + 1) % 5);

    for (int k = 0; k < G_K; k++)
        for (int j = 0; j < G_J; j++)
            g_B[tid][k][j] = (elem_t)(((k + j * 2 + tid) % 3) - 1);

    /* --- CPU reference --- */
    memset(g_C_ref[tid], 0, sizeof(g_C_ref[tid]));
    for (int i = 0; i < G_I; i++)
        for (int j = 0; j < G_J; j++) {
            int32_t sum = 0;
            for (int k = 0; k < G_K; k++)
                sum += (int32_t)g_A[tid][i][k] * (int32_t)g_B[tid][k][j];
            g_C_ref[tid][i][j] = (elem_t)sum;
        }

    /* --- Gemmini HW GEMM (repeated N_REPEAT times) --- */
    memset(g_C_hw[tid], 0, sizeof(g_C_hw[tid]));

    /* Record vCPU just before first RoCC — IDLE_FIRST binds this vCPU to a Gemmini */
    ta->vcpu_start = sched_getcpu();
    ta->gemmini_id = atomic_fetch_add(&next_gemmini_id, 1);
    printf("[Thread %d] issuing RoCC on vCPU %d → QEMU will bind to Gemmini %d\n",
           tid, ta->vcpu_start, ta->gemmini_id);

    gemmini_fence();

    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);
    gemmini_extended_config_st(G_J * sizeof(elem_t), NO_ACTIVATION, ACC_SCALE_IDENTITY);
    gemmini_extended3_config_ld(G_K * sizeof(elem_t),        MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(G_KB * DIM * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(G_J  * sizeof(acc_t),        MVIN_SCALE_IDENTITY, false, 2);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int rep = 0; rep < N_REPEAT; rep++) {
        tiled_matmul_auto(
            G_I, G_J, G_K,
            (const elem_t *)g_A[tid],
            (const elem_t *)g_B[tid],
            NULL,                       /* no bias (D) */
            (elem_t *)g_C_hw[tid],
            G_K,                        /* stride_A */
            G_J,                        /* stride_B */
            G_J,                        /* stride_D */
            G_J,                        /* stride_C */
            MVIN_SCALE_IDENTITY,        /* A_scale_factor */
            MVIN_SCALE_IDENTITY,        /* B_scale_factor */
            ACC_SCALE_IDENTITY,         /* D_scale_factor */
            NO_ACTIVATION,              /* act */
            ACC_SCALE_IDENTITY,         /* scale */
            ACC_SCALE_IDENTITY,         /* bert_scale */
            false,                      /* repeating_bias */
            false,                      /* transpose A */
            false,                      /* transpose B */
            false,                      /* full_C */
            false,                      /* low_D */
            0,                          /* weightA */
            WS
        );
    }

    gemmini_fence();

    clock_gettime(CLOCK_MONOTONIC, &t1);
    ta->elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1e3
                   + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    ta->vcpu_end = sched_getcpu();

    /* --- Verify last result --- */
    ta->result = 0;
    for (int i = 0; i < G_I; i++)
        for (int j = 0; j < G_J; j++) {
            if (g_C_hw[tid][i][j] != g_C_ref[tid][i][j]) {
                printf("[FAIL] Thread %d [%d][%d]: hw=%d ref=%d\n",
                       tid, i, j,
                       (int)g_C_hw[tid][i][j],
                       (int)g_C_ref[tid][i][j]);
                ta->result = -1;
                return NULL;
            }
        }

    printf("[PASS] Thread %d | vCPU %d → Gemmini %d (end vCPU %d) | %.1f ms (%.1f ms/rep)\n",
           tid, ta->vcpu_start, ta->gemmini_id, ta->vcpu_end,
           ta->elapsed_ms, ta->elapsed_ms / N_REPEAT);
    return NULL;
}

int main(void)
{
    pthread_t threads[NUM_THREADS];
    ThreadArg args[NUM_THREADS];

    srand((unsigned)time(NULL));
    printf("=== Parallel Gemmini GEMM: %d threads, %dx%d x %dx%d, %d reps ===\n\n",
           NUM_THREADS, G_I, G_K, G_K, G_J, N_REPEAT);

    /* Build a shuffled vCPU list so each thread gets a unique vCPU */
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int vcpu_list[num_cpus];
    for (int i = 0; i < num_cpus; i++) vcpu_list[i] = i;
    for (int i = num_cpus - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = vcpu_list[i]; vcpu_list[i] = vcpu_list[j]; vcpu_list[j] = tmp;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id  = i;
        args[i].result     = -1;
        args[i].elapsed_ms = 0;
        args[i].vcpu_start = -1;
        args[i].vcpu_end   = -1;

        /* Pin guest thread to a unique random vCPU */
        int vcpu = vcpu_list[i % num_cpus];
        pthread_attr_t attr;
        cpu_set_t cpuset;
        pthread_attr_init(&attr);
        CPU_ZERO(&cpuset);
        CPU_SET(vcpu, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
        pthread_create(&threads[i], &attr, gemm_thread, &args[i]);
        pthread_attr_destroy(&attr);
        printf("[Main] Thread %d pinned to vCPU %d (of %d)\n", i, vcpu, num_cpus);
    }

    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    int fails = 0;
    for (int i = 0; i < NUM_THREADS; i++)
        if (args[i].result != 0) fails++;

    printf("\n=== vCPU → Gemmini binding summary ===\n");
    for (int i = 0; i < NUM_THREADS; i++)
        printf("  Thread %d: vCPU %d → Gemmini %d  (end vCPU %d)%s\n",
               i, args[i].vcpu_start, args[i].gemmini_id, args[i].vcpu_end,
               args[i].vcpu_start != args[i].vcpu_end ? "  *** migrated!" : "");

    if (fails == 0) {
        printf("\nAll %d parallel tests passed!\n", NUM_THREADS);
        return 0;
    } else {
        printf("\nFailed: %d / %d\n", fails, NUM_THREADS);
        return 1;
    }
}
