#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	uint64_t vector_mem1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint64_t vector_mem2[8] = {9, 10, 11, 12, 13, 14, 15, 16};
	uint64_t vector_mem3[8] = {17, 18, 19, 20, 21, 22, 23, 24};
	uint64_t vector_mem4[8] = {25, 26, 27, 28, 29, 30, 31, 32};
	uint64_t vector_mem5[8] = {33, 34, 35, 36, 37, 38, 39, 40};
	uint64_t vector_mem6[8] = {41, 42, 43, 44, 45, 46, 47, 48};
	uint64_t vector_mem7[8] = {49, 50, 51, 52, 53, 54, 55, 56};
	uint64_t vector_mem8[8] = {57, 58, 59, 60, 61, 62, 63, 64};
	uint64_t scalar = 0;
	uint64_t result[8] = {0};

	__asm__ volatile (
			"vsetvli t0, %0, e64, m1 \n"
			"vle64.v v1, (%1) \n"
			"vle64.v v2, (%2) \n"
			"vle64.v v3, (%3) \n"
			"vle64.v v4, (%4) \n"
			"vle64.v v5, (%5) \n"
			"vle64.v v6, (%6) \n"
			"vle64.v v7, (%7) \n"
			"vle64.v v8, (%8) \n"
			".word 0x4E1E64D7 \n"
			"vse64.v v9, (%10) \n"
			:
			: "r"(VL), "r"(vector_mem1), "r"(vector_mem2), "r"(vector_mem3),
			"r"(vector_mem4), "r"(vector_mem5), "r"(vector_mem6), "r"(vector_mem7),
			"r"(vector_mem8), "r"(scalar), "r"(result)
			: "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"
			);

	printf("Result Vector Registers:\n");
	for (int i = 0; i < VL; i++) {
		printf("result[%d]: %lu\n", i, result[i]);
	}
							        
	return 0;
}

