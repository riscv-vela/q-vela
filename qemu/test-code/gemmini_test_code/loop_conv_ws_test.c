#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "include/gemmini.h"

#define MVIN_SCALE_IDENTITY 1.0f
#define ACC_SCALE_IDENTITY  1.0f
#define NO_ACTIVATION 0

/* 5x5 input, NHWC layout, values 1..25 */
static int8_t g_input[1*5*5*1] = {
     1, 2, 3, 4, 5,
     6, 7, 8, 9,10,
    11,12,13,14,15,
    16,17,18,19,20,
    21,22,23,24,25
};

/* 3x3 kernel, HWCF layout, all ones */
static int8_t g_kernel[3*3*1*1] = {
    1,1,1,
    1,1,1,
    1,1,1
};

/* Expected output: sum of 3x3 window, clamped to int8 range [-128, 127] */
static int8_t g_expected[1*3*3*1] = {
    63, 72, 81,
   108,117,126,
   127, 127, 127  /* 153, 162, 171 clamped by lcw_apply_activation */
};

static int8_t  g_output[1*3*3*1];
static int32_t g_bias[1] = {0};

int main(void) {
    printf("[loop_conv_ws_test] start\n");

    /* Config matching what the binary does */
    gemmini_extended_config_st(sizeof(int8_t), NO_ACTIVATION, ACC_SCALE_IDENTITY);
    gemmini_extended_config_ex(WS, NO_ACTIVATION, 0, 1, false, false);

    printf("[loop_conv_ws_test] config done, calling gemmini_loop_conv_ws\n");

    /* 5x5 input, 3x3 kernel, 3x3 output, stride=1, no padding */
    gemmini_loop_conv_ws(
        /* batch_size */      1,
        /* in_row_dim */      5,
        /* in_col_dim */      5,
        /* in_channels */     1,
        /* out_channels */    1,
        /* out_row_dim */     3,
        /* out_col_dim */     3,
        /* pool_out_row_dim */3,
        /* pool_out_col_dim */3,
        /* stride */          1,
        /* padding */         0,
        /* kernel_dim */      3,
        /* kernel_dilation */ 1,
        /* pool_size */       0,
        /* pool_stride */     0,
        /* pool_padding */    0,
        /* batches */         1,
        /* porows */          3,
        /* pocols */          3,
        /* pochs */           1,
        /* krows */           3,
        /* kcols */           3,
        /* kchs */            1,
        /* lpad */            0,
        /* rpad */            0,
        /* upad */            0,
        /* dpad */            0,
        /* plpad */           0,
        /* prpad */           0,
        /* pupad */           0,
        /* pdpad */           0,
        /* orows */           3,
        /* ocols */           3,
        /* weights */         g_kernel,
        /* output */          g_output,
        /* bias */            g_bias,
        /* input */           g_input,
        /* no_bias */         0,
        /* no_pool */         1,
        /* downsample */      0,
        /* wrot180 */         0,
        /* input_dilated */   0,
        /* activation */      NO_ACTIVATION,
        /* trans_output_1203 */0,
        /* trans_weight_1203 */0,
        /* trans_weight_0132 */0,
        /* trans_input_3120 */ 0,
        /* max_pixels_per_row */0,
        /* in_stride */       1,
        /* weight_stride */   1,
        /* out_stride */      1,
        /* dw */              0,
        /* a_spad_id */       0,
        /* b_spad_id */       0
    );

    printf("[loop_conv_ws_test] gemmini_loop_conv_ws issued, waiting fence\n");
    gemmini_fence();
    printf("[loop_conv_ws_test] fence done\n");

    /* Print results */
    int pass = 1;
    printf("Output (row-major):\n");
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int8_t v = g_output[i*3 + j];
            int8_t e = g_expected[i*3 + j];
            printf("  [%d][%d] = %d  (expected %d)%s\n",
                   i, j, v, e, v == e ? "" : " MISMATCH");
            if (v != e) pass = 0;
        }
    }
    printf("[loop_conv_ws_test] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
