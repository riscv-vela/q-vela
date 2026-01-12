#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
       	uint8_t vector_mem[64] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	      	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
       		0, 0, 0, 0, 0, 0, 0, 
	};

	uint8_t result_v1[8] = {0, 9, 0, 0, 0, 0, 0, 0};
	uint8_t result_v2[8] = {0, 10, 0, 0, 0, 0, 0, 0};
	uint8_t result_v3[8] = {0, 11, 0, 0, 0, 0, 0, 0};
	uint8_t result_v4[8] = {0, 12, 0, 0, 0, 0, 0, 0};
	uint8_t result_v5[8] = {0, 13, 0, 0, 0, 0, 0, 0};
	uint8_t result_v6[8] = {0, 14, 0, 0, 0, 0, 0, 0};
	uint8_t result_v7[8] = {0, 15, 0, 0, 0, 0, 0, 0};
	uint8_t result_v8[8] = {0, 16, 0, 0, 0, 0, 0, 0};
	uint8_t scalar = 1;

        printf("=========== INITIAL STATE ===========\n");
       	printf("v1: %u %u %u %u %u %u %u %u\n", 
       			result_v1[0], result_v1[1], result_v1[2], result_v1[3], 
	       		result_v1[4], result_v1[5], result_v1[6], result_v1[7]);
   	printf("v2: %u %u %u %u %u %u %u %u\n", 
   			result_v2[0], result_v2[1], result_v2[2], result_v2[3], 
	   		result_v2[4], result_v2[5], result_v2[6], result_v2[7]);
       	printf("v3: %u %u %u %u %u %u %u %u\n", 
			result_v3[0], result_v3[1], result_v3[2], result_v3[3], 
       			result_v3[4], result_v3[5], result_v3[6], result_v3[7]);
	printf("v4: %u %u %u %u %u %u %u %u\n", 
			result_v4[0], result_v4[1], result_v4[2], result_v4[3], 
   			result_v4[4], result_v4[5], result_v4[6], result_v4[7]);
	printf("v5: %u %u %u %u %u %u %u %u\n", 
			result_v5[0], result_v5[1], result_v5[2], result_v5[3], 
			result_v5[4], result_v5[5], result_v5[6], result_v5[7]);
	printf("v6: %u %u %u %u %u %u %u %u\n", 
			result_v6[0], result_v6[1], result_v6[2], result_v6[3], 
			result_v6[4], result_v6[5], result_v6[6], result_v6[7]);
	printf("v7: %u %u %u %u %u %u %u %u\n", 
			result_v7[0], result_v7[1], result_v7[2], result_v7[3], 
			result_v7[4], result_v7[5], result_v7[6], result_v7[7]);
   	printf("v8: %u %u %u %u %u %u %u %u\n", 
			result_v8[0], result_v8[1], result_v8[2], result_v8[3], 
			result_v8[4], result_v8[5], result_v8[6], result_v8[7]);

	printf("Vector Element Index %d\n", scalar);

	for (int i = 0; i < 64; i += 8) {
		printf("Actuator[%d]: ", i / 8);
		for (int j = 0; j < 8; j++) {
			printf("%d ", vector_mem[i + j]);
		}
		printf("\n");
	}

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
                        ".word 0x4E1E64D7 \n"
			"vse8.v v9, (%10) \n"
			:
			: "r"(VL), "r"(result_v1), "r"(result_v2), "r"(result_v3), "r"(result_v4), "r"(result_v5), "r"(result_v6), "r"(result_v7), "r"(result_v8), "r"(scalar), "r"(vector_mem + 8 * scalar)
			: "t0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9"
			);
	printf("\n========== AFTER EXECUTION ==========\n");

       	for (int i = 0; i < 64; i += 8) {
		printf("Actuator[%d]: ", i / 8);
		for (int j = 0; j < 8; j++) {
			printf("%d ", vector_mem[i + j]);
		}
		printf("\n");
	}
	
	return 0;
}

