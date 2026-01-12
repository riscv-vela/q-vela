#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	uint8_t vector_mem1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint8_t vector_mem2[8] = {9, 10, 11, 12, 13, 14, 15, 16};
	uint8_t vector_mem3[8] = {17, 18, 19, 20, 21, 22, 23, 24};
	uint8_t vector_mem4[8] = {25, 26, 27, 28, 29, 30, 31, 32};
	uint8_t vector_mem5[8] = {33, 34, 35, 36, 37, 38, 39, 40};
	uint8_t vector_mem6[8] = {41, 42, 43, 44, 45, 46, 47, 48};
	uint8_t vector_mem7[8] = {49, 50, 51, 52, 53, 54, 55, 56};
	uint8_t vector_mem8[8] = {57, 58, 59, 60, 61, 62, 63, 64};
	uint8_t scalar = 5;
	uint8_t result[VL] = {0};

	__asm__ volatile (
			"vsetvli t0, %0, e8, m1 \n"
			"vle8.v v1, (%1) \n"
			"vle8.v v2, (%2) \n"
			"vle8.v v3, (%3) \n"
			"vle8.v v4, (%4) \n"
			"vle8.v v5, (%5) \n"
			"vle8.v v6, (%6) \n"
			"vle8.v v7, (%7) \n"
			"vle8.v v8, (%8) \n"
			"vadd.vx v9, v2, %9 \n"
			"vse8.v v9, (%10) \n"
			:
			: "r"(VL), "r"(vector_mem1), "r"(vector_mem2), "r"(vector_mem3), "r"(vector_mem4), "r"(vector_mem5), "r"(vector_mem6), "r"(vector_mem7), "r"(vector_mem8), "r"(scalar), "r"(result)
			: "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"
			);

	printf("Result Vector Registers:\n");
	printf("result[0]: %u\n", result[0]);
	printf("result[1]: %u\n", result[1]);
	printf("result[2]: %u\n", result[2]);
	printf("result[3]: %u\n", result[3]);
	printf("result[4]: %u\n", result[4]);
	printf("result[5]: %u\n", result[5]);
	printf("result[6]: %u\n", result[6]);
	printf("result[7]: %u\n", result[7]);

	return 0;
}

