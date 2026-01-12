#include <stdio.h>
#include <stdint.h>

#define VL 2

int main() {
	uint64_t vs2[VL] = {10, 20};
	uint64_t result[VL] = {0};
	uint64_t rs1 = 11;

	__asm__ volatile (
			"vsetvli t0, %2, e64, m1 \n"
			"vle64.v v0, (%0) \n"
			".word 0xC2074057 \n"
			"vse64.v v0, (%3) \n"
			: 
			: "r"(vs2), "r"(rs1), "r"(VL), "r"(result)
		      	: "t0", "v0"
			);

	printf("Result Vector: ");
	for (int i = 0; i < VL; i++) {
		printf("%lu ", result[i]);
	}
	printf("\n");

	return 0;
}

