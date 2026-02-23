#include <math.h>
#include <riscv_vector.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define D 4096
#define VL 8
#define M 10
#define IDX 0

static void rope_fp32_reference(const float *x, float *y,
								uint32_t d, uint16_t m, uint16_t idx0)
{
	for (uint32_t k = 0; k < d / 2; k++) {
		uint32_t elem0 = idx0 + (2 * k);
		uint32_t i = (elem0 % 128) / 2;

		float theta = powf(10000.0f, (-2.0f * (float)i / 128.0f));
		float angle = (float)m * theta;
		float cos_theta = cosf(angle);
		float sin_theta = sinf(angle);

		float x1 = x[2 * k];
		float x2 = x[2 * k + 1];

		y[2 * k] = x1 * cos_theta - x2 * sin_theta;
		y[2 * k + 1] = x1 * sin_theta + x2 * cos_theta;
	}
}

static void rope_custom_instruction(const float *x, float *y,
									uint32_t d, uint16_t m, uint16_t idx0)
{
	uint16_t idx = idx0;
	uint32_t x3 = ((uint32_t)idx << 16) | (uint32_t)m;

	asm volatile("vsetivli zero, 8, e32, m1, ta, ma" ::: "memory");

	for (uint32_t chunk = 0; chunk < d / VL; chunk++) {
		asm volatile(
			"mv t0, %1\n\t"
			"vle32.v v2, (%0)\n\t"
			".word 0x4A22D1D7\n\t"
			"vse32.v v3, (%2)\n\t"
			:
			: "r"(&x[idx]), "r"(x3), "r"(&y[idx])
			: "x3", "x31", "v2", "v3", "t0", "memory");

		idx += VL;
		x3 += (VL << 16);
	}
}

int main(void)
{
	static float x[D];
	static float y_ref[D];
	static float y_custom[D];

	for (uint32_t i = 0; i < D; i++) {
		x[i] = ((float)(i + 1) * 0.001953125f) - 1.0f;
		y_ref[i] = 0.0f;
		y_custom[i] = 0.0f;
	}

	rope_fp32_reference(x, y_ref, D, M, IDX);
	rope_custom_instruction(x, y_custom, D, M, IDX);

	double sum_abs_err = 0.0;
	double sum_sq_err = 0.0;
	double sum_rel_err = 0.0;
	float max_abs_err = 0.0f;
	float max_rel_err = 0.0f;
	uint32_t max_abs_idx = 0;
	uint32_t max_rel_idx = 0;

	for (uint32_t i = 0; i < D; i++) {
		float abs_err = fabsf(y_custom[i] - y_ref[i]);
		float denom = fmaxf(fabsf(y_ref[i]), 1e-12f);
		float rel_err = abs_err / denom;

		sum_abs_err += abs_err;
		sum_sq_err += (double)abs_err * (double)abs_err;
		sum_rel_err += rel_err;

		if (abs_err > max_abs_err) {
			max_abs_err = abs_err;
			max_abs_idx = i;
		}
		if (rel_err > max_rel_err) {
			max_rel_err = rel_err;
			max_rel_idx = i;
		}
	}

	double mae = sum_abs_err / (double)D;
	double rmse = sqrt(sum_sq_err / (double)D);
	double mrae = sum_rel_err / (double)D;

	printf("=== vfrope.fvx precision test ===\n");
	printf("D=%d, VL=%d, m=%d, idx0=%d\n", D, VL, M, IDX);
	printf("MAE          : %.10e\n", mae);
	printf("RMSE         : %.10e\n", rmse);
	printf("Mean Rel Err : %.10e\n", mrae);
	printf("Max Abs Err  : %.10e at i=%u (custom=%.8f, ref=%.8f)\n",
		   max_abs_err, max_abs_idx, y_custom[max_abs_idx], y_ref[max_abs_idx]);
	printf("Max Rel Err  : %.10e at i=%u (custom=%.8f, ref=%.8f)\n",
		   max_rel_err, max_rel_idx, y_custom[max_rel_idx], y_ref[max_rel_idx]);

	printf("\nSample points:\n");
	for (uint32_t i = 0; i < D; i += 256) {
		float abs_err = fabsf(y_custom[i] - y_ref[i]);
		printf("i=%4u  custom=% .8f  ref=% .8f  abs_err=% .3e\n",
			   i, y_custom[i], y_ref[i], abs_err);
	}

	return 0;
}
