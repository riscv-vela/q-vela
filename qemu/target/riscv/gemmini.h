#ifndef _GEMMINI_H
#define _GEMMINI_H
#include <limits.h>
#include <math.h>
#define DIM 16
#define ADDR_LEN 32
#define BANK_NUM 4
#define BANK_ROWS 4096
#define ACC_ROWS 1024
#define MAX_BYTES 64
#define LOAD_STATES 3
#define NORM_STAT_IDS 4

#ifndef NORM_STAT_IDS
#define NORM_STAT_IDS 4
#endif

static const uint64_t addr_len = ADDR_LEN;
typedef int32_t acc_t;
typedef int8_t elem_t;
static const elem_t elem_t_max = 127;
static const elem_t elem_t_min = -128;

typedef uint64_t reg_t;
typedef acc_t output_t;

#define HAS_MVIN_SCALE
//#define ELEM_T_IS_FLOAT
typedef float scale_t;
typedef uint32_t scale_t_bits;

typedef int32_t scale_acc_t;
typedef uint32_t scale_acc_t_bits;

typedef float acc_scale_t;
typedef uint32_t acc_scale_t_bits;

typedef uint32_t acc_t_bits;
typedef uint8_t elem_t_bits;

typedef enum {
    OS, // Output Stationary
    WS  // Weight Stationary
} Dataflow;

typedef enum {
    NONE,
    RELU,
    LAYERNORM,
    IGELU,
    SOFTMAX
} Activation;

typedef enum {
    RESET, 
    SUM, 
    MEAN, 
    VARIANCE, 
    INV_STDDEV, 
    MAX, 
    SUM_EXP, 
    INV_SUM_EXP
} NormCmd;

scale_t scale_t_bits_to_scale_t(scale_t_bits bits);
acc_scale_t acc_scale_t_bits_to_acc_scale_t(acc_scale_t_bits bits); 
elem_t mvin_scale(elem_t value, scale_t scale);
elem_t MVIN_SCALE(elem_t x, scale_t scale);
elem_t sys_shift(acc_t value, unsigned int shift);
elem_t apply_activation(elem_t value, Activation act);
elem_t apply_activation_sys(elem_t value);
NormCmd non_terminating_norm_cmd(NormCmd cmd);
bool apply_norm(const acc_t * x, size_t len, NormCmd cmd);
acc_t apply_iexp(acc_t q, acc_t qb, acc_t qc, acc_t qln2, acc_t qln2_inv);
acc_t apply_pre_activation_acc(acc_t value);
elem_t apply_activation_acc(elem_t value);
elem_t acc_scale(acc_t value, acc_scale_t scale);
acc_t apply_igelu(acc_t q, acc_t qb, acc_t qc);

#ifdef ELEM_T_IS_FLOAT
  elem_t elem_t_bits_to_elem_t(elem_t_bits x);
  elem_t_bits elem_t_to_elem_t_bits(elem_t x);
  acc_t acc_t_bits_to_acc_t(acc_t_bits x);
  acc_t_bits acc_t_to_acc_t_bits(acc_t x);
#endif

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

typedef struct {
  uint32_t output_sp_addr;
  uint32_t preload_sp_addr;
  uint16_t preload_cols, preload_rows;
  uint16_t output_cols, output_rows;

  acc_t igelu_qb, igelu_qc;
  acc_t qln2, qln2_inv;
  bool mode;
  uint16_t sys_act;
  uint16_t acc_act;
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
  acc_t accumulator[BANK_NUM * BANK_ROWS][DIM];
  acc_t pe_state[DIM][DIM];
  elem_t bias_buffer[DIM][DIM];
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

  //loop_ws
  uint16_t loop_ws_I, loop_ws_J, loop_ws_K;
  uint16_t loop_ws_pad_I, loop_ws_pad_J, loop_ws_pad_K;
  uint64_t loop_ws_A, loop_ws_B, loop_ws_D, loop_ws_C;
  uint64_t loop_ws_A_stride, loop_ws_B_stride, loop_ws_D_stride, loop_ws_C_stride;

  //gemv
  uint16_t gemv_loop_ws_I, gemv_loop_ws_J, gemv_loop_ws_K;
  uint16_t gemv_loop_ws_pad_I, gemv_loop_ws_pad_J, gemv_loop_ws_pad_K;
  uint64_t gemv_loop_ws_A, gemv_loop_ws_B, gemv_loop_ws_D, gemv_loop_ws_C;
  uint64_t gemv_loop_ws_A_stride, gemv_loop_ws_B_stride, gemv_loop_ws_D_stride, gemv_loop_ws_C_stride;

} gemmini_state_t;

gemmini_state_t gemmini_state;

acc_scale_t acc_scale_t_bits_to_acc_scale_t(acc_scale_t_bits bits){
   union {
	acc_scale_t scale;
        acc_scale_t_bits bits;
    } un;

    un.bits = bits;
    return un.scale;
}
scale_t scale_t_bits_to_scale_t(scale_t_bits bits) {
    union {
        scale_t scale;
        scale_t_bits bits;
    } un;

    un.bits = bits;
    return un.scale;
}

#ifdef ELEM_T_IS_FLOAT
elem_t elem_t_bits_to_elem_t(elem_t_bits x) {
  union {
    elem_t_bits b;
    elem_t f;
  } un;

  un.b = x;
  return un.f;
}

elem_t_bits elem_t_to_elem_t_bits(elem_t x) {
  union {
    elem_t_bits b;
    elem_t f;
  } un;

  un.f = x;
  return un.b;
}

acc_t acc_t_bits_to_acc_t(acc_t_bits x) {
  union {
    acc_t_bits b;
    acc_t f;
  } un;

  un.b = x;
  return un.f;
}

acc_t_bits acc_t_to_acc_t_bits(acc_t x) {
  union {
    acc_t_bits b;
    acc_t f;
  } un;

  un.f = x;
  return un.b;
}
#endif


elem_t MVIN_SCALE(elem_t x, scale_t scale) {
    float y = rintf(x * scale);
    if (y > INT8_MAX) return INT8_MAX;
    else if (y < INT8_MIN) return INT8_MIN;
    else return (elem_t)y;
}

elem_t acc_scale(acc_t value, acc_scale_t scale) {
  acc_t scaled = ACC_SCALE(value, scale);
#ifndef ELEM_T_IS_FLOAT
  // Saturate and cast element
  const elem_t elem_t_max = INT8_MAX;
  const elem_t elem_t_min = INT8_MIN;
  scaled = scaled > elem_t_max ? elem_t_max : (scaled < elem_t_min ? elem_t_min : scaled);
#endif

  return scaled;
}

#ifdef HAS_MVIN_SCALE
elem_t mvin_scale(elem_t value, scale_t scale) {
  acc_t scaled = MVIN_SCALE(value, scale);
#ifndef ELEM_T_IS_FLOAT
  // Saturate and cast element
  const elem_t elem_t_max = INT8_MAX;
  const elem_t elem_t_min = INT8_MIN;
  scaled = scaled > elem_t_max ? elem_t_max : (scaled < elem_t_min ? elem_t_min : scaled);
#endif

  return scaled;
}
#endif

elem_t sys_shift(acc_t value, unsigned int shift) {
  acc_t shifted = ROUNDING_RIGHT_SHIFT(value, shift);
#ifndef ELEM_T_IS_FLOAT
  // Saturate and cast element
  const elem_t elem_t_max = INT8_MAX;
  const elem_t elem_t_min = INT8_MIN;
  shifted = shifted > elem_t_max ? elem_t_max : (shifted < elem_t_min ? elem_t_min : shifted);
#endif

  return shifted;
}

elem_t apply_activation(elem_t value, Activation act) {
  if (act == RELU) {
    return value > 0 ? (elem_t)value : (elem_t)0;
  } else {
    return (elem_t)value;
  }
}
elem_t apply_activation_sys(elem_t value) {
  return apply_activation(value, gemmini_state.sys_act);
}
elem_t apply_activation_acc(elem_t value) {
  return apply_activation(value, gemmini_state.acc_act);
}

acc_t apply_iexp(acc_t q, acc_t qb, acc_t qc, acc_t qln2, acc_t qln2_inv) {
#ifdef ELEM_T_IS_FLOAT
  const acc_t z = (acc_t) (-q * qln2_inv) / (1 << 16);
#else
  const acc_t z = (acc_t) (-q * qln2_inv) >> 16;
#endif
  const acc_t qp = q + z * qln2;
  const acc_t q_exp = (qp + qb) * (qp + qb) + qc;
#ifdef ELEM_T_IS_FLOAT
  return q_exp / pow(2, z);
#else
  return q_exp >> z;
#endif
}

acc_t apply_igelu(acc_t q, acc_t qb, acc_t qc) {
  const acc_t q_sign = q < 0 ? -1 : 1;
  const acc_t q_clipped = abs(q) > (-qb) ? (-qb) : abs(q);
  const acc_t q_poly = (q_clipped + qb)*(q_clipped + qb) + qc;
  const acc_t q_erf = q_sign * q_poly;

  return q * (q_erf + qc);
}

acc_t apply_pre_activation_acc(acc_t value) {
  if (gemmini_state.acc_act == IGELU) {
    return apply_igelu(value, gemmini_state.igelu_qb, gemmini_state.igelu_qc);
  } else if (gemmini_state.acc_act == LAYERNORM) {
    const uint8_t stat_id = gemmini_state.norm_stat_id;
    const acc_t norm_mean = gemmini_state.norm_mean[stat_id];
    const acc_t  norm_inv_stddev = gemmini_state.norm_inv_stddev[stat_id];
    return (value - norm_mean) * norm_inv_stddev;
  } else if (gemmini_state.acc_act == SOFTMAX) {
    const uint8_t stat_id = gemmini_state.norm_stat_id;
    const acc_t norm_max = gemmini_state.norm_max[stat_id];
    const acc_t norm_inv_sum_exp = gemmini_state.norm_inv_sum_exp[stat_id];
    //printf("value : %d, norm_max : %d, igelu_qb : %d, igelu_qc : %d, qln2 : %d, qln2_inv : %d\n", value,norm_max,gemmini_state.igelu_qb, gemmini_state.igelu_qc, gemmini_state.qln2,gemmini_state.qln2_inv);
    //printf("apply_iexp : %d\n",apply_iexp(value - norm_max, gemmini_state.igelu_qb, gemmini_state.igelu_qc, gemmini_state.qln2, gemmini_state.qln2_inv));
    return acc_scale(apply_iexp(value - norm_max, gemmini_state.igelu_qb, gemmini_state.igelu_qc,
        gemmini_state.qln2, gemmini_state.qln2_inv), norm_inv_sum_exp);
  } else {
    return value;
  }
}

bool apply_norm(const acc_t * x, size_t len, NormCmd cmd) {
  // The return value is whether or not the command should write to main memory
  const uint8_t stat_id = gemmini_state.norm_stat_id;
  acc_t *norm_sum = &gemmini_state.norm_sum[stat_id];
  acc_t *norm_count = &gemmini_state.norm_count[stat_id];
  acc_t *norm_mean = &gemmini_state.norm_mean[stat_id];
  acc_scale_t *norm_inv_stddev = &gemmini_state.norm_inv_stddev[stat_id];
  acc_t *norm_max = &gemmini_state.norm_max[stat_id];
  acc_t *norm_running_max = &gemmini_state.norm_running_max[stat_id];
  acc_scale_t *norm_inv_sum_exp = &gemmini_state.norm_inv_sum_exp[stat_id];
  bool *norm_reset = &gemmini_state.norm_reset[stat_id];
  if (*norm_reset) {
    *norm_sum = 0;
    *norm_count = 0;
    *norm_running_max = -2147483648;
  }
  *norm_reset = cmd == RESET || cmd == MEAN || cmd == INV_STDDEV;

  if (cmd == SUM || cmd == MEAN) {
    for (size_t i = 0; i < len; i++)
      *norm_sum += x[i];
    *norm_count += len;
  } else if (cmd == VARIANCE || cmd == INV_STDDEV) {
    for (size_t i = 0; i < len; i++)
      *norm_sum += (x[i] - *norm_mean)*(x[i] - *norm_mean);
    *norm_count += len;
  } else if (cmd == MAX) {
    for (size_t i = 0; i < len; i++) {
      *norm_running_max = x[i] > *norm_running_max ? x[i] : *norm_running_max;
    }
  } else if (cmd == SUM_EXP || cmd == INV_SUM_EXP) {
    *norm_max = *norm_running_max;
    for (size_t i = 0; i < len; i++) {
      *norm_sum += apply_iexp(x[i] - *norm_max, gemmini_state.igelu_qb, gemmini_state.igelu_qc,
          gemmini_state.qln2, gemmini_state.qln2_inv);
    }
  }

  if (cmd == MEAN) {
    *norm_mean = *norm_sum / *norm_count;
  } else if (cmd == INV_STDDEV) {
    acc_t variance = *norm_sum / *norm_count;
    acc_t norm_stddev = sqrt((double)variance);
    if (variance == 0) 
	    norm_stddev = 1;
    *norm_inv_stddev = (acc_scale_t)1.0 / (acc_scale_t)norm_stddev;
  } else if (cmd == INV_SUM_EXP) {
    *norm_running_max = -2147483648;
    *norm_inv_sum_exp = ((acc_scale_t) 127.0) / *norm_sum;
  }

  return cmd == RESET;
}

NormCmd non_terminating_norm_cmd(NormCmd cmd) {
  if (cmd == RESET)
    cmd = RESET;
  else if (cmd == MEAN)
    cmd = SUM;
  else if (cmd == INV_STDDEV)
    cmd = VARIANCE;
  else if (cmd == INV_SUM_EXP)
    cmd = SUM_EXP;
  return cmd;
}
#endif
