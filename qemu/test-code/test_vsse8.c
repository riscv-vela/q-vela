#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	    uint8_t data[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	    uint8_t write_data[8] = {10, 30, 50, 70, 90, 110, 130, 150};
	    uint64_t stride = 2;

	    __asm__ volatile (
			    "vsetvli t0, %2, e8, m1 \n"
			    "vle8.v v0, (%1) \n"
			    "vsse8.v v0, (%0), %3 \n"
			    :
			    : "r"(data), "r"(write_data), "r"(VL), "r"(stride)
			    : "t0", "v0"
			    );
	    
	    printf("Updated data: ");
	    for (int i = 0; i < 16; i++) {
		    printf("%d ", data[i]);
	    }
	
	    printf("\n");
				        
	    return 0;
}

