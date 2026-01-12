#include <stdio.h>
#include <stdint.h>

int main() {
	    uint8_t data[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	        uint8_t result[8];

		    uint64_t stride = 2;

		        __asm__ volatile (
					        "vsetvli t0, %2, e8, m1 \n"
						        "vlse8.v v0, (%0), %1 \n"
							        "vse8.v v0, (%3) \n"
								        :
									        : "r"(data), "r"(stride), "r"(8), "r"(result)
										        : "t0", "v0"
											    );

			    printf("Loaded vector (stride = %lu): ", stride);
			        for (int i = 0; i < 8; i++) {
					        printf("%d ", result[i]);
						    }
				    printf("\n");

				        return 0;
}

