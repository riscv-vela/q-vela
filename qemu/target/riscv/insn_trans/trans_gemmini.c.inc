#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
//gRocc instruction test
static void gen_loop_ws_config_bounds(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws_config_bounds(tcg_env, src1, src2);
}

static bool trans_loop_ws_config_bounds(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws_config_bounds);
}

// ------------------------------------------------------------

static void gen_loop_ws_addrs_ab(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws_addrs_ab(tcg_env, src1, src2);
}

static bool trans_loop_ws_addrs_ab(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws_addrs_ab);
}
// ------------------------------------------------------------

static void gen_loop_ws_addrs_dc(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws_addrs_dc(tcg_env, src1, src2);
}

static bool trans_loop_ws_addrs_dc(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws_addrs_dc);
}

// ------------------------------------------------------------

static void gen_loop_ws_strides_ab(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws_strides_ab(tcg_env, src1, src2);
}

static bool trans_loop_ws_strides_ab(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws_strides_ab);
}

// ------------------------------------------------------------

static void gen_loop_ws_strides_dc(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws_strides_dc(tcg_env, src1, src2);
}

static bool trans_loop_ws_strides_dc(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws_strides_dc);
}

// ------------------------------------------------------------

static void gen_loop_ws(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_loop_ws(tcg_env, src1, src2);
}

static bool trans_loop_ws(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_loop_ws);
}

//gemv_Rocc instruction test
static void gen_k_gemv_loop_ws_config_bounds(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_config_bounds(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_config_bounds(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_config_bounds);
}

// ------------------------------------------------------------

static void gen_k_gemv_loop_ws_addrs_ab(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_addrs_ab(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_addrs_ab(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_addrs_ab);
}
// ------------------------------------------------------------

static void gen_k_gemv_loop_ws_addrs_dc(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_addrs_dc(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_addrs_dc(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_addrs_dc);
}

// ------------------------------------------------------------

static void gen_k_gemv_loop_ws_strides_ab(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_strides_ab(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_strides_ab(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_strides_ab);
}

// ------------------------------------------------------------

static void gen_k_gemv_loop_ws_strides_dc(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_strides_dc(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_strides_dc(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_strides_dc);
}

// ------------------------------------------------------------

static void gen_k_gemv_loop_ws_flags(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_k_gemv_loop_ws_flags(tcg_env, src1, src2);
}

static bool trans_k_gemv_loop_ws_flags(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_k_gemv_loop_ws_flags);
}

//rocc 명령어
//gemmini_config
static void gen_gemmini_config(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_gemmini_config(tcg_env, src1, src2);
}

static bool trans_gemmini_config(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_config);
}
// -----------------------------------------------------------
//mvin instruction
static void gen_gemmini_mvin2(TCGv dest, TCGv src1, TCGv src2) {
    TCGv_i32 id = tcg_temp_new_i32();
    tcg_gen_movi_i32(id, 1);

    gen_helper_mvin(tcg_env, src1, src2, id);
}

static bool trans_gemmini_mvin2(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_mvin2);
}

static void gen_gemmini_mvin(TCGv dest, TCGv src1, TCGv src2) {
    TCGv_i32 id = tcg_temp_new_i32();
    tcg_gen_movi_i32(id, 0);

    gen_helper_mvin(tcg_env, src1, src2, id);
}

static bool trans_gemmini_mvin(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_mvin);
}

static void gen_gemmini_mvin3(TCGv dest, TCGv src1, TCGv src2) {
    TCGv_i32 id = tcg_temp_new_i32();
    tcg_gen_movi_i32(id, 2);

    gen_helper_mvin(tcg_env, src1, src2, id);
}

static bool trans_gemmini_mvin3(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_mvin3);
}
//============================================================
//mvout
static void gen_gemmini_mvout(TCGv dest, TCGv src1, TCGv src2) {
    gen_helper_mvout(tcg_env, src1, src2);
}

static bool trans_gemmini_mvout(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_mvout);
}
//===========================================================
//preload
static void gen_gemmini_preload(TCGv dest, TCGv src1, TCGv src2) {
        gen_helper_preload(src1, src2);
}

static bool trans_gemmini_preload(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_preload);
}

//===========================================================
//compute
static void gen_gemmini_compute_preloaded(TCGv dest, TCGv src1, TCGv src2) {
        TCGv_i32 state = tcg_temp_new_i32();
    	tcg_gen_movi_i32(state, 1);

	gen_helper_compute(src1, src2,state);
}

static bool trans_gemmini_compute_preloaded(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_compute_preloaded);
}

static void gen_gemmini_compute_accumulate(TCGv dest, TCGv src1, TCGv src2) {  
       	TCGv_i32 state = tcg_temp_new_i32();
        tcg_gen_movi_i32(state, 0);

	gen_helper_compute(src1, src2,state);
}

static bool trans_gemmini_compute_accumulate(DisasContext *ctx, arg_r *a) {
    return gen_logic(ctx, a, gen_gemmini_compute_accumulate);
}
