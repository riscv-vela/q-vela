#pragma once
#include <stdint.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdbool.h>

#define DIM 16

#define MVIN_SCALE_IDENTITY 1.0
#define ACC_SCALE_IDENTITY 1.0

#define OS 0
#define WS 1
#define GARBAGE_ADDR ((uint32_t)(-1))
#define OUTPUT_STATIONARY 0
#define WEIGHT_STATIONARY 1

#define CONFIG_EX 0
#define CONFIG_LD 1
#define CONFIG_ST 2
#define CONFIG_BERT 3

#define NO_ACTIVATION 0
#define RELU 1
#define LAYERNORM 2
#define IGELU 3
#define SOFTMAX 4

typedef int8_t elem_t;
typedef int32_t acc_t;

#define row_align(blocks) __attribute__((aligned(blocks*DIM*sizeof(elem_t))))
#define row_align_acc(blocks) __attribute__((aligned(blocks*DIM*sizeof(acc_t))))

typedef float scale_t;
typedef uint32_t scale_t_bits;
typedef int32_t scale_acc_t;
typedef uint32_t scale_acc_t_bits;
typedef float acc_scale_t;
typedef uint32_t acc_scale_t_bits;

static inline void gemmini_fence(void){
    asm volatile("" ::: "memory");
}

static inline void gemmini_dump_trace(void)
{  
    asm volatile (                                   
        ".word 0xFEB5307B\n"
        //1111111 01011 01010 011 00000 1111011
        : : : "memory"                            
    );
}
static inline void gemmini_init_trace(void)
{   
    asm volatile (                                   
        ".word 0xFCB5307B\n"
        //1111110 01011 01010 011 00000 1111011
        : : : "memory"                            
    );
}

static inline void gemmini_mvin(uint64_t dram_addr,
                                uint64_t sp_addr)
{  
    asm volatile (                                   
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x4B5307B\n"
        //0000010 01011 01010 011 00000 1111011
        :
        : "r"(dram_addr), "r"(sp_addr)      
        : "x10", "x11", "memory"                            
    );
}

static inline void gemmini_mvout(uint64_t dram_addr,
                                uint64_t sp_addr)
{  
    asm volatile (                                   
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x6B5307B\n"
        //0000011 01011 01010 011 00000 1111011
        :
        : "r"(dram_addr), "r"(sp_addr)  
        : "x10", "x11", "memory"                                  
    );
}

static inline void loop_ws_config_bounds(uint16_t I, uint16_t J, uint16_t K,
                                        uint16_t pad_I, uint16_t pad_J, uint16_t pad_K) {
    uint64_t rs1 = ((uint64_t)pad_K << 32) | ((uint64_t)pad_J << 16) | pad_I;
    uint64_t rs2 = ((uint64_t)K << 32) | ((uint64_t)J << 16) | I;

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x12B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void loop_ws_addrs_ab(int8_t *A, int8_t *B) {
    uint64_t rs1 = (uintptr_t)A;
    uint64_t rs2 = (uintptr_t)B;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x14B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void loop_ws_addrs_dc(acc_t *D, int8_t *C) {
    uint64_t rs1 = (uintptr_t)D;
    uint64_t rs2 = (uintptr_t)C;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x16B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void loop_ws_strides_ab(uint64_t A_stride, uint64_t B_stride) {
    uint64_t rs1 = A_stride;
    uint64_t rs2 = B_stride;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x18B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void loop_ws_strides_dc(uint64_t D_stride, uint64_t C_stride) {
    uint64_t rs1 = D_stride;
    uint64_t rs2 = C_stride;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x1AB5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void loop_ws(uint8_t A_transpose, uint8_t B_transpose, uint8_t full_C, uint8_t low_D, uint8_t ex_accumulate, uint8_t act, uint8_t a_spad_id, uint8_t b_spad_id, uint8_t c_spad_id, uint8_t is_resadd) {
    uint64_t rs1 = (ex_accumulate & 0x1)
                 | ((full_C & 0x1) << 1)
                 | ((low_D & 0x1) << 2)
                 | ((act & 0xFF) << 8)
		 | ((c_spad_id & 0x7)<<16)
		 | ((b_spad_id & 0x7)<<19)
		 | ((a_spad_id & 0x7)<<22); 
    uint64_t rs2 = (A_transpose & 0x1)
                 | ((B_transpose & 0x1) << 1)
                 | ((is_resadd & 0x1) << 2);

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x10B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void gemmini_loop_ws(uint16_t I, uint16_t J, uint16_t K,
                                   uint16_t pad_I, uint16_t pad_J, uint16_t pad_K,
                                   int8_t *A, int8_t *B, acc_t *D, int8_t *C,
                                   uint64_t A_stride, uint64_t B_stride,
                                   uint64_t D_stride, uint64_t C_stride,
                                   uint8_t A_transpose, uint8_t B_transpose,
                                   uint8_t full_C, uint8_t low_D,
                                   uint8_t ex_accumulate, uint8_t act,
                                   uint8_t a_spad_id, uint8_t b_spad_id, 
				   uint8_t c_spad_id,  uint8_t is_resadd) {
    loop_ws_config_bounds(I, J, K, pad_I, pad_J, pad_K);
    loop_ws_addrs_ab(A, B);
    loop_ws_addrs_dc(D, C);
    loop_ws_strides_ab(A_stride, B_stride);
    loop_ws_strides_dc(D_stride, C_stride);
    loop_ws(A_transpose, B_transpose, full_C, low_D,
                    ex_accumulate, act, a_spad_id, b_spad_id,c_spad_id, is_resadd);
}

static inline void ternary_loop_ws(uint8_t A_transpose, uint8_t B_transpose,
                                uint8_t full_C, uint8_t low_D, uint8_t ex_accumulate, uint8_t act, 
                                uint8_t a_spad_id, uint8_t b_spad_id, uint8_t c_spad_id,  uint8_t is_resadd) {
    uint64_t rs1 = (ex_accumulate & 0x1)
                 | ((full_C & 0x1) << 1)
                 | ((low_D & 0x1) << 2)
                 | ((act & 0xFF) << 8)
		 | ((c_spad_id & 0x7)<<16)
		 | ((b_spad_id & 0x7)<<19)
		 | ((a_spad_id & 0x7)<<22); 
    uint64_t rs2 = (A_transpose & 0x1)
                 | ((B_transpose & 0x1) << 1)
                 | ((is_resadd & 0x1) << 2)
                 | (1ULL << 20); // Set ternary flag

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x3CB5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void ternary_gemm_auto(uint16_t I, uint16_t J, uint16_t K,
                                   uint16_t pad_I, uint16_t pad_J, uint16_t pad_K,
                                   int8_t *A, int8_t *B, acc_t *D, int8_t *C,
                                   uint64_t A_stride, uint64_t B_stride,
                                   uint64_t D_stride, uint64_t C_stride,
                                   uint8_t A_transpose, uint8_t B_transpose,
                                   uint8_t full_C, uint8_t low_D,
                                   uint8_t ex_accumulate, uint8_t act,
                                   uint8_t a_spad_id, uint8_t b_spad_id, 
				   uint8_t c_spad_id,  uint8_t is_resadd) {
    loop_ws_config_bounds(I, J, K, pad_I, pad_J, pad_K);
    loop_ws_addrs_ab(A, B);
    loop_ws_addrs_dc(D, C);
    loop_ws_strides_ab(A_stride, B_stride);
    loop_ws_strides_dc(D_stride, C_stride);
    ternary_loop_ws(A_transpose, B_transpose, full_C, low_D,
                    ex_accumulate, act, a_spad_id, b_spad_id,c_spad_id, is_resadd);
}

static inline void k_gemv_loop_ws_config_bounds(uint16_t I, uint16_t J, uint16_t K,
                                        uint16_t pad_I, uint16_t pad_J, uint16_t pad_K) {
    uint64_t rs1 = ((uint64_t)pad_K << 32) | ((uint64_t)pad_J << 16) | pad_I;
    uint64_t rs2 = ((uint64_t)K << 32) | ((uint64_t)J << 16) | I;

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x30B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void k_gemv_loop_ws_addrs_ab(int8_t *A, int8_t *B) {
    uint64_t rs1 = (uintptr_t)A;
    uint64_t rs2 = (uintptr_t)B;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x32B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void k_gemv_loop_ws_addrs_dc(acc_t *D, int8_t *C) {
    uint64_t rs1 = (uintptr_t)D;
    uint64_t rs2 = (uintptr_t)C;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x34B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void k_gemv_loop_ws_strides_ab(uint64_t A_stride, uint64_t B_stride) {
    uint64_t rs1 = A_stride;
    uint64_t rs2 = B_stride;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x36B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void k_gemv_loop_ws_strides_dc(uint64_t D_stride, uint64_t C_stride) {
    uint64_t rs1 = D_stride;
    uint64_t rs2 = C_stride;
    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x38B5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void k_gemv_loop_ws(uint8_t A_transpose, uint8_t B_transpose, uint8_t full_C, uint8_t low_D, uint8_t ex_accumulate, uint8_t act, uint8_t a_spad_id, uint8_t b_spad_id, uint8_t c_spad_id, uint8_t is_resadd) {
    uint64_t rs1 = (ex_accumulate & 0x1)
                 | ((full_C & 0x1) << 1)
                 | ((low_D & 0x1) << 2)
                 | ((act & 0xFF) << 8)
		 | ((c_spad_id & 0x7)<<16)
		 | ((b_spad_id & 0x7)<<19)
		 | ((a_spad_id & 0x7)<<22); 
    uint64_t rs2 = (A_transpose & 0x1)
                 | ((B_transpose & 0x1) << 1)
                 | ((is_resadd & 0x1) << 2);

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x2EB5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

static inline void gemv_loop_ws(uint16_t I, uint16_t J, uint16_t K,
                                   uint16_t pad_I, uint16_t pad_J, uint16_t pad_K,
                                   int8_t *A, int8_t *B, acc_t *D, int8_t *C,
                                   uint64_t A_stride, uint64_t B_stride,
                                   uint64_t D_stride, uint64_t C_stride,
                                   uint8_t A_transpose, uint8_t B_transpose,
                                   uint8_t full_C, uint8_t low_D,
                                   uint8_t ex_accumulate, uint8_t act,
                                   uint8_t a_spad_id, uint8_t b_spad_id, 
				   uint8_t c_spad_id,  uint8_t is_resadd) {
    k_gemv_loop_ws_config_bounds(I, J, K, pad_I, pad_J, pad_K);
    k_gemv_loop_ws_addrs_ab(A, B);
    k_gemv_loop_ws_addrs_dc(D, C);
    k_gemv_loop_ws_strides_ab(A_stride, B_stride);
    k_gemv_loop_ws_strides_dc(D_stride, C_stride);
    k_gemv_loop_ws(A_transpose, B_transpose, full_C, low_D,
                    ex_accumulate, act, a_spad_id, b_spad_id,c_spad_id, is_resadd);
}

static acc_scale_t_bits acc_scale_t_to_acc_scale_t_bits(acc_scale_t x) {
	union {
		acc_scale_t_bits b;
		acc_scale_t f;
	} un;
        un.f = x;
        return un.b;
}
static scale_t_bits scale_t_to_scale_t_bits(scale_t x) {
       	union {
		scale_t_bits b;
		scale_t f;
	} un;
	un.f = x;
	return un.b;
}

void gemmini_extended3_config_ex(int dataflow, int sys_act, int sys_shift, int sys_acc_scale, int C_stride, int A_stride, bool A_transpose, bool B_transpose, bool set_only_strides){
	uint64_t rs1 = ((uint64_t)acc_scale_t_to_acc_scale_t_bits((acc_scale_t)sys_acc_scale) << 32) | 
		((uint64_t)(A_stride) << 16) |
                (B_transpose << 9) |
                (A_transpose << 8) |
                ((set_only_strides) << 7) |
                ((sys_act) << 3) |
                ((dataflow) << 2) |
                CONFIG_EX;
	uint64_t rs2 = ((uint64_t)(C_stride) << 48) | (sys_shift);
	
	__asm__ volatile(
			"mv x10, %0\n"
			"mv x11, %1\n"
			".word 0x00B5307B\n"
			:
			:"r"(rs1),"r"(rs2)
            : "x10", "x11", "memory"  
	);
}
void gemmini_extended2_config_ex(int dataflow, int sys_act, int sys_shift, int A_stride, bool A_transpose, bool B_transpose)
{
	gemmini_extended3_config_ex(dataflow, sys_act, sys_shift, ACC_SCALE_IDENTITY, 1, A_stride, A_transpose, B_transpose, false);
}

void gemmini_extended_config_ex(int dataflow, int sys_act, int sys_shift, int A_stride, bool A_transpose, bool B_transpose)
{
	gemmini_extended2_config_ex(dataflow, sys_act, sys_shift, A_stride, A_transpose, B_transpose);
}
void gemmini_config_ex(int dataflow, int sys_act, int sys_shift){
 	gemmini_extended_config_ex(dataflow, sys_act, sys_shift, 1,0,0);
}
void gemmini_extended5_config_ld(uint64_t stride,float scale,bool shrunk,uint16_t block_mvin_stride,uint8_t pixel_repeats,uint8_t id){

        uint64_t rs2 = stride;
	
	uint64_t rs1 =
		    ((uint64_t)(scale_t_to_scale_t_bits(scale)) << 32) |
		     ((uint64_t)(block_mvin_stride) << 16) |
		     ((uint64_t)(pixel_repeats) << 8) |
		     ((uint64_t)(id) << 3) |
		     ((uint64_t)(shrunk) << 2) |
		      CONFIG_LD;

        __asm__ volatile(
                        "mv x10, %0\n"
                        "mv x11, %1\n"
                        ".word 0x00B5307B\n"
                        :
                        :"r"(rs1),"r"(rs2)
                        :"x10", "x11", "memory"
        );
}
void gemmini_extended4_config_ld(uint64_t stride, float scale, bool shrunk, uint16_t block_mvin_stride, uint8_t id) {
    gemmini_extended5_config_ld(stride, scale, shrunk, block_mvin_stride, 1, id);
}

void gemmini_extended3_config_ld(uint64_t stride, float scale, bool shrunk, uint8_t id) {
    gemmini_extended4_config_ld(stride, scale, shrunk, DIM, id);
}

void gemmini_extended2_config_ld(uint64_t stride, float scale, bool shrunk) {
    gemmini_extended3_config_ld(stride, scale, shrunk, 0);
}

void gemmini_extended_config_ld(uint64_t stride, float scale) {
    gemmini_extended2_config_ld(stride, scale, false);
}

void gemmini_config_ld(uint64_t stride) {
    gemmini_extended_config_ld(stride, MVIN_SCALE_IDENTITY);
}
void gemmini_extended2_config_st(uint32_t stride, uint8_t acc_act, float acc_scale,
    uint8_t pool_stride,uint8_t pool_size, uint8_t pool_out_dim,uint8_t porows,
    uint8_t pocols,uint8_t orows,uint8_t ocols,uint8_t upad,uint8_t lpad
) {
    uint64_t rs1 =
        ((uint64_t)ocols << 56) |
        ((uint64_t)orows << 48) |
        ((uint64_t)pocols << 40) |
        ((uint64_t)porows << 32) |
        ((uint64_t)pool_out_dim << 24) |
        ((uint64_t)lpad << 10) |
        ((uint64_t)upad << 8) |
        ((uint64_t)pool_size << 6) |
        ((uint64_t)pool_stride << 4) |
        ((uint64_t)acc_act << 2) |
        CONFIG_ST;

    uint64_t rs2 =((uint64_t)acc_scale_t_to_acc_scale_t_bits(acc_scale) << 32) |
        ((uint64_t)stride);
        __asm__ volatile(
                        "mv x10, %0\n"
                        "mv x11, %1\n"
                        ".word 0x00B5307B\n"
                        :
                        :"r"(rs1),"r"(rs2)
                        :"x10", "x11", "memory"
        );
}
void gemmini_extended_config_st(uint32_t stride, uint8_t acc_act, float acc_scale) {
    gemmini_extended2_config_st(stride, acc_act, acc_scale,
        0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void gemmini_config_st(uint32_t stride) {
    gemmini_extended_config_st(stride, NO_ACTIVATION, ACC_SCALE_IDENTITY);
}

void gemmini_config_norm(uint32_t q_const, uint8_t q_const_type, uint8_t set_stats_id_only,uint8_t act_msb, uint8_t stat_id, uint32_t igelu_qb,uint32_t igelu_qc)
{
    uint64_t rs1 = ((uint64_t)q_const << 32)
                 | ((uint64_t)(q_const_type & 1) << 18)
                 | ((uint64_t)(set_stats_id_only & 1) << 17)
                 | ((uint64_t)(act_msb & 1) << 16)
                 | ((uint64_t)stat_id << 8)
		 | CONFIG_BERT;
    uint64_t rs2 = ((uint64_t)igelu_qc << 32)
                 | ((uint64_t)igelu_qb);
    __asm__ volatile(
		    "mv x10, %0\n"
                    "mv x11, %1\n"
                    ".word 0x00B5307B\n"
                    :
                    :"r"(rs1),"r"(rs2)
                    : "x10", "x11", "memory"  
    );
}

