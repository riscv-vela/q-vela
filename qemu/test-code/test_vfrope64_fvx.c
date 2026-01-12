#include <stdio.h>
#include <stdint.h>

#define VL 2

int main() {
	double vs2[VL] = {10.0, 5.0};
	double result[VL] = {0};
	uint64_t rs1 = 1;

        printf("====================== INITIAL STATE ======================\n");
	printf("x1: %f, x2: %f\n", vs2[0], vs2[1]);
	printf("Position: %lu\n", rs1);

	__asm__ volatile (
			"vsetvli t0, %2, e64, m1 \n"
			"vle64.v v0, (%0) \n"
			".word 0xE6075057 \n"
			"vse64.v v0, (%3) \n"
			: 
			: "r"(vs2), "r"(rs1), "r"(VL), "r"(result)
		      	: "t0", "v0"
			);


        printf("\n===================== AFTER EXECUTION =====================\n");
	printf("x1': %f, x2': %f\n", result[0], result[1]);

	return 0;
}

