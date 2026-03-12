#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

static bool trans_rocc(DisasContext *ctx, arg_r *a, uint32_t funct7, uint32_t funct3) {
    TCGv_i32 funct7_v = tcg_temp_new_i32();
    tcg_gen_movi_i32(funct7_v, funct7);
    TCGv_i32 funct3_v = tcg_temp_new_i32();
    tcg_gen_movi_i32(funct3_v, funct3);
    gen_helper_rocc_exec(
        tcg_env,
        funct7_v,
        funct3_v,
        cpu_gpr[a->rs1],
        cpu_gpr[a->rs2]
    );
    return true;
}

static bool trans_loop_ws_config_bounds(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001001, 0b011);
}

static bool trans_loop_ws_addrs_ab(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001010, 0b011);
}

static bool trans_loop_ws_addrs_dc(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001011, 0b011);
}

static bool trans_loop_ws_strides_ab(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001100, 0b011);
}

static bool trans_loop_ws_strides_dc(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001101, 0b011);
}

static bool trans_loop_ws(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0001000, 0b011);
}

static bool trans_k_gemv_loop_ws_config_bounds(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011000, 0b011);
}

static bool trans_k_gemv_loop_ws_addrs_ab(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011001, 0b011);
}

static bool trans_k_gemv_loop_ws_addrs_dc(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011010, 0b011);
}

static bool trans_k_gemv_loop_ws_strides_ab(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011011, 0b011);
}

static bool trans_k_gemv_loop_ws_strides_dc(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011100, 0b011);
}

static bool trans_k_gemv_loop_ws_flags(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0010111, 0b011);
}

static bool trans_gemmini_config(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000000, 0b011);
}

static bool trans_gemmini_mvin2(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000001, 0b011);
}

static bool trans_gemmini_mvin(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000010, 0b011);
}

static bool trans_gemmini_mvin3(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000010, 0b011);
}

static bool trans_gemmini_mvout(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000011, 0b011);
}

static bool trans_gemmini_preload(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000100, 0b011);
}

static bool trans_gemmini_compute_preloaded(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000101, 0b011);
}

static bool trans_gemmini_compute_accumulate(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000110, 0b011);
}

static bool trans_ternary_loop_ws(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0011110, 0b011);
}

static bool trans_gemmini_dump_trace(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b1111111, 0b011);
}

static bool trans_gemmini_init_trace(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b1111110, 0b011);
}

static bool trans_gemmini_flush(DisasContext *ctx, arg_r *a) {
    return trans_rocc(ctx, a, 0b0000111, 0b011);
}
