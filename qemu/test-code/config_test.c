#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "gemmini_api.h"

#define MAT_DIM_I 32 // batch (=Q rows)
#define MAT_DIM_K 32 // Q, K feature lengt
#define MAT_DIM_J 32 // sequence length
#define MAT_DIM_V 32 // V vector length

#define I_PAD (((MAT_DIM_I + DIM - 1) / DIM) * DIM)
#define K_PAD (((MAT_DIM_K + DIM - 1) / DIM) * DIM)
#define J_PAD (((MAT_DIM_J + DIM - 1) / DIM) * DIM)
#define V_PAD (((MAT_DIM_V + DIM - 1) / DIM) * DIM)

#define IB (I_PAD / DIM)
#define KB (K_PAD / DIM)
#define JB (J_PAD / DIM)
#define VB (V_PAD / DIM)

#define MVIN_SCALE_IDENTITY 1.0
#define ACC_SCALE_IDENTITY 1.0

#define NO_ACTIVATION 0
#define RELU 1
#define LAYERNORM 2
#define IGELU 3
#define SOFTMAX 4
#define ACT_FN SOFTMAX

#define NO_BIAS 1
#define FULL_BIAS_WIDTH 1
#define BERT_SCALE 0.8
#define ACT_FN SOFTMAX

#if FULL_BIAS_WIDTH
typedef acc_t ACC_T;
#else
typedef elem_t ACC_T;
#endif
typedef float acc_scale_t;

//stage-1 (Q dot K^T to softmax)
static elem_t Q[MAT_DIM_I][MAT_DIM_K] row_align(1);
static elem_t Kmat[MAT_DIM_K][MAT_DIM_J] row_align(1);
static elem_t Kmat_reordered[JB*DIM][KB*DIM] row_align(1);
static elem_t Soft[MAT_DIM_I][MAT_DIM_J] row_align(1);
// stage-2 (softmax dot V to output)
static elem_t Vmat[MAT_DIM_J][MAT_DIM_V] row_align(1);
static elem_t Vmat_reordered[VB*DIM][JB*DIM] row_align(1);
static elem_t O[MAT_DIM_I][MAT_DIM_V] row_align(1);
static ACC_T DummyBias[MAT_DIM_I][MAT_DIM_J] row_align_acc(1);


void print_matrix(elem_t *base, size_t rows, size_t cols) {
	for (size_t r = 0; r < rows; ++r) {
		for (size_t c = 0; c < cols; ++c)
			printf("%d ", base[r*cols + c]);
		printf("\n");
	}
}
static void init_mats(void) {
	/*Q*/
	for (size_t i = 0; i < MAT_DIM_I; ++i)
		for (size_t k = 0; k < MAT_DIM_K; ++k)
			Q[i][k] = 1;
	/* K */
	for (size_t k = 0; k < MAT_DIM_K; ++k)
		for (size_t j = 0; j < MAT_DIM_J; ++j)
			Kmat[k][j] = 1;//rand() % 16 == 1 ? 1 : 0;
	/* V */
	for (size_t j = 0; j < MAT_DIM_J; ++j)
		for (size_t v = 0; v < MAT_DIM_V; ++v)
			Vmat[j][v] = 1;//rand() % 16 == 1 ? 1 : 0;
}
static void reorder_K(elem_t *dst, elem_t *src, int stride, int row_tile, int col_tile){
	size_t out = 0;
	for (size_t jb = 0; jb < col_tile; ++jb){ // col-tile
		for (size_t k = 0; k < DIM; ++k){
			for (size_t kb = 0; kb < row_tile; ++kb){
				for (size_t j = 0; j < DIM; ++j){
					dst[out] = src[(kb*DIM + k)*stride+ jb*DIM + j];
					out++;
				}
			}
		}
	}
}
int main(){
	//gemmini_flush(0);
	init_mats();
	reorder_K((elem_t *)Kmat_reordered, (elem_t *)Kmat, MAT_DIM_J, KB, JB);
	reorder_K((elem_t *)Vmat_reordered, (elem_t *)Vmat, MAT_DIM_V, JB, VB);

	/* ======================== Stage-1 ============================ */
	const size_t pad_I = I_PAD - MAT_DIM_I;
	const size_t pad_K = K_PAD - MAT_DIM_K;
	const size_t pad_J = J_PAD - MAT_DIM_J;
	
	gemmini_extended_config_ex(WS, ACT_FN & 3, 0, 1, false, false);
	gemmini_extended_config_st(MAT_DIM_J * sizeof(elem_t), ACT_FN & 3, ACC_SCALE_IDENTITY);
	gemmini_extended3_config_ld(MAT_DIM_K * sizeof(elem_t), MVIN_SCALE_IDENTITY,false,0);
	gemmini_extended3_config_ld(KB*DIM * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1); //K^T							
	gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 2); // no bias
	if (ACT_FN == SOFTMAX){
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
	//const unsigned long start = read_cycles();
	
	gemmini_gemv_loop_ws(
			IB, JB, KB,
			pad_I, pad_J, pad_K,
			(elem_t*)Q, (elem_t*)Kmat_reordered,
			NO_BIAS ? NULL : &DummyBias[0][0], /* no bias */
			(elem_t*)Soft,
			MAT_DIM_K, DIM*KB, /*B_stride = DIM * KB because of
						 reordering*/
			MAT_DIM_J, MAT_DIM_J,
			false, false,
			/*full_C*/false, /*low_D*/false,
			/*ex_accumulate*/false,
			ACT_FN,
			/*a_spad_id*/0,
			/*b_spad_id*/0,
			/*c_spad_id*/2,
			/*is_resadd*/false);
	//gemmini_fence();
	printf("Soft matrix (I - 128) ð J = %d - 128) ð %d):\n", MAT_DIM_I, MAT_DIM_J);
	print_matrix(&Soft[0][0], MAT_DIM_I, MAT_DIM_J);
	printf("\n");
	sleep(1);

	/* =========================== Stage-2 ============================= */
	/* pad counts for Soft (1xJ) dot V (JxV) */
	const size_t pad_V = V_PAD - MAT_DIM_V;
	gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);
	gemmini_extended_config_st(MAT_DIM_V * sizeof(elem_t), NO_ACTIVATION,ACC_SCALE_IDENTITY);
	gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
	// Soft (MVIN skipped)
	gemmini_extended3_config_ld(JB*DIM * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
	gemmini_extended3_config_ld(MAT_DIM_J * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 2);
	
	gemmini_gemv_loop_ws(
			IB, VB, JB,
			pad_I, pad_V, pad_J,
			NULL, /* A skipped, reuse bank 2 */
			(elem_t*)Vmat_reordered,
			NO_BIAS ? NULL : &DummyBias[0][0],
			(elem_t*)O,
			MAT_DIM_J, JB * DIM,
			MAT_DIM_V, MAT_DIM_V,
			false, false,
			/*full_C*/false, /*low_D*/false,
			/*ex_accumulate*/false,
			NO_ACTIVATION,
			/*a_spad_id*/2, /* Soft in bank 2 */
			/*b_spad_id*/0,
			/*c_spad_id*/0,
			/*is_resadd*/false);;
	//gemmini_fence();
	//const unsigned long end = read_cycles();
	//printf("Total Gemmini cycles: %u\n", end - start);
	printf("GEMV_loop_ws O matrix (I - 128) ð J = %d - 128) ð %d):\n", MAT_DIM_I,MAT_DIM_V);
	print_matrix(&O[0][0], MAT_DIM_I, MAT_DIM_V);
	printf("\n");
	return 0;
}
