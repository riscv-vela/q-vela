#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <riscv_vector.h>

#define D 4096  // embedding vector size
#define VL 8    // 256bit / float32
#define M 10    // token position
#define IDX 0 // base index

int main(){
    static float x[D] = {0};   // input vector x
    static float y[D] = {0};   // output vector y

    for(int i = 0; i < D; i++){
        x[i] = (float)(i + 1);
    }

    uint16_t m = M;
    uint16_t idx = IDX;
    uint32_t x3 = ((uint32_t)idx << 16) | (uint32_t)m; // pack idx and m into x3

    asm volatile("vsetivli zero, 8, e32, m1, ta, ma" ::: "memory");

    for(int i = 0; i < D/VL; i++){
        asm volatile(
            "mv t0, %1\n\t"         // idx
            "vle32.v v2, (%0)\n\t"  // x[IDX...IDX+7]
            //"vfrope.fvx v3, v2, t0\n\t" // custom instruction
            //".word 0b010010(funct6) 1(no_mask) 00010(v2) 00101(t0(x5)) 101(funct3) 00011(v3) 1010111(opcode)\n\t"
            ".word 0x4A22D1D7\n\t"
            "vse32.v v3, (%2)\n\t"  // y[IDX...IDX+7]
            :
            : "r"(&x[idx]), "r"(x3), "r"(&y[idx])
            : "x3", "x31", "v2", "v3", "t0", "memory"
        );
        idx += VL;
        x3 += VL << 16; // update idx in x3
    }

    for(int i = 0; i < D; i+=64){
        printf("y[%d] = %f\n", i, y[i]);
    }

    return 0;
}