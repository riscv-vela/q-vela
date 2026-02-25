/*
 * Header file for Gemmini accelerator internals for RISC-V target in QEMU
 * 
 * Includes Data types, Gemmini State Structure(Memory(Spad, Accumulator), Internal Configurations), Utility Functions, and etc.)
 * Relies Heavily on gemmmini.h from libgemmini repo, and xcustom.h of rocc-software.
 */
#ifndef _GEMMINI_H
#define _GEMMINI_H

#include "hw/qdev-core.h"
#include "gemmini_params.h"
#include <math.h>

#define TYPE_GEMMINI_DEVICE "gemmini"

OBJECT_DECLARE_SIMPLE_TYPE(GemminiState, GEMMINI_DEVICE)

/* Prototype of Universal RoCC command execution function */
void gemmini_exec_cmd(GemminiState *s, uint32_t funct7, uint32_t funct3, uint64_t rs1, u_int64_t rs2, CPUState *env);

/* Callback Functions for Gemmini memory read and write */
typedef uint64_t (*gemmini_mem_read_fn)(
    void *opaque,
    uint64_t addr,
    uint32_t size
);

typedef void (*gemmini_mem_write_fn)(
    void *opaque,
    uint64_t addr,
    uint64_t data,
    uint32_t size
);

/* Gemmini Function Prototypes */
scale_t scale_t_bits_to_scale_t(scale_t_bits bits);
acc_scale_t acc_scale_t_bits_to_acc_scale_t(acc_scale_t_bits bits); 
elem_t mvin_scale(elem_t value, scale_t scale);
elem_t MVIN_SCALE(elem_t x, scale_t scale);
elem_t sys_shift(acc_t value, unsigned int shift);
elem_t apply_activation(elem_t value, Activation act);
elem_t apply_activation_sys(GemminiState *gm, elem_t value);
NormCmd non_terminating_norm_cmd(NormCmd cmd);
bool apply_norm(GemminiState *gm, const acc_t * x, size_t len, NormCmd cmd);
acc_t apply_iexp(acc_t q, acc_t qb, acc_t qc, acc_t qln2, acc_t qln2_inv);
acc_t apply_pre_activation_acc(GemminiState *gm, acc_t value);
elem_t apply_activation_acc(GemminiState *gm, elem_t value);
elem_t acc_scale(acc_t value, acc_scale_t scale);
acc_t apply_igelu(acc_t q, acc_t qb, acc_t qc);

void gemmini_config(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void loop_ws_config_bounds(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void loop_ws_addrs_ab(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void loop_ws_addrs_dc(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void loop_ws_strides_ab(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void loop_ws_strides_dc(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void k_gemv_loop_ws_config_bounds(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void k_gemv_loop_ws_addrs_ab(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void k_gemv_loop_ws_addrs_dc(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void k_gemv_loop_ws_strides_ab(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);
void k_gemv_loop_ws_strides_dc(u_int64_t rs1, u_int64_t rs2, GemminiState *gm);

void preload(uint64_t bd_addr, uint64_t c_addr, GemminiState *gm);
void mvin(uint64_t dram_addr, uint64_t sp_addr, int state_id, GemminiState *gm);
void mvout(uint64_t dram_addr, uint64_t sp_addr, GemminiState *gm);

void compute(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm);
void loop_ws(GemminiState *gm, uint64_t rs1, uint64_t rs2);
void k_gemv_loop_ws(uint64_t rs1, uint64_t rs2, GemminiState *gm);
void compute_gemv(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm);

void ternary_loop_ws(GemminiState *gm, uint64_t rs1, uint64_t rs2);
void compute_ternary(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm);
void switch_ternary(GemminiState *gm, uint64_t rs1, uint64_t rs2);

void init_trace(GemminiState *gm);
void dump_trace(GemminiState *gm);

typedef struct GemminiState {
    DeviceState parent;

    /* Features for Manager */
    uint32_t id;
    QemuMutex gemmini_lock;
    bool busy;
    uint32_t locked_by_cpu;

    /* Features for Gemmini itself */
    uint32_t output_sp_addr;
    uint32_t preload_sp_addr;
    uint16_t preload_cols, preload_rows;
    uint16_t output_cols, output_rows;

    acc_t igelu_qb, igelu_qc;
    acc_t qln2, qln2_inv;
    Dataflow mode;
    Activation sys_act;
    Activation acc_act;
    reg_t sys_shift;
    bool a_transpose;
    bool b_transpose;
    acc_scale_t acc_shift;
    acc_scale_t sys_acc_shift;

    uint16_t c_stride;
    uint16_t a_stride;
    uint8_t pool_stride;
    uint8_t pool_size;
    uint8_t pool_out_dim;
    uint8_t pool_porows;
    uint8_t pool_pocols;
    uint8_t pool_orows;
    uint8_t pool_ocols;
    uint8_t pool_lpad;
    uint8_t pool_upad;

    reg_t load_strides[LOAD_STATES];
    reg_t store_stride;
    uint16_t load_block_strides[LOAD_STATES];
    bool load_shrunks[LOAD_STATES];
    #if defined(HAS_MVIN_SCALE) || defined(HAS_MVIN_ACC_SCALE)
    scale_t load_scales[LOAD_STATES];
    #endif
    uint8_t pixels_per_rows[LOAD_STATES];
    #ifdef ELEM_T_IS_FLOAT
    float spad[BANK_NUM * BANK_ROWS][DIM];
    float accumulator[BANK_NUM * BANK_ROWS][DIM];
    float pe_state[DIM][DIM];
    float bias_buffer[DIM][DIM];
    #else
    elem_t spad[BANK_NUM * BANK_ROWS][DIM]; 
    acc_t accumulator[BANK_NUM * BANK_ROWS][DIM * 4];
    acc_t pe_state[DIM][DIM];
    elem_t ternary_pe_state[DIM][DIM * 3];

    acc_t bias_buffer[DIM];
    acc_t ternary_bias_buffer[DIM * 3];
    #endif

    // Normalization statistics
    uint8_t norm_stat_id;
    acc_t norm_sum[NORM_STAT_IDS];
    acc_t norm_running_max[NORM_STAT_IDS];
    acc_t norm_max[NORM_STAT_IDS];
    acc_t norm_count[NORM_STAT_IDS];
    acc_t norm_mean[NORM_STAT_IDS];
    acc_scale_t norm_inv_stddev[NORM_STAT_IDS];
    acc_scale_t norm_inv_sum_exp[NORM_STAT_IDS];
    bool norm_reset[NORM_STAT_IDS];

    uint16_t loop_ws_I, loop_ws_J, loop_ws_K;
    uint16_t loop_ws_pad_I, loop_ws_pad_J, loop_ws_pad_K;
    uint64_t loop_ws_A, loop_ws_B, loop_ws_D, loop_ws_C;
    uint64_t loop_ws_A_stride, loop_ws_B_stride, loop_ws_D_stride, loop_ws_C_stride;

    uint16_t gemv_loop_ws_I, gemv_loop_ws_J, gemv_loop_ws_K;
    uint16_t gemv_loop_ws_pad_I, gemv_loop_ws_pad_J, gemv_loop_ws_pad_K;
    uint64_t gemv_loop_ws_A, gemv_loop_ws_B, gemv_loop_ws_D, gemv_loop_ws_C;
    uint64_t gemv_loop_ws_A_stride, gemv_loop_ws_B_stride, gemv_loop_ws_D_stride, gemv_loop_ws_C_stride;

    /* Memory Callbacks */
    void *mem_opaque;
    gemmini_mem_read_fn  mem_read;
    gemmini_mem_write_fn mem_write;

    /* Trace */
    uint64_t total_writes_in_bytes;
    uint64_t total_reads_in_bytes;

} GemminiState;

/* Not Needed */
extern GemminiState *global_gemmini_state;

#endif // _GEMMINI_H