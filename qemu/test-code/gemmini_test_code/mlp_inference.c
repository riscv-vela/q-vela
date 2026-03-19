/*
 * Gemmini MLP Inference Demo
 *
 * 2-layer MLP (int8 quantized):
 *
 *   Input  [BATCH × IN_DIM]
 *     -> Layer 1: Linear(IN_DIM → HIDDEN_DIM) + ReLU
 *   Hidden [BATCH × HIDDEN_DIM]
 *     -> Layer 2: Linear(HIDDEN_DIM → OUT_DIM)
 *   Output [BATCH × OUT_DIM]
 *     -> Argmax → predicted class index
 *
 * Weight ranges are kept small so int32 accumulator → int8 conversion
 * does not saturate after scaling:
 *
 *   Layer 1 scale = 0.5
 *     max acc = IN_DIM(128) × input_max(2) × W1_max(1) = 256 → ×0.5 = 128 ✓
 *
 *   Layer 2 scale = 1/64
 *     max acc = HIDDEN_DIM(64) × hidden_max(127) × W2_max(1) = 8128 → /64 = 127 ✓
 *
 * Verifies Gemmini output against CPU reference element-by-element,
 * then prints the argmax prediction for each sample.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "include/gemmini.h"

/* ---- Model dimensions (all multiples of DIM = 16) ---- */
#define BATCH       16   /* inference batch size      */
#define IN_DIM     128   /* input feature dimension   */
#define HIDDEN_DIM  64   /* hidden layer width        */
#define OUT_DIM     16   /* number of output classes  */

#define LAYER1_SCALE  0.5f           /* int32 → int8 scale for layer 1 */
#define LAYER2_SCALE  (1.0f / 64.0f) /* int32 → int8 scale for layer 2 */

/* ---- Weight matrices (K × J layout for A*B GEMM) ---- */
static elem_t W1[IN_DIM][HIDDEN_DIM]     row_align(1);
static elem_t W2[HIDDEN_DIM][OUT_DIM]    row_align(1);

/* ---- Activation buffers (Gemmini output) ---- */
static elem_t gm_hidden[BATCH][HIDDEN_DIM] row_align(1);
static elem_t gm_output[BATCH][OUT_DIM]    row_align(1);

/* ---- CPU reference buffers ---- */
static elem_t ref_hidden[BATCH][HIDDEN_DIM];
static elem_t ref_output[BATCH][OUT_DIM];

/* ---- Input ---- */
static elem_t input[BATCH][IN_DIM] row_align(1);

/* ------------------------------------------------------------------ */

static void init_weights(void)
{
    /* Deterministic weights in {-1, 0, 1} */
    for (int i = 0; i < IN_DIM; i++)
        for (int j = 0; j < HIDDEN_DIM; j++)
            W1[i][j] = (elem_t)(((i * 3 + j * 7 + 1) % 3) - 1);

    for (int i = 0; i < HIDDEN_DIM; i++)
        for (int j = 0; j < OUT_DIM; j++)
            W2[i][j] = (elem_t)(((i * 5 + j * 2 + 3) % 3) - 1);
}

static void init_input(void)
{
    /* Values in {-2, -1, 0, 1, 2}: each sample has a unique offset */
    for (int b = 0; b < BATCH; b++)
        for (int i = 0; i < IN_DIM; i++)
            input[b][i] = (elem_t)(((b * IN_DIM + i + 1) % 5) - 2);
}

/*
 * CPU reference scale: clamp(roundf(acc * scale), -128, 127)
 * Matches Gemmini's ACC_SCALE macro behavior (round-half-to-even
 * is approximated by roundf; for exact-half cases the results may
 * differ by ±1, but our scale factors avoid half-integer outputs).
 */
static elem_t cpu_scale_clamp(int32_t acc, float scale)
{
    float y = (float)acc * scale;
    /* Round-half-to-even (banker's rounding) to match Gemmini's ROUND_NEAR_EVEN */
    float fl = floorf(y);
    float diff = y - fl;
    int32_t r;
    if (diff < 0.5f)
        r = (int32_t)fl;
    else if (diff > 0.5f)
        r = (int32_t)fl + 1;
    else  /* exactly 0.5: round to nearest even */
        r = ((int32_t)fl % 2 == 0) ? (int32_t)fl : (int32_t)fl + 1;
    if (r >  127) r =  127;
    if (r < -128) r = -128;
    return (elem_t)r;
}

static void cpu_forward(void)
{
    /* Layer 1: ref_hidden = ReLU( input × W1 × LAYER1_SCALE ) */
    for (int b = 0; b < BATCH; b++) {
        for (int j = 0; j < HIDDEN_DIM; j++) {
            int32_t acc = 0;
            for (int k = 0; k < IN_DIM; k++)
                acc += (int32_t)input[b][k] * (int32_t)W1[k][j];
            elem_t v = cpu_scale_clamp(acc, LAYER1_SCALE);
            ref_hidden[b][j] = (v < 0) ? 0 : v;  /* ReLU */
        }
    }

    /* Layer 2: ref_output = ref_hidden × W2 × LAYER2_SCALE  (no activation) */
    for (int b = 0; b < BATCH; b++) {
        for (int j = 0; j < OUT_DIM; j++) {
            int32_t acc = 0;
            for (int k = 0; k < HIDDEN_DIM; k++)
                acc += (int32_t)ref_hidden[b][k] * (int32_t)W2[k][j];
            ref_output[b][j] = cpu_scale_clamp(acc, LAYER2_SCALE);
        }
    }
}

static int argmax(const elem_t *logits, int n)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (logits[i] > logits[best]) best = i;
    return best;
}

/* Returns 0 on pass, number of mismatches on failure */
static int verify_layer(const char *name,
                         const elem_t *hw, const elem_t *ref,
                         int rows, int cols)
{
    int mismatches = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            elem_t hw_v  = hw [i * cols + j];
            elem_t ref_v = ref[i * cols + j];
            if (hw_v != ref_v) {
                if (mismatches < 5)  /* print first 5 only */
                    printf("[FAIL] %s [%d][%d]: gemmini=%d  ref=%d\n",
                           name, i, j, (int)hw_v, (int)ref_v);
                mismatches++;
            }
        }
    }
    if (mismatches == 0)
        printf("[PASS] %s — all %d values match\n", name, rows * cols);
    else
        printf("[FAIL] %s — %d / %d mismatches\n", name, mismatches, rows * cols);
    return mismatches;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== Gemmini MLP Inference Demo ===\n");
    printf("    Arch : %d-layer MLP  (%d → %d → %d)\n",
           2, IN_DIM, HIDDEN_DIM, OUT_DIM);
    printf("    Batch: %d   DIM: %d\n\n", BATCH, DIM);

    init_weights();
    init_input();

    /* ---- CPU reference forward pass ---- */
    cpu_forward();

    /* ---- Gemmini forward pass ---- */

    gemmini_fence();

    /* --- Layer 1: gm_hidden = ReLU( input × W1, scale=0.5 ) --- */
    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);
    gemmini_extended_config_st(HIDDEN_DIM * sizeof(elem_t), RELU, LAYER1_SCALE);
    gemmini_extended3_config_ld(IN_DIM     * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(HIDDEN_DIM * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(HIDDEN_DIM * sizeof(acc_t),  MVIN_SCALE_IDENTITY, false, 2);

    tiled_matmul_auto(
        BATCH, HIDDEN_DIM, IN_DIM,
        (const elem_t *)input,      /* A: BATCH × IN_DIM     */
        (const elem_t *)W1,         /* B: IN_DIM × HIDDEN_DIM */
        NULL,                        /* D: no bias             */
        (elem_t *)gm_hidden,         /* C: BATCH × HIDDEN_DIM  */
        IN_DIM,                      /* stride_A */
        HIDDEN_DIM,                  /* stride_B */
        HIDDEN_DIM,                  /* stride_D */
        HIDDEN_DIM,                  /* stride_C */
        MVIN_SCALE_IDENTITY,         /* A_scale  */
        MVIN_SCALE_IDENTITY,         /* B_scale  */
        ACC_SCALE_IDENTITY,          /* D_scale  */
        RELU,                        /* activation */
        LAYER1_SCALE,                /* accumulator → int8 scale */
        ACC_SCALE_IDENTITY,          /* bert_scale (unused)  */
        false,                       /* repeating_bias */
        false, false,                /* transpose A, B */
        false,                       /* full_C (output as int8) */
        false,                       /* low_D  */
        0,                           /* weightA */
        WS
    );

    gemmini_fence();

    /* --- Layer 2: gm_output = gm_hidden × W2, scale=1/64 --- */
    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);
    gemmini_extended_config_st(OUT_DIM * sizeof(elem_t), NO_ACTIVATION, LAYER2_SCALE);
    gemmini_extended3_config_ld(HIDDEN_DIM * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 0);
    gemmini_extended3_config_ld(OUT_DIM    * sizeof(elem_t), MVIN_SCALE_IDENTITY, false, 1);
    gemmini_extended3_config_ld(OUT_DIM    * sizeof(acc_t),  MVIN_SCALE_IDENTITY, false, 2);

    tiled_matmul_auto(
        BATCH, OUT_DIM, HIDDEN_DIM,
        (const elem_t *)gm_hidden,   /* A: BATCH × HIDDEN_DIM  */
        (const elem_t *)W2,          /* B: HIDDEN_DIM × OUT_DIM */
        NULL,                         /* D: no bias              */
        (elem_t *)gm_output,          /* C: BATCH × OUT_DIM      */
        HIDDEN_DIM,                   /* stride_A */
        OUT_DIM,                      /* stride_B */
        OUT_DIM,                      /* stride_D */
        OUT_DIM,                      /* stride_C */
        MVIN_SCALE_IDENTITY,
        MVIN_SCALE_IDENTITY,
        ACC_SCALE_IDENTITY,
        NO_ACTIVATION,
        LAYER2_SCALE,                 /* accumulator → int8 scale */
        ACC_SCALE_IDENTITY,
        false,
        false, false,
        false, false, 0,
        WS
    );

    gemmini_fence();

    /* ---- Verification ---- */
    printf("--- Verification ---\n");
    int fail1 = verify_layer("Layer1 (hidden)",
                              (elem_t *)gm_hidden, (elem_t *)ref_hidden,
                              BATCH, HIDDEN_DIM);
    int fail2 = verify_layer("Layer2 (output)",
                              (elem_t *)gm_output, (elem_t *)ref_output,
                              BATCH, OUT_DIM);

    /* ---- Inference results ---- */
    printf("\n--- Inference Results ---\n");
    printf("  Sample  | Gemmini pred | CPU pred | Match?\n");
    printf("  --------|--------------|----------|---------\n");
    int pred_mismatches = 0;
    for (int b = 0; b < BATCH; b++) {
        int gm_cls  = argmax(gm_output[b],  OUT_DIM);
        int ref_cls = argmax(ref_output[b], OUT_DIM);
        int match   = (gm_cls == ref_cls);
        if (!match) pred_mismatches++;
        printf("  Sample%2d |     %2d       |    %2d    |  %s\n",
               b, gm_cls, ref_cls, match ? "OK" : "MISMATCH");
    }

    /* ---- Summary ---- */
    printf("\n=== Summary ===\n");
    if (fail1 == 0 && fail2 == 0)
        printf("All element-wise values match. Inference PASSED.\n");
    else
        printf("Element mismatch detected. Inference FAILED.\n");

    return (fail1 == 0 && fail2 == 0) ? 0 : 1;
}
