#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	    uint8_t data[16] = {97, 96, 86, 85, 84, 83};
	    uint8_t write_data[8] = {86, 90, 97, 255, 255, 255, 255, 255};
	    uint64_t index = 6;

	    printf("==================== INITIAL STATE ====================\n");
            printf("Write Data: %2d %2d %2d %2d %2d %2d %2d %2d\n", write_data[0], write_data[1], write_data[2], write_data[3], write_data[4], write_data[5], write_data[6], write_data[7]);
	    printf("Index:  %lu\n", index+1);
            printf("Heap: ");
	    for (int i = 0; i < 16; i++) {
		    printf("%2d ", data[i]);
	    }
	    printf("\n");

	    __asm__ volatile (
			    "vsetvli t0, %2, e8, m1 \n"
			    "vle8.v v0, (%1) \n"
			    ".word 0x1AD40027 \n"
			    :
			    : "r"(data), "r"(write_data), "r"(VL), "r"(index)
			    : "t0", "v0"
			    );
	    printf("\n=================== AFTER EXECUTION ===================\n");

	    printf("Heap: ");
	    for (int i = 0; i < 16; i++) {
		    printf("%2d ", data[i]);
	    }
	
	    printf("\n");
				        
	    return 0;
}

