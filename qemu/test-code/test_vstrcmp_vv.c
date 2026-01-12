#include <stdio.h>
#include <stdint.h>

int main() {
	
	char str1[8]={'n','a','m','e'};//'h','e','l','l'};
	char str2[8]={'n','a','i','i'};//'h','e','l','l'};
	int8_t result[8]={0};

        printf("====================== INITIAL STATE ======================\n");
	printf("str1 : ");
	for(int i=0;i<8;i++){
		printf("%c",str1[i]);
	}
	printf("\nstr2 : ");
	;
	for(int i=0;i<8;i++){
                printf("%c",str2[i]);
        }

	//printf("str1: %s, str2: %s\n", str1, str2);

	__asm__ volatile (
			"vsetivli t0, 8, e8, m1 \n"
			"vle8.v v1, (%0) \n"
			"vle8.v v2, (%1) \n"
			".word 0xFA208057 \n"	
			"vse8.v v0, (%2) \n"
			: 
			: "r"(str1), "r"(str2), "r"(result)
		      	: "t0", "v0","v1","v2"
			);


        printf("\n===================== AFTER EXECUTION =====================\n");
	printf("result : %d\n", result[0]);

	return 0;
}

