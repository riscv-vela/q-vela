#include <stdio.h>
#include <stdint.h>

#define VL 8  

int main() {
	    uint8_t vector_mem[VL] = {1, 2, 3, 4, 5, 6, 7, 8};
	        uint8_t result_v9[VL] = {0};

		    __asm__ volatile (
				            "vsetvli t0, %1, e8, m1 \n"
					            "vle8.v v9, (%0) \n"
						            "vse8.v v9, (%2) \n"
							            :
								            : "r"(vector_mem), "r"(VL), "r"(result_v9)
									            : "t0", "v9"
										        );

		        printf("Result v9:\n");
			    for (int i = 0; i < VL; i++) {
				            printf("%u ", result_v9[i]);
					        }
			        printf("\n");

				    return 0;
}

