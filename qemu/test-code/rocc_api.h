#pragma once
#include <stdint.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

#define DIM 16
#define ADDR_LEN 32
#define BANK_NUM 4
#define BANK_ROWS 4096
#define ACC_ROWS 1024
#define MAX_BYTES 64
#define MAX_BLOCK_LEN (MAX_BYTES/(DIM*1))
#define MAX_BLOCK_LEN_ACC 1

#define MVIN_SCALE_IDENTITY 1.0
#define ACC_SCALE_IDENTITY 1.0

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
static const elem_t elem_t_max = 127;
static const elem_t elem_t_min = -128;

#define row_align(blocks) __attribute__((aligned(blocks*DIM*sizeof(elem_t))))
#define row_align_acc(blocks) __attribute__((aligned(blocks*DIM*sizeof(acc_t))))

typedef float scale_t;
typedef uint32_t scale_t_bits;
typedef int32_t scale_acc_t;
typedef uint32_t scale_acc_t_bits;
typedef float acc_scale_t;
typedef uint32_t acc_scale_t_bits;

static inline void gemmini_heavy_compute_only(uint64_t iters)
{
    register uint64_t a0 asm("x10") = iters; // rs1
    register uint64_t a1 asm("x11") = 0;     // rs2
    asm volatile(
        ".word 0x7ab5307b\n" // funct7=0x3d, rs2=x11, rs1=x10, funct3=3, rd=x0, opcode=0x7b
        :
        : "r"(a0), "r"(a1)
        : "memory");
}

static inline void gemmini_fence(void){
    asm volatile (
        ".word 0x7EB5307B\n"
        : : : "memory"
    );
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

static inline void loop_ws_addrs_ab(const elem_t *A, const elem_t *B) {
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

static inline void loop_ws_addrs_dc(const acc_t *D, elem_t *C) {
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

static inline void loop_ws(uint8_t A_transpose, uint8_t B_transpose, uint8_t full_C, uint8_t low_D, uint8_t ex_accumulate, uint8_t act, uint8_t a_spad_id, uint8_t b_spad_id, uint8_t is_resadd, uint8_t is_mgpemm) {
    uint64_t rs1 = (ex_accumulate & 0x1)
                 | ((full_C & 0x1) << 1)
                 | ((low_D & 0x1) << 2)
                 | ((act & 0xFF) << 8)
		 | ((b_spad_id & 0x7)<<16)
		 | ((a_spad_id & 0x7)<<18)
         | ((is_mgpemm & 0x1)<<20);
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
                                   const elem_t *A, const elem_t *B, const acc_t *D, elem_t *C,
                                   uint64_t A_stride, uint64_t B_stride,
                                   uint64_t D_stride, uint64_t C_stride,
                                   uint8_t A_transpose, uint8_t B_transpose,
                                   uint8_t full_C, uint8_t low_D,
                                   uint8_t ex_accumulate, uint8_t act,
                                   uint8_t a_spad_id, uint8_t b_spad_id, 
				   uint8_t is_resadd, uint8_t is_mgpemm) {
    loop_ws_config_bounds(I, J, K, pad_I, pad_J, pad_K);
    loop_ws_addrs_ab(A, B);
    loop_ws_addrs_dc(D, C);
    loop_ws_strides_ab(A_stride, B_stride);
    loop_ws_strides_dc(D_stride, C_stride);
    loop_ws(A_transpose, B_transpose, full_C, low_D,
                    ex_accumulate, act, a_spad_id, b_spad_id, is_resadd, is_mgpemm);
}

static inline void ternary_loop_ws(uint8_t A_transpose, uint8_t B_transpose,
                                uint8_t full_C, uint8_t low_D, uint8_t ex_accumulate, uint8_t act, 
                                uint8_t a_spad_id, uint8_t b_spad_id, uint8_t c_spad_id, uint8_t is_resadd) {
    uint64_t rs1 = (ex_accumulate & 0x1)
                 | ((full_C & 0x1) << 1)
                 | ((low_D & 0x1) << 2)
                 | ((act & 0xFF) << 8)
		 | ((b_spad_id & 0x7)<<16)
		 | ((a_spad_id & 0x7)<<18)
         | ((1ULL & 0x1)<<20);
    uint64_t rs2 = (A_transpose & 0x1)
                 | ((B_transpose & 0x1) << 1)
                 | ((is_resadd & 0x1) << 2);

    __asm__ volatile (
        "mv x10, %0\n"
        "mv x11, %1\n"
        ".word 0x3CB5307B\n"
        :
        : "r"(rs1), "r"(rs2)
        : "x10", "x11", "memory"  
    );
}

// static inline void ternary_gemm_auto(uint16_t I, uint16_t J, uint16_t K,
//                                    uint16_t pad_I, uint16_t pad_J, uint16_t pad_K,
//                                    int8_t *A, int8_t *B, acc_t *D, int8_t *C,
//                                    uint64_t A_stride, uint64_t B_stride,
//                                    uint64_t D_stride, uint64_t C_stride,
//                                    uint8_t A_transpose, uint8_t B_transpose,
//                                    uint8_t full_C, uint8_t low_D,
//                                    uint8_t ex_accumulate, uint8_t act,
//                                    uint8_t a_spad_id, uint8_t b_spad_id, 
// 				   uint8_t c_spad_id,  uint8_t is_resadd) {
//     loop_ws_config_bounds(I, J, K, pad_I, pad_J, pad_K);
//     loop_ws_addrs_ab(A, B);
//     loop_ws_addrs_dc(D, C);
//     loop_ws_strides_ab(A_stride, B_stride);
//     loop_ws_strides_dc(D_stride, C_stride);
//     ternary_loop_ws(A_transpose, B_transpose, full_C, low_D,
//                     ex_accumulate, act, a_spad_id, b_spad_id,c_spad_id, is_resadd);
// }

// static inline void k_gemv_loop_ws_config_bounds(uint16_t I, uint16_t J, uint16_t K,
//                                         uint16_t pad_I, uint16_t pad_J, uint16_t pad_K) {
//     uint64_t rs1 = ((uint64_t)pad_K << 32) | ((uint64_t)pad_J << 16) | pad_I;
//     uint64_t rs2 = ((uint64_t)K << 32) | ((uint64_t)J << 16) | I;

//     __asm__ volatile (
//         "mv x10, %0\n"
//         "mv x11, %1\n"
//         ".word 0x30B5307B\n"
//         :
//         : "r"(rs1), "r"(rs2)
//         : "x10", "x11", "memory"  
//     );
// }

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


/* Below Are given in gemmini.h, still under Construction */

#define GEMMINI_ASSERTIONS

enum tiled_matmul_type_t {OS, WS, CPU};

#define ROUNDING_RIGHT_SHIFT(x, shift) \
    ((shift) > 0 ? (((x) >> (shift)) + \
        (((shift) == 0 ? 0 : (((x) >> ((shift)-1)) & 1)) & \
             ((((shift) <= 1 ? 0 : ((x) & ((1 << ((shift)-1)) - 1))) != 0) | (((x) >> (shift)) & 1)))) : ((x) << (-(shift))))

static double ROUND_NEAR_EVEN(double x) {
    long long i = (long long)x;
    long long next = x < 0 ? i - 1 : i + 1;
    double rem = x - i;
    rem = rem < 0 ? -rem : rem;
    if (rem < 0.5) {
        return (double)i;
    } else if (rem > 0.5) {
        return (double)next;
    } else {
        // rem == -1.5일 때 짝수로 반올림
        return (i % 2 == 0) ? (double)i : (double)next;
    }
}

#define ACC_SCALE(x, scale) \
    ({float y = ROUND_NEAR_EVEN((x) * (scale)); y > INT8_MAX ? INT8_MAX : (y < INT8_MIN ? INT8_MIN : (acc_t)y);})


static acc_t int_sqrt(acc_t n) {
  if (n == 0) return 0;

  int bits = 0;
  for (acc_t x = n; x > 0; x /= 2)
    bits++;

  acc_t x_prev = 1 << ((bits + 1) / 2);

  while (1) {
    acc_t x_next = (x_prev + n / x_prev) / 2;
    if (x_next >= x_prev) return x_prev;
    x_prev = x_next;
  };
}

static elem_t scale_and_sat(acc_t x, int act, acc_scale_t scale, acc_scale_t bert_scale) {
  // Apply I-GELU if needed
  if (act == IGELU) {
    const acc_scale_t sqrt_2 = 1.41421356237;

    const acc_scale_t S = bert_scale;

    const acc_scale_t S_erf = (-0.2888 * (S/sqrt_2)*(S/sqrt_2));
    const acc_t q1 = 1 / S_erf;
    const acc_t qb = -1.769 / (S / sqrt_2);
    const acc_t qc = 1.0 / (-0.2888 * (S / sqrt_2) * (S / sqrt_2));

    const acc_t q = x;

    const acc_t q_sign = q < 0 ? -1 : 1;
    const acc_t q_clipped = abs(q) > (-qb) ? (-qb) : abs(q);
    const acc_t q_poly = (q_clipped + qb)*(q_clipped + qb) + qc;
    const acc_t q_erf = q_sign * q_poly;

    x = q * (q_erf + q1);
  }

  // Scale value down and round it
  x = ACC_SCALE(x, scale);
  // Clip result
  x = x > elem_t_max ? elem_t_max : (x < elem_t_min ? elem_t_min : x);
  // Apply activation function
  if (act == RELU) {
    x = x < 0 ? 0 : x;
  }
  return x;
}

#ifdef HAS_MVIN_SCALE
#define GEMMINI_SCALE(x, scale) MVIN_SCALE((x), (scale))
#else
#define GEMMINI_SCALE(x, scale) (x)
#endif

#ifdef HAS_MVIN_ACC_SCALE
#define GEMMINI_ACC_SCALE(x, scale) MVIN_SCALE_ACC((x), (scale))
#else
#define GEMMINI_ACC_SCALE(x, scale) (x)
#endif

static void matmul_cpu(bool transA, bool transB, size_t DIM_I, size_t DIM_J, size_t DIM_K,
        const elem_t* A, const elem_t* B, const acc_t * D,
        elem_t* C,
        size_t stride_A, size_t stride_B, size_t stride_D, size_t stride_C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        int act, acc_scale_t scale, acc_scale_t bert_scale, bool repeating_bias) {

  const int no_bias = D == NULL;
  if (act != LAYERNORM && act != SOFTMAX && !transA && !transB && DIM_I % 4 == 0 && DIM_J % 4 == 0) {
    for (size_t i = 0; i < DIM_I; i += 4) {
      for (size_t j = 0; j < DIM_J; j += 4) {

        acc_t result[4][4]; // = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

        for (size_t ii = 0; ii < 4; ii++)
          for (size_t jj = 0; jj < 4; jj++) {
            const size_t bias_row = repeating_bias ? 0 : i + ii;
            result[ii][jj] = no_bias ? 0 :
              GEMMINI_ACC_SCALE(*(D + bias_row*stride_D + j + jj), D_scale_factor);
          }

        for (size_t k = 0; k < DIM_K; k++) {
          result[0][0] +=
                GEMMINI_SCALE(*(A + i*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j), B_scale_factor);
          result[0][1] +=
                GEMMINI_SCALE(*(A + i*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+1), B_scale_factor);
          result[0][2] +=
                GEMMINI_SCALE(*(A + i*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+2), B_scale_factor);
          result[0][3] +=
                GEMMINI_SCALE(*(A + i*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+3), B_scale_factor);
          result[1][0] +=
                GEMMINI_SCALE(*(A + (i+1)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j), B_scale_factor);
          result[1][1] +=
                GEMMINI_SCALE(*(A + (i+1)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+1), B_scale_factor);
          result[1][2] +=
                GEMMINI_SCALE(*(A + (i+1)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+2), B_scale_factor);
          result[1][3] +=
                GEMMINI_SCALE(*(A + (i+1)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+3), B_scale_factor);
          result[2][0] +=
                GEMMINI_SCALE(*(A + (i+2)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j), B_scale_factor);
          result[2][1] +=
                GEMMINI_SCALE(*(A + (i+2)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+1), B_scale_factor);
          result[2][2] +=
                GEMMINI_SCALE(*(A + (i+2)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+2), B_scale_factor);
          result[2][3] +=
                GEMMINI_SCALE(*(A + (i+2)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+3), B_scale_factor);
          result[3][0] +=
                GEMMINI_SCALE(*(A + (i+3)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j), B_scale_factor);
          result[3][1] +=
                GEMMINI_SCALE(*(A + (i+3)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+1), B_scale_factor);
          result[3][2] +=
                GEMMINI_SCALE(*(A + (i+3)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+2), B_scale_factor);
          result[3][3] +=
                GEMMINI_SCALE(*(A + (i+3)*stride_A + k), A_scale_factor) *
                GEMMINI_SCALE(*(B + k*stride_B + j+3), B_scale_factor);
        }

        *(C + i*stride_C + j) =
             scale_and_sat(result[0][0], act, scale, bert_scale);
        *(C + i*stride_C + j+1) =
             scale_and_sat(result[0][1], act, scale, bert_scale);
        *(C + i*stride_C + j+2) =
             scale_and_sat(result[0][2], act, scale, bert_scale);
        *(C + i*stride_C + j+3) =
             scale_and_sat(result[0][3], act, scale, bert_scale);
        *(C + (i+1)*stride_C + j) =
             scale_and_sat(result[1][0], act, scale, bert_scale);
        *(C + (i+1)*stride_C + j+1) =
             scale_and_sat(result[1][1], act, scale, bert_scale);
        *(C + (i+1)*stride_C + j+2) =
             scale_and_sat(result[1][2], act, scale, bert_scale);
        *(C + (i+1)*stride_C + j+3) =
             scale_and_sat(result[1][3], act, scale, bert_scale);
        *(C + (i+2)*stride_C + j) =
             scale_and_sat(result[2][0], act, scale, bert_scale);
        *(C + (i+2)*stride_C + j+1) =
             scale_and_sat(result[2][1], act, scale, bert_scale);
        *(C + (i+2)*stride_C + j+2) =
             scale_and_sat(result[2][2], act, scale, bert_scale);
        *(C + (i+2)*stride_C + j+3) =
             scale_and_sat(result[2][3], act, scale, bert_scale);
        *(C + (i+3)*stride_C + j) =
             scale_and_sat(result[3][0], act, scale, bert_scale);
        *(C + (i+3)*stride_C + j+1) =
             scale_and_sat(result[3][1], act, scale, bert_scale);
        *(C + (i+3)*stride_C + j+2) =
             scale_and_sat(result[3][2], act, scale, bert_scale);
        *(C + (i+3)*stride_C + j+3) =
             scale_and_sat(result[3][3], act, scale, bert_scale);
      }
    }
  } else {
    size_t A_dim_strides[2] = {!transA ? stride_A : 1, !transA ? 1 : stride_A}; // i, j stride
    size_t B_dim_strides[2] = {!transB ? 1 : stride_B, !transB ? stride_B : 1}; // j, k stride

    // We also create a buffer that we can use for layernorms and softmaxes
    static acc_t c_buffer[1024];
    const size_t c_buffer_sz = sizeof(c_buffer)/sizeof(c_buffer[0]);
    if ((act == LAYERNORM || act == SOFTMAX) && DIM_J > c_buffer_sz) {
      printf("Matmul is too large to normalize\n");
      exit(1);
    }

    for (size_t i = 0; i < DIM_I; i++) {
      for (size_t j = 0; j < DIM_J; j++) {
        elem_t* c = C + (i * stride_C) + j;

        const size_t bias_row = repeating_bias ? 0 : i;
        acc_t sum = no_bias ? 0 : GEMMINI_ACC_SCALE(*(D + bias_row * stride_D + j), D_scale_factor);

        for (size_t k = 0; k < DIM_K; k++) {
          const elem_t* a = A + i * A_dim_strides[0] + k * A_dim_strides[1];
          const elem_t* b = B + j * B_dim_strides[0] + k * B_dim_strides[1];
          sum += (GEMMINI_SCALE(*a, A_scale_factor) * GEMMINI_SCALE(*b, B_scale_factor));
        }

        if (act == LAYERNORM || act == SOFTMAX)
          c_buffer[j] = sum;
        else
          *c = scale_and_sat(sum, act, scale, bert_scale);
      }

      if (act == LAYERNORM) {
        acc_t sum = 0;
        for (size_t j = 0; j < DIM_J; j++)
          sum += c_buffer[j];
        acc_t mean = sum / (acc_t)DIM_J;

        acc_t total_err_sq = 0;
        for (size_t j = 0; j < DIM_J; j++)
          total_err_sq += (c_buffer[j] - mean)*(c_buffer[j] - mean);
        acc_t variance = total_err_sq / (acc_t)DIM_J;

        acc_t stddev = int_sqrt(variance);
        if (variance == 0) stddev = 1;

        for (size_t j = 0; j < DIM_J; j++) {
          c_buffer[j] -= mean;
          // c_buffer[j] /= stddev;
          c_buffer[j] = ROUND_NEAR_EVEN((double)c_buffer[j] / stddev); // TODO I don't think I-BERT uses round-near-even, so we shouldn't either. We just use this rounding mode here in order to match the hardware.

          elem_t* c = C + (i * stride_C) + j;
          *c = scale_and_sat(c_buffer[j], act, scale, bert_scale);
        }
      } else if (act == SOFTMAX) {
        const scale_t a = 0.3585;
        const scale_t b = 1.353;
        const scale_t c = 0.344;

        // is SCALE supposed to be input scale?
        const acc_t qln2 = (acc_t) (0.693147 / bert_scale);
        const acc_t qln2_inv = 65536 / qln2;
        const acc_t qb = b / bert_scale;
        const acc_t qc = c / (a*bert_scale*bert_scale);

        // pass 1: get max_q
        acc_t max_q = -2147483648;
        for (size_t j = 0; j < DIM_J; j++) {
          if (c_buffer[j] > max_q) max_q = c_buffer[j];
        }

        // pass 2: calculate iexp(q_tilde) and sum(q_tilde)
        acc_t sum_exp = 0;
        for (size_t j = 0; j < DIM_J; j++) {
          acc_t q = c_buffer[j] - max_q;
          acc_t z = (acc_t) (-q * qln2_inv) >> 16;
          acc_t qp = q + z * qln2;
          acc_t q_exp = (qp + qb)*(qp + qb) + qc;
          c_buffer[j] = q_exp >> z;
          sum_exp += c_buffer[j];
        }

        // pass 3: divide by sum
        scale_t factor = (127.f) / (float) sum_exp; // what corresponds to 1 in output?
        for (size_t j = 0; j < DIM_J; j++) {
          elem_t* c = C + (i * stride_C) + j;
          *c = scale_and_sat(c_buffer[j], act, factor, bert_scale);
        }
      }
    }
  }
}

#undef GEMMINI_SCALE

// Tiling functions
/*
static void sp_tiled_matmul_os(const elem_t * A, const elem_t * B, const void * D, void * C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        size_t I, size_t J, size_t K, size_t pad_I, size_t pad_J, size_t pad_K,
        size_t A_row_stride, size_t B_row_stride, size_t D_row_stride, size_t C_row_stride,
        bool a_transpose, bool b_transpose,
        bool full_C, bool low_D,
        bool no_bias, bool repeating_bias,
        int act,
        int a_spad_id, int b_spad_id) {

  const uint32_t A_sp_addr_start = 0;
  const uint32_t B_sp_addr_start = BANK_NUM * BANK_ROWS - K * J * DIM;
  const uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
  const uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3));

  const int A_blocks = K <= MAX_BLOCK_LEN ? K : MAX_BLOCK_LEN;
  const int B_blocks = J <= MAX_BLOCK_LEN ? J : MAX_BLOCK_LEN;
  const int D_blocks = J <= MAX_BLOCK_LEN_ACC ? J : MAX_BLOCK_LEN_ACC;

  // Move-in D
  if (D != NULL && !no_bias) {
    const size_t D_stride = repeating_bias ? 0 : D_row_stride * sizeof(acc_t);
    gemmini_extended_config_ld(D_stride, D_scale_factor);

    for (size_t i = 0; i < I; i++) {
      for (size_t j = 0; j < J; j += D_blocks) {
        const size_t bias_row = repeating_bias ? 0 : i;
        const acc_t * const D_dram_addr = (acc_t *)D + (bias_row * D_row_stride + j)*DIM;

        const uint32_t D_sp_addr_acc = D_sp_addr_start + (i*J + j)*DIM;

        const size_t blocks = j + D_blocks <= J ? D_blocks : J-j;

        const size_t cols = blocks * DIM - (j + blocks >= J ? pad_J : 0);
        const size_t rows = DIM - (i == I-1 ? pad_I : 0);

        gemmini_extended_mvin(D_dram_addr, D_sp_addr_acc, cols, rows);
      }
    }
  }

  // Move-in B
  gemmini_extended_config_ld(B_row_stride * sizeof(elem_t), B_scale_factor);
  for (size_t j = 0; j < J; j += B_blocks) {
    for (size_t k = 0; k < K; k++) {
      const elem_t * const B_dram_addr = B + (k*B_row_stride + j)*DIM;
      const uint32_t B_sp_addr = B_sp_addr_start + (k*J + j)*DIM;
      const size_t blocks = j + B_blocks <= J ? B_blocks : J-j;
      const size_t cols = blocks * DIM - (j + blocks >= J ? pad_J : 0);
      const size_t rows = DIM - (k == K-1 ? pad_K : 0);
      gemmini_extended_mvin(B_dram_addr, B_sp_addr, cols, rows);
    }
  }

  // Move-in A
  gemmini_extended_config_ld(A_row_stride * sizeof(elem_t), A_scale_factor);
  for (size_t i = 0; i < I; i++) {
    for (size_t k = 0; k < K; k += A_blocks) {
      const elem_t * const A_dram_addr = A + (i*A_row_stride + k)*DIM;
      const uint32_t A_sp_addr = A_sp_addr_start + (i*K + k)*DIM;
      const size_t blocks = k + A_blocks <= K ? A_blocks : K-k;
      const size_t cols = blocks * DIM - (k + blocks >= K ? pad_K : 0);
      const size_t rows = DIM - (i == I-1 ? pad_I : 0);
      gemmini_extended_mvin(A_dram_addr, A_sp_addr, cols, rows);
    }
  }

  for (size_t i = 0; i < I; i++) {
    for (size_t j = 0; j < J; j++) {
      const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j)*DIM;

      for (size_t k = 0; k < K; k++) {

        const uint32_t A_sp_addr = A_sp_addr_start + (i*K + k)*DIM;
        const uint32_t B_sp_addr = B_sp_addr_start + (k*J + j)*DIM;

        uint32_t out_sp_addr = k == K-1 ? C_sp_addr : GARBAGE_ADDR;

        // If we're not using a bias, then we want to overwrite what's in the
        // accumulator, rather than writing over it
        int no_bias_new_matrix = no_bias && D != NULL && k == K-1;
        if (no_bias_new_matrix) {
          out_sp_addr &= ~(1 << (ADDR_LEN-2));
        }

        const size_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
        const size_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
        const size_t B_cols = DIM - (j == J - 1 ? pad_J : 0);
        const size_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
        const size_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
        const size_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

        gemmini_extended_preload(GARBAGE_ADDR, out_sp_addr, DIM, DIM, C_cols, C_rows);

        if (k == 0) { // First iteration
          gemmini_extended_compute_preloaded(A_sp_addr, B_sp_addr, A_cols, A_rows, B_cols, B_rows);
        } else { // All other iterations
          gemmini_extended_compute_accumulated(A_sp_addr, B_sp_addr, A_cols, A_rows, B_cols, B_rows);
        }
      }
    }
  }

  // Move-out C
  if (C != NULL) {
    const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);

    for (size_t i = 0; i < I; i++) {
      for (size_t j = 0; j < J; j++) {
        void * const C_dram_addr = (int8_t*)C + (i*C_row_stride + j)*DIM*sizeof_C;
        const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j)*DIM;

        const size_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
        const size_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

        gemmini_extended_mvout(C_dram_addr, C_sp_addr, C_cols, C_rows);
      }
    }
  }
}
*/

static void sp_tiled_matmul_ws(const elem_t * A, const elem_t * B,
        const void * D, void * C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        size_t I, size_t J, size_t K, size_t pad_I, size_t pad_J, size_t pad_K,
        size_t A_row_stride, size_t B_row_stride, size_t D_row_stride, size_t C_row_stride,
        bool a_transpose, bool b_transpose,
        bool full_C, bool low_D,
        bool no_bias, bool repeating_bias,
        int act,
        int a_spad_id, int b_spad_id, bool is_mpgemm) {
#ifdef PRINT_TILE
#if PRINT_TILE
  printf("[sp_tiled_matmul_ws] I=%d J=%d K=%d | pad_I=%d pad_J=%d pad_K=%d | "
         "A_rs=%d B_rs=%d D_rs=%d C_rs=%d | At=%d Bt=%d | full_C=%d low_D=%d | "
         "no_bias=%d rep_bias=%d | act=%d | a_spad_id=%d b_spad_id=%d | is_mpgemm=%d\n",
         I, J, K, pad_I, pad_J, pad_K,
         A_row_stride, B_row_stride, D_row_stride, C_row_stride,
         (int)a_transpose, (int)b_transpose,
         (int)full_C, (int)low_D,
         (int)no_bias, (int)repeating_bias,
         act, a_spad_id, b_spad_id, (int)is_mpgemm);
#endif
#endif
  // Combined loop
  gemmini_loop_ws(I, J, K, pad_I, pad_J, pad_K, A, B, no_bias ? NULL : D, C,
    A_row_stride, B_row_stride, repeating_bias ? 0 : D_row_stride, C_row_stride,
    a_transpose, b_transpose,
    full_C, low_D, !no_bias || D == NULL,
    act, a_spad_id, b_spad_id, false, is_mpgemm);
}

static void tiled_matmul_outer(size_t dim_I, size_t dim_J, size_t dim_K,
        const elem_t* A, const elem_t* B,
        const void * D, void * C,
        size_t stride_A, size_t stride_B, size_t stride_D, size_t stride_C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        size_t tile_I, size_t tile_J, size_t tile_K,
        int act, acc_scale_t scale, acc_scale_t bert_scale,
        bool repeating_bias,
        bool a_transpose, bool b_transpose,
        bool full_C, bool low_D,
        uint8_t weightA,
        int dataflow, bool is_mpgemm) {

  const size_t dim_I_padded = (dim_I / DIM + (dim_I % DIM != 0)) * DIM;
  const size_t dim_J_padded = (dim_J / DIM + (dim_J % DIM != 0)) * DIM;
  const size_t dim_K_padded = (dim_K / DIM + (dim_K % DIM != 0)) * DIM;

  const size_t I0 = dim_I_padded / (tile_I*DIM) + (dim_I_padded % (tile_I*DIM) != 0);
  const size_t J0 = dim_J_padded / (tile_J*DIM) + (dim_J_padded % (tile_J*DIM) != 0);
  const size_t K0 = dim_K_padded / (tile_K*DIM) + (dim_K_padded % (tile_K*DIM) != 0);

  // These lines here are supposed to help us deal with when the dimensions of
  // the systolic array aren't divisible by the tiling factors
  const size_t last_I = dim_I_padded % (tile_I*DIM) == 0 ? tile_I : (dim_I_padded/DIM) % tile_I;
  const size_t last_J = dim_J_padded % (tile_J*DIM) == 0 ? tile_J : (dim_J_padded/DIM) % tile_J;
  const size_t last_K = dim_K_padded % (tile_K*DIM) == 0 ? tile_K : (dim_K_padded/DIM) % tile_K;

  // These lines are supposed to figure out how much padding the hardware is
  // supposed to add for the final tile
  const size_t padding_I = dim_I_padded - dim_I;
  const size_t padding_J = dim_J_padded - dim_J;
  const size_t padding_K = dim_K_padded - dim_K;

  const bool no_bias = D == NULL;

  if (no_bias) {
    D = (void*) 1; // Dummy address which isn't NULL
  }

  const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t) ;
  const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);

  gemmini_extended_config_ex(dataflow, act & 3, 0, 1, a_transpose, b_transpose);
  gemmini_extended_config_st(stride_C * sizeof_C, act & 3, scale);
  gemmini_extended3_config_ld(stride_A * sizeof(elem_t), A_scale_factor, false, 0);
  gemmini_extended3_config_ld(stride_B * sizeof(elem_t), B_scale_factor, false, 1);
  gemmini_extended3_config_ld(repeating_bias ? 0 : (stride_D * sizeof_D), D_scale_factor, low_D, 2);

  if (act == IGELU) {
    const acc_scale_t sqrt_2 = 1.41421356237;
    const acc_scale_t S = bert_scale;
    const acc_scale_t S_erf = (-0.2888 * ((S*S) / 2));

    const acc_t qb = -1.769 / (S / sqrt_2);
    const acc_t qc = 1.0 / S_erf;

    gemmini_config_norm(0, 0, 0, 0, 0, qb, qc);
  }

  if (act == SOFTMAX) {
    const scale_t a = 0.3585;
    const scale_t b = 1.353;
    const scale_t c = 0.344;

    const acc_t qln2 = (int) (0.693147 / bert_scale);
    const acc_t qln2_inv = 65536 / qln2;
    const acc_t qb = b / bert_scale;
    const acc_t qc = c / (a*bert_scale*bert_scale);

    gemmini_config_norm(qln2, 0, 0, 1, 0, qb, qc);
    gemmini_config_norm(qln2_inv, 1, 0, 1, 0, qb, qc);
  }

  void (*inner)(const elem_t *, const elem_t *, const void *, void *,
        scale_t, scale_t, scale_acc_t,
        size_t, size_t, size_t, size_t, size_t, size_t,
        size_t, size_t, size_t, size_t,
        bool, bool,
        bool, bool,
        bool, bool,
        int, int, int, bool);

  if (dataflow == OUTPUT_STATIONARY) {
    //inner = &sp_tiled_matmul_os;
  } else /* if (dataflow == WEIGHT_STATIONARY) */ {
    inner = &sp_tiled_matmul_ws;
  }

  // reuse operand if it fits scratchpad
  int a_spad_id = 0;
  int b_spad_id = 0;
  bool b_reuse = (J0 * K0 <= 2) && (dataflow == WEIGHT_STATIONARY);
  bool a_reuse = (I0 * K0 <= 2) && (dataflow == WEIGHT_STATIONARY);

  for (size_t i0 = 0; i0 < I0; i0++)
    for (size_t j0 = 0; j0 < J0; j0++)
      for (size_t k0 = 0; k0 < K0; k0++) {
        if(a_reuse)
          a_spad_id = ((i0+k0) == 0) ? 1 : 2;
        if(b_reuse)
          b_spad_id = ((j0+k0) == 0) ? 1 : 2;

        // Calculate the standard column-wise offset based on j0
        size_t j_offset = j0 * tile_J * DIM;

        // If is_mpgemm is true, multiply the column offset by 4 to compensate
        if (is_mpgemm) {
            j_offset *= 4;
        }

        const void * pre;
        if (k0 != 0) {
          pre = NULL;
        } else {
          size_t bias_row = repeating_bias ? 0 : i0*tile_I*DIM;
          // pre = &(((acc_t*)D)[bias_row * stride_D + j0 * tile_J * DIM]);
          // pre = (int8_t*)D + (bias_row * stride_D + j0 * tile_J * DIM)*sizeof_D;
          pre = (int8_t*)D + (bias_row * stride_D + j_offset )*sizeof_D;
        }

        // void * out = k0 == K0-1 ? (int8_t*)C + (i0*tile_I*DIM*stride_C + j0*tile_J*DIM)*sizeof_C : NULL;


        // Calculate the final 'out' pointer using the (potentially adjusted) j_offset
        void * out = k0 == K0-1 ? (int8_t*)C + (i0*tile_I*DIM*stride_C + j_offset)*sizeof_C : NULL;

        const size_t I = i0 < I0-1 ? tile_I : last_I;
        const size_t J = j0 < J0-1 ? tile_J : last_J;
        const size_t K = k0 < K0-1 ? tile_K : last_K;

        const size_t pad_I = i0 == I0-1 ? padding_I : 0;
        const size_t pad_J = j0 == J0-1 ? padding_J : 0;
        const size_t pad_K = k0 == K0-1 ? padding_K : 0;

        const elem_t * a = a_transpose ? (A + k0*tile_K*DIM*stride_A + i0*tile_I*DIM)
          : (A + i0*tile_I*DIM*stride_A + k0*tile_K*DIM);

        const elem_t * b = b_transpose ? (B + j0*tile_J*DIM*stride_B + k0*tile_K*DIM)
          : (B + k0*tile_K*DIM*stride_B + j0*tile_J*DIM);

        if(a_reuse && j0 >= 1) a = NULL;
        if(b_reuse && i0 >= 1) b = NULL;

        (*inner)(a, b, pre, out,
            A_scale_factor, B_scale_factor, D_scale_factor,
            I, J, K,
            pad_I, pad_J, pad_K,
            stride_A, stride_B, stride_D, stride_C,
            a_transpose, b_transpose,
            full_C, low_D,
            no_bias, repeating_bias,
            act, a_spad_id, b_spad_id, is_mpgemm);
      }

  gemmini_fence();
}
// This function runs a tiled matrix mulctiplication, with hardcoded tiling
// factors
static void tiled_matmul(size_t dim_I, size_t dim_J, size_t dim_K,
        const elem_t* A, const elem_t* B,
        const void * D, void* C,
        size_t stride_A, size_t stride_B, size_t stride_D, size_t stride_C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        int act, acc_scale_t scale, acc_scale_t bert_scale,
        bool repeating_bias,
        size_t tile_I, size_t tile_J, size_t tile_K,
        bool transpose_A, bool transpose_B,
        bool full_C, bool low_D,
        uint8_t weightA,
        enum tiled_matmul_type_t tiled_matmul_type, bool is_mpgemm) {

#ifdef GEMMINI_ASSERTIONS
  // Make sure that the tiling factors make sense
  if (tile_I <= 0) {
    printf("tile_I is non-positive\n");
    exit(1);
  } else if (tile_J <= 0) {
    printf("tile_J is non-positive\n");
    exit(1);
  } else if (tile_K <= 0) {
    printf("tile_K is non-positive\n");
    exit(1);
  }

  const size_t dim_I_padded = (dim_I / DIM + (dim_I % DIM != 0)) * DIM;
  const size_t dim_J_padded = (dim_J / DIM + (dim_J % DIM != 0)) * DIM;
  const size_t dim_K_padded = (dim_K / DIM + (dim_K % DIM != 0)) * DIM;

  if (tile_I * DIM > dim_I_padded) {
    printf("tile_I is too large (tile_I * DIM > dim_I_padded)\n");
    exit(1);
  } else if (tile_J * DIM > dim_J_padded) {
    printf("tile_J is too large (tile_J * DIM > dim_J_padded)\n");
    exit(1);
  } else if (tile_K * DIM > dim_K_padded) {
    printf("tile_K is too large (tile_K * DIM > dim_K_padded)\n");
    exit(1);
  }

  const bool double_buffered = tiled_matmul_type == WS;

  const size_t total_spad_size = double_buffered ? BANK_NUM * BANK_ROWS / 2 :
      BANK_NUM * BANK_ROWS;
  const size_t total_acc_size = double_buffered ? ACC_ROWS / 2 : ACC_ROWS;

  const size_t total_spad_rows =
      (tile_I * tile_K * DIM) +   // Rows to store A
      (tile_K * tile_J * DIM);    // Rows to store B

  if (total_spad_rows > total_spad_size) {
    printf("Not enough space in scratchpad to store A and B matrices\n");
    exit(1);
  }

  const size_t total_acc_rows =
      tile_I * tile_J * DIM;      // Rows to store C

  if (total_acc_rows > total_acc_size) {
    printf("Not enough space in accumulator to store C\n");
    exit(1);
  }

  if (tile_I > 65535 || tile_J > 65535 || tile_K > 65535) {
    printf("I, J, and K tiling factors must be less than 65535, to fit within the bounds of the LOOP_WS function");
    exit(1);
  }

  char matmul_type_str[][4] = {"OS", "WS", "CPU"};

  // Check if transpose options are correct
  if (((tiled_matmul_type == OS) && (transpose_A || transpose_B)) ||
    (tiled_matmul_type == WS && transpose_A && transpose_B)) {
    printf("Not implemented: %s matmul, a_transpose=%d, b_transpose=%d\n", matmul_type_str[tiled_matmul_type], transpose_A, transpose_B);
    exit(1);
  }

  // Check if full_C options are correct
  if ((tiled_matmul_type == CPU && (full_C || low_D)) ||
      (tiled_matmul_type == OS && low_D)) {
    printf("Not implemented: %s matmul, full_C=%d, low_D=%d\n", matmul_type_str[tiled_matmul_type], full_C, low_D);
  }

  if (act == LAYERNORM || act == SOFTMAX) {
    if (tiled_matmul_type == OS) {
      printf("Not implemented: %s matmul, act=%d\n", matmul_type_str[tiled_matmul_type], act);
    }
    if (tile_J * DIM < dim_J) {
      printf("When doing layernorm or softmax, the full J dimension of the matrix must fit in the accumulator\n");
    }
  }
#endif

  // Run a tiled matrix multiplication on either Gemmini or the CPU
  if (tiled_matmul_type == OS || tiled_matmul_type == WS) {
    tiled_matmul_outer(dim_I, dim_J, dim_K,
        A, B, D, C,
        stride_A, stride_B, stride_D, stride_C,
        A_scale_factor, B_scale_factor, D_scale_factor,
        tile_I, tile_J, tile_K,
        act, scale, bert_scale, repeating_bias,
        transpose_A, transpose_B,
        full_C, low_D,
        weightA,
        (int)tiled_matmul_type, is_mpgemm);
  } else /*if (tiled_matmul_type == CPU)*/ {
    matmul_cpu(transpose_A, transpose_B, dim_I, dim_J, dim_K,
            A, B, (const acc_t*) D, (elem_t*)C,
            stride_A, stride_B, stride_D, stride_C,
            A_scale_factor, B_scale_factor, D_scale_factor,
            act, scale, bert_scale, repeating_bias);
  }
}

static size_t tiled_matmul_total_spad_rows(size_t I, size_t J, size_t K) {
  return (I * K + K * J) * DIM;
}


static size_t tiled_matmul_total_acc_rows(size_t I, size_t J) {
  return (I * J) * DIM;
}

//This function is for mpgemm
static inline int tiled_mpgemm_total_spad_rows(size_t tile_I, size_t tile_J_packed, size_t tile_K) {
  // A tile mats: tile_I * tile_K
  const int A_mats = tile_I * tile_K;

  // B tile mats (logical): tile_K * (tile_J_out) = tile_K * (tile_J_packed*4)
  // but stored packed -> 1/4 footprint => effectively tile_K * tile_J_packed
  const int B_mats_effective = tile_K * tile_J_packed;

  // If you also store D/bias in spad, add it here (depends on your kernel)
  const int D_mats = 0;

  // Convert mats -> rows. In Gemmini SW, 1 "mat" corresponds to DIM rows in spad addressing.
  // That's why earlier code uses mats_in_partition = partition_rows / DIM.
  return (A_mats + B_mats_effective + D_mats) * DIM;
}

static void ternary_gemm_auto(size_t dim_I, size_t dim_J_out, size_t dim_K,
        const elem_t* A, const elem_t* B,
        const void * D, void * C,
        size_t stride_A, size_t stride_B, size_t stride_D, size_t stride_C,
        scale_t A_scale_factor, scale_t B_scale_factor, scale_acc_t D_scale_factor,
        int act, acc_scale_t scale, acc_scale_t bert_scale, 
        bool repeating_bias,
        bool B_transpose,
        bool full_C, bool low_D){

  if(dim_J_out % 4 != 0 || stride_B % 4 != 0){
    printf("dim_J, stride_B should be the multiples of 4");
    exit(1);
  }
    // "db_" means "double-buffered"
#define db_partition_rows ((BANK_NUM * BANK_ROWS / 2) / 2)
#define db_mats_in_partition (db_partition_rows / DIM)
#define db_mats_in_acc ((ACC_ROWS / 2) / DIM)
#define db_max_tile_i_j ((size_t)sqrt(db_mats_in_acc))
#define db_max_tile_k (db_mats_in_partition / db_max_tile_i_j)

    const size_t dim_J = dim_J_out / 4;
    const size_t real_stride_B = B_transpose ? stride_B : stride_B / 4;

    const size_t dim_I_padded = (dim_I / DIM + (dim_I % DIM != 0)) * DIM;
    const size_t dim_J_padded = (dim_J / DIM + (dim_J % DIM != 0)) * DIM;
    const size_t dim_K_padded = (dim_K / DIM + (dim_K % DIM != 0)) * DIM;

    const size_t max_spad_rows = BANK_NUM * BANK_ROWS / 2;
    const size_t max_acc_rows =  ACC_ROWS / 2;

    size_t tile_I, tile_J, tile_K;

    if (act == LAYERNORM || act == SOFTMAX) {
       tile_I = 1;
       tile_J = dim_J_padded/DIM;
       tile_K = 1;
    } else  {
       tile_I = dim_I_padded/DIM < 1 ? dim_I_padded/DIM : 1;
       tile_J = dim_J_padded/DIM < 2 ? dim_J_padded/DIM : 2;
       tile_K = dim_K_padded/DIM < db_max_tile_k ? dim_K_padded/DIM : db_max_tile_k;
    }
    // Fill scratchpad as much as possible
    while (true) {
      bool increased = false;

      // Grow J (packed)
      if ((size_t)tiled_mpgemm_total_spad_rows(tile_I, tile_J + 1, tile_K) <= max_spad_rows &&
          (size_t)tiled_matmul_total_acc_rows(tile_I, (tile_J + 1) * 4) <= max_acc_rows &&
          (tile_J + 1) * DIM <= dim_J_padded) {
        tile_J++;
        increased = true;
      }

      // Grow I
      if ((size_t)tiled_mpgemm_total_spad_rows(tile_I + 1, tile_J, tile_K) <= max_spad_rows &&
          (size_t)tiled_matmul_total_acc_rows(tile_I + 1, tile_J * 4) <= max_acc_rows &&
          (tile_I + 1) * DIM <= dim_I_padded) {
        tile_I++;
        increased = true;
      }

      // Grow K
      if ((size_t)tiled_mpgemm_total_spad_rows(tile_I, tile_J, tile_K + 1) <= max_spad_rows &&
          (tile_K + 1) * DIM <= dim_K_padded) {
        tile_K++;
        increased = true;
      }

      if (!increased)
        break;
    }

#ifdef PRINT_TILE
#if PRINT_TILE
    const int spad_rows = tiled_mpgemm_total_spad_rows(tile_I, tile_J, tile_K);
    const int acc_rows = tiled_matmul_total_acc_rows(tile_I, tile_J);

    printf("tile_I: %d\n", tile_I);
    printf("tile_J: %d\n", tile_J);
    printf("tile_K: %d\n\n", tile_K);

    printf("spad_rows: %d\n", spad_rows);
    printf("acc_rows: %d\n\n", acc_rows);

    printf("spad_row utilization: %d%%\n", (spad_rows * 100) / max_spad_rows);
    printf("acc_row utilization: %d%%\n\n", (acc_rows * 100) / max_acc_rows);

    // exit(EXIT_SUCCESS);
#endif
#endif

    tiled_matmul(dim_I, dim_J, dim_K,
        A, B, D, C,
        stride_A, real_stride_B, stride_D, stride_C,
        A_scale_factor, B_scale_factor, D_scale_factor,
        act, scale, bert_scale, repeating_bias,
        tile_I, tile_J, tile_K,
        false, B_transpose,
        full_C, low_D,
        0,
        WS, true);

#undef partition_rows
#undef mats_in_partition
#undef mats_in_acc
#undef max_tile_i_j
#undef max_tile_k

}