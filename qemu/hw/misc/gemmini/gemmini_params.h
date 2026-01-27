#ifndef GEMMINI_PARAMS_H
#define GEMMINI_PARAMS_H

#include <stdint.h>
#include <limits.h>
#include <stdbool.h>

#define DIM 16
#define ADDR_LEN 32
#define BANK_NUM 4
#define BANK_ROWS 4096
#define ACC_ROWS 1024
#define MAX_BYTES 64
#define MAX_BLOCK_LEN (MAX_BYTES/(DIM*1))
#define MAX_BLOCK_LEN_ACC (MAX_BYTES/(DIM*4))

#define LOAD_STATES 3
#define NORM_STAT_IDS 4

static const uint64_t addr_len = ADDR_LEN;

typedef int8_t elem_t;
static const elem_t elem_t_max = 127;
static const elem_t elem_t_min = -128;
typedef int32_t acc_t;
typedef int64_t full_t;

#define HAS_MVIN_SCALE
typedef float scale_t;
typedef uint32_t scale_t_bits;

typedef int32_t scale_acc_t;
typedef uint32_t scale_acc_t_bits;

typedef float acc_scale_t;
typedef uint32_t acc_scale_t_bits;

typedef uint32_t acc_t_bits;
typedef uint8_t elem_t_bits;

typedef uint64_t reg_t;
typedef acc_t output_t;

/* Output Stationary, Weight Stationary */
typedef enum { OS, WS } Dataflow;
/* Choose Activation Function */
typedef enum { NONE, RELU, LAYERNORM, IGELU, SOFTMAX } Activation;
/* Control Signals Used for Normalization */
typedef enum { RESET, SUM, MEAN, VARIANCE, INV_STDDEV, MAX, SUM_EXP, INV_SUM_EXP } NormCmd;

#endif // GEMMINI_PARAMS_H