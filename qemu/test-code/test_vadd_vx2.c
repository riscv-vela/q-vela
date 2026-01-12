#include <stdio.h>
#include <stdint.h>

#define VL 1  

int main() {
	uint8_t vector_mem[8] = {10, 20, 30, 40, 50, 60, 70, 80};
	uint8_t scalar = 5;
	uint8_t result_v1[VL] = {0};
	uint8_t result_v2[VL] = {0};
	uint8_t result_v3[VL] = {0};
	uint8_t result_v4[VL] = {0};
	uint8_t result_v5[VL] = {0};
	uint8_t result_v6[VL] = {0};
	uint8_t result_v7[VL] = {0};
	uint8_t result_v8[VL] = {0};

	__asm__ volatile (
			"vsetvli t0, %2, e8, m1 \n"
			"vle8.v v9, (%0) \n"
			"vadd.vx v4, v9, %1 \n"
			"vse8.v v1, (%3) \n"
			"vse8.v v2, (%4) \n"
			"vse8.v v3, (%5) \n"
			"vse8.v v4, (%6) \n"
			"vse8.v v5, (%7) \n"
			"vse8.v v6, (%8) \n"
			"vse8.v v7, (%9) \n"
			"vse8.v v8, (%10) \n"
			:
			: "r"(vector_mem), "r"(scalar), "r"(VL),
			"r"(result_v1), "r"(result_v2), "r"(result_v3), "r"(result_v4),
			"r"(result_v5), "r"(result_v6), "r"(result_v7), "r"(result_v8)
			: "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"
			);

	printf("Result Vector Registers:\n");
	printf("v1[0]: %u\n", result_v1[0]);
	printf("v2[0]: %u\n", result_v2[0]);
	printf("v3[0]: %u\n", result_v3[0]);
	printf("v4[0]: %u\n", result_v4[0]);
	printf("v5[0]: %u\n", result_v5[0]);
	printf("v6[0]: %u\n", result_v6[0]);
	printf("v7[0]: %u\n", result_v7[0]);
	printf("v8[0]: %u\n", result_v8[0]);

	return 0;
}

