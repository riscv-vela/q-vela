#include <stdio.h>
#include <stdint.h>

int main() {
	    uint64_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	        uint64_t result[8] = { 0 };
		    uint64_t stride = 1;

		        uint64_t vl = 4;  

			    __asm__ volatile (
					            "vsetvli t0, %3, e64, m1 \n"
						            "vlse64.v v0, (%0), %2 \n"
							            "vsse64.v v0, (%1), %2 \n"
								            :
									            : "r"(data), "r"(result), "r"(stride), "r"(vl)
										            : "t0", "v0"
											        );

			        printf("Loaded vector (stride = %lu): ", stride);
				    for (int i = 0; i < vl; i++) {
					            printf("%lu ", result[i]);
						        }
				        printf("\n");

					    return 0;
}

