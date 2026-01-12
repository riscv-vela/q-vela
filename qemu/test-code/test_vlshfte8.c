#include <stdio.h>
#include <stdint.h>

int main() {
	uint8_t data[16] = {97, 96, 86, 85, 84, 83};
	uint8_t read_data[8] = {0};
	uint64_t index = 6;

	printf("==================== INITIAL STATE ====================\n");

	printf("Heap: ");
	
	for (int i = 0; i < 16; i++) {
		printf("%2d ", data[i]);
	}
	printf("\n");
	printf("Index: %lu\n", index+1);
	
	printf("Read Data: ");
	
	for (int i = 0; i < 8; i++) {
		printf("%2d ", read_data[i]);
	}
	printf("\n");

	__asm__ volatile (
			"vsetvli t0, %2, e8, m1 \n"
			".word 0x1AF48007 \n"
			"vse8.v v0, (%3) \n"
			:
			: "r"(data), "r"(index), "r"(8), "r"(read_data)
			: "t0", "v0"
			);

	printf("\n=================== AFTER EXECUTION ===================\n");


	printf("Read Data: ");
	for (int i = 0; i < 8; i++) {
		printf("%2d ", read_data[i]);
	}
	printf("\n");

	
	return 0;
}

