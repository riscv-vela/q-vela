
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "rocc_api.h"

#define MAT_DIM_I 32 // batch (=Q rows)
#define MAT_DIM_K 64 // Q, K feature lengt
#define MAT_DIM_J 32 // sequence length

#define I_PAD (((MAT_DIM_I + DIM - 1) / DIM) * DIM)
#define K_PAD (((MAT_DIM_K + DIM - 1) / DIM) * DIM)
#define J_PAD (((MAT_DIM_J + DIM - 1) / DIM) * DIM)

#define IB (I_PAD / DIM)
#define KB (K_PAD / DIM)
#define JB (J_PAD / DIM)

#define MVIN_SCALE_IDENTITY 1.0
#define ACC_SCALE_IDENTITY 1.0

#define NO_ACTIVATION 0
#define RELU 1
#define LAYERNORM 2
#define IGELU 3
#define SOFTMAX 4
#define ACT_FN SOFTMAX

#define NO_BIAS 0
#define FULL_BIAS_WIDTH 1
#define BERT_SCALE 0.8
#if FULL_BIAS_WIDTH
typedef acc_t ACC_T;
#else
typedef elem_t ACC_T;
#endif
typedef float acc_scale_t;

static elem_t Q[MAT_DIM_I][MAT_DIM_K] row_align(1);
static elem_t Kmat[MAT_DIM_K][MAT_DIM_J] row_align(1);
static elem_t Soft[MAT_DIM_I][MAT_DIM_J] row_align(1);
static ACC_T DummyBias[MAT_DIM_I][MAT_DIM_J] row_align_acc(1);

void print_matrix(elem_t *base, size_t rows, size_t cols) {
	for (size_t r = 0; r < rows; ++r) {
		for (size_t c = 0; c < cols; ++c)
			printf("%d ", base[r*cols + c]);
		printf("\n");
	}
}

static void init_mats(void) {
	srand(time(NULL));
	/*Q*/
	for (size_t i = 0; i < MAT_DIM_I; ++i)
		for (size_t k = 0; k < MAT_DIM_K; ++k)
			Q[i][k] = 1;
	/* K */
	for (size_t k = 0; k < MAT_DIM_K; ++k)
		for (size_t j = 0; j < MAT_DIM_J; ++j)
			Kmat[k][j] = rand() % 16 == 1 ? 1 : 0;
	/* Bias */
	for (size_t i = 0; i < MAT_DIM_I; ++i)
		for (size_t j = 0; j < MAT_DIM_J; ++j)
			DummyBias[i][j] = 3;
}

static void init_mats2(void) {
	srand(time(NULL));
	/*Q*/
	for (size_t i = 0; i < MAT_DIM_I; ++i)
		for (size_t k = 0; k < MAT_DIM_K; ++k)
			Q[i][k] = 1;
	/* K */
	for (size_t k = 0; k < MAT_DIM_K; ++k){
		for (size_t j = 0; j < MAT_DIM_J; ++j){
			if(k == j)
				Kmat[k][j] = 1;
			else
				Kmat[k][j] = 0;
		}
	}
	/* Bias */
	for (size_t i = 0; i < MAT_DIM_I; ++i)
		for (size_t j = 0; j < MAT_DIM_J; ++j)
			DummyBias[i][j] = rand() % 2;
}

typedef struct {
    int tile_i;
    int tile_j;
    elem_t *Q;
    elem_t *Kmat;
    ACC_T *DummyBias;
    elem_t *Soft;
} thread_arg_t;

void *fence_worker(void *arg){
    gemmini_fence();
    return NULL;
}

void *matmul_worker(void *arg){
    thread_arg_t *args = (thread_arg_t *)arg;
    struct timespec t_start, t_config, t_compute;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    int i_start = args->tile_i * DIM;
    int j_start = args->tile_j * DIM;
    
    // Calculate tile dimensions (handle edge tiles)
    int tile_i_dim = (i_start + DIM <= MAT_DIM_I) ? DIM : (MAT_DIM_I - i_start);
    int tile_j_dim = (j_start + DIM <= MAT_DIM_J) ? DIM : (MAT_DIM_J - j_start);
    
    if (tile_i_dim <= 0 || tile_j_dim <= 0) {
        return NULL;
    }
    
    // Compute padding for this tile
    const size_t pad_I = DIM - tile_i_dim;
    const size_t pad_J = DIM - tile_j_dim;
    const size_t pad_K = K_PAD - MAT_DIM_K;
    
    // Configure for this tile
    gemmini_extended_config_ex(WS, ACT_FN & 3, 0, 1, false, false);
    gemmini_extended_config_st(MAT_DIM_J * sizeof(elem_t), ACT_FN & 3, ACC_SCALE_IDENTITY);
    gemmini_extended3_config_ld(MAT_DIM_K * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 2);
    
    if (ACT_FN == SOFTMAX) {
        const scale_t a = 0.3585;
        const scale_t b = 1.353;
        const scale_t c = 0.344;
        const acc_t qln2 = (int) (0.693147 / BERT_SCALE);
        const acc_t qln2_inv = qln2 == 0 ? -1 : 65536 / qln2;
        const acc_t qb = b / BERT_SCALE;
        const acc_t qc = c / (a*BERT_SCALE*BERT_SCALE);
        gemmini_config_norm(qln2, 0, 0, 1, 0, qb, qc);
        gemmini_config_norm(qln2_inv, 1, 0, 1, 0, qb, qc);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_config);
    // Compute this tile: Q[tile_i, :] @ Kmat[:, tile_j]
    gemmini_loop_ws(
        1, 1, KB,  // Process 1x1 tile at a time in I,J dimensions
        pad_I, pad_J, pad_K,
        (elem_t*)&args->Q[i_start * MAT_DIM_K],
        (elem_t*)&args->Kmat[j_start],
        NO_BIAS ? NULL : &args->DummyBias[i_start * MAT_DIM_J + j_start],
        (elem_t*)&args->Soft[i_start * MAT_DIM_J + j_start],
        MAT_DIM_K, MAT_DIM_J,
        MAT_DIM_J, MAT_DIM_J,
        false, false,
        false, false,
        false,
        NO_ACTIVATION,
        0, 0, 0,
        false);

    //gemmini_fence();
    

    clock_gettime(CLOCK_MONOTONIC, &t_compute);
    
    printf("Thread %d: config=%.3fμs compute=%.3fμs\n",
           args->tile_i * JB + args->tile_j,
           (t_config.tv_nsec - t_start.tv_nsec) / 1000.0,
           (t_compute.tv_nsec - t_config.tv_nsec) / 1000.0);
    return NULL;
}

int main(){
    init_mats2();
    gemmini_init_trace();

	struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create threads for each tile
    int num_tasks = IB * JB;
    pthread_t fences[4];
    pthread_t threads[4];
    thread_arg_t thread_args_fence[4];
    thread_arg_t thread_args[num_tasks];
    
    int thread_idx = 0;
    for (int i = 0; i < IB; i++) {
        for (int j = 0; j < JB; j++) {
            thread_args[thread_idx].tile_i = i;
            thread_args[thread_idx].tile_j = j;
            thread_args[thread_idx].Q = &Q[0][0];
            thread_args[thread_idx].Kmat = &Kmat[0][0];
            thread_args[thread_idx].DummyBias = &DummyBias[0][0];
            thread_args[thread_idx].Soft = &Soft[0][0];

            thread_idx++;
        }
    }

    for (int base = 0; base < num_tasks; base += 4) {
        int batch = (num_tasks - base > 4) ? 4 : (num_tasks - base);

        for (int t = 0; t < batch; t++) {
            pthread_create(&threads[t], NULL, matmul_worker, &thread_args[base + t]);
        }
        for (int t = 0; t < batch; t++) {
            pthread_join(threads[t], NULL);
        }
    }

    // for (int i = 0; i < 4; i++) {
    //     thread_args_fence[i].tile_i = i;
    //     pthread_create(&fences[i], NULL, fence_worker, &thread_args_fence[i]);
    // }

    // for (int i = 0; i < 4; i++) {
    //     pthread_join(fences[i], NULL);
    // }

	clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate elapsed time
    double elapsed_sec = (end.tv_sec - start.tv_sec) + 
                         (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL + 
                          (end.tv_nsec - start.tv_nsec);
    
    printf("Soft matrix (I=%d, J=%d):\n", MAT_DIM_I, MAT_DIM_J);
    print_matrix(&Soft[0][0], MAT_DIM_I, MAT_DIM_J);
    printf("\n");
	printf("Total execution time: %.6f seconds (%.3f ms)\n", elapsed_sec, elapsed_sec * 1000);
    printf("Total execution time: %lu nanoseconds\n", elapsed_ns);
    gemmini_dump_trace();
    return 0;
}