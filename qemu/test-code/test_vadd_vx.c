#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	    uint8_t vs2[VL] = {1, 2, 3, 4, 5, 6, 7, 8};
	    uint8_t result[VL] = {0};
	    uint8_t rs1 = 5;

            __asm__ volatile (
		"vsetvli t0, %2, e8, m1 \n"
		"vle8.v v0, (%0) \n"
		//"vadd.vx v0, v0, %1 \n"
		".word 0xC2074057 \n"
		//".word 0x02074057 \n"
		"vse8.v v0, (%3) \n"
		:									        : "r"(vs2), "r"(rs1), "r"(VL), "r"(result)	
		: "t0", "v0"						
		);

	    printf("Result Vector: ");
	    for (int i = 0; i < VL; i++) {
		    printf("%d ", result[i]);
	    }
	    printf("\n");

	    return 0;
}

