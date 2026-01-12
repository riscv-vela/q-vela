#include <stdio.h>
#include <stdint.h>

#define VL 8

int main() {
	    uint64_t data[8] = {97, 96, 86, 85, 84, 83};

	    uint64_t max_val=UINT64_MAX;
	    uint64_t write_data[8] = {86, 90, 97,max_val, max_val, max_val, max_val, max_val};
	    uint64_t index = 6;

	    printf("==================== INITIAL STATE ====================\n");
            printf("Write Data: %2lu %2lu %2lu %2lu %2lu %2lu %2lu %2lu\n", write_data[0], write_data[1], write_data[2], write_data[3], write_data[4], write_data[5], write_data[6], write_data[7]);
	    printf("Index:  %lu\n", index+1);
            printf("Heap: ");
	    for (int i = 0; i < 8; i++) {
		    printf("%2lu ", data[i]);
	    }
	    printf("\n");
	    
	    __asm__ volatile (
			    "vsetvli t0, %2, e64, m1 \n"
			    "vle64.v v0, (%1) \n"
			    "mv x10,%0\n"
			    "mv x12,%3\n"
			    ".word 0x1AC57027\n"
			    :
			    : "r"(data), "r"(write_data), "r"(VL), "r"(index)
			    : "t0", "v0", "x10","x12"
			    );
	    printf("\n=================== AFTER EXECUTION ===================\n");

	    printf("Heap: ");
	    for (int i = 0; i < 8; i++) {
		    printf("%2lu ", data[i]);
	    }
	
	    printf("\n");
				        
	    return 0;
}

