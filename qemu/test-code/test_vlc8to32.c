#include <stdio.h>
#include <stdint.h>

int main(){
	printf("\n=============BEFORE=================\n");
	uint8_t data[8] = {'n','a','m','e'};
	for(int i=0;i<4;i++){
		printf("%c, %d, size : %ld\n",data[i],data[i],sizeof(data[i]));
	}
	//printf("data_addr : %p",data);
	uint32_t  result[8]={0};
	__asm__ volatile(
			"vsetivli t0,8,e32,m1\n"
			"mv x10,data\n"
			".word 0x12056007\n"
			"vse32.v v0,(%0)\n"
			:
			:"r"(result),"r"(data)
			:"t0","v0","x10"
			);
	printf("\n====================AFTER EXECUTION=============\n");
	
	for(int i=0;i<4;i++){
		printf("%c, %d, size : %ld\n",result[i],result[i], sizeof(result[i]));
	}
	return 0;
}	

