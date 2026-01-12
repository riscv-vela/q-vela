#include <stdio.h>
#include <stdint.h>

#define VL 4

int main() {
	float vs2[VL] = {10.0, 5.0, -4, 8};
	float result[VL] = {0};
	uint64_t rs1 = 1;

        printf("====================== INITIAL STATE ======================\n");
	printf("x1: %f, x2: %f, x3: %f, x4: %f\n", vs2[0], vs2[1], vs2[2], vs2[3]);
	printf("Position: %lu\n", rs1);

	__asm__ volatile (
			"vsetvli t0, %2, e32, m1 \n"
			"vle32.v v0, (%0) \n"
			".word 0xE6075057 \n"
			"vse32.v v0, (%3) \n"
			: 
			: "r"(vs2), "r"(rs1), "r"(VL), "r"(result)
		      	: "t0", "v0"
			);


        printf("\n===================== AFTER EXECUTION =====================\n");
	printf("x1': %f, x2': %f, x3': %f, x4': %f\n", result[0], result[1], result[2], result[3]);

	return 0;
}

