#include <stdio.h>
#include <stdint.h>

#define VL 8 

int main() {
	uint8_t vs2[VL] = {86, 97, 255, 255, 255, 255, 255, 255};
	uint8_t result[VL] = {0};
	uint8_t rs1 = 90;

        printf("==================== INITIAL STATE ====================\n");
	printf("Read Data: %2d %2d %2d %2d %2d %2d %2d %2d\n", vs2[0], vs2[1], vs2[2], vs2[3], vs2[4], vs2[5], vs2[6], vs2[7]);
	printf("New Node: %u\n", rs1);
	printf("Mask: ");
	for (int i = 0; i < VL; i++) {
		printf("%2d ", result[i]);
	}
	printf("\n");

	__asm__ volatile (
			"vsetvli t0, %2, e8, m1 \n"
			"vle8.v v0, (%0) \n"
			".word 0xC2074057 \n"
			"vse8.v v0, (%3) \n"
			:
			: "r"(vs2), "r"(rs1), "r"(VL), "r"(result)
			: "t0", "v0"
			);


	printf("\n=================== AFTER EXECUTION ===================\n");

	printf("Mask: ");
	
	for (int i = 0; i < VL; i++) {
		printf("%2d ", result[i]);
	}
	printf("\n");

	return 0;
}

