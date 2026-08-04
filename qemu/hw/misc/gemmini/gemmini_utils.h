/* Utility Functions for Gemmini */
#include "gemmini.h"

/* Erase below for Debugging */
#undef qemu_log
#define qemu_log(...) do {} while (0)

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

elem_t MVIN_SCALE(elem_t x, scale_t scale) {
    float y = rintf(x * scale);
    if (y > INT8_MAX) return INT8_MAX;
    else if (y < INT8_MIN) return INT8_MIN;
    else return (elem_t)y;
}

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

elem_t apply_activation_sys(GemminiState *gm, elem_t value) {
  return apply_activation(value, gm->sys_act);
}
elem_t apply_activation_acc(GemminiState *gm, elem_t value) {
  return apply_activation(value, gm->acc_act);
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

acc_t apply_pre_activation_acc(GemminiState *gm, acc_t value) {
  if (gm->acc_act == IGELU) {
    return apply_igelu(value, gm->igelu_qb, gm->igelu_qc);
  } else if (gm->acc_act == LAYERNORM) {
    const uint8_t stat_id = gm->norm_stat_id;
    const acc_t norm_mean = gm->norm_mean[stat_id];
    /* norm_inv_stddev is a fractional scale (1/stddev, e.g. 0.5); reading it into
     * an integer acc_t truncated fractions to 0, zeroing rows whose stddev > 1.
     * Must be acc_scale_t (float), matching the field type. */
    const acc_scale_t norm_inv_stddev = gm->norm_inv_stddev[stat_id];
    return (value - norm_mean) * norm_inv_stddev;
  } else if (gm->acc_act == SOFTMAX) {
    qemu_log("SOFTMAX pre-activation value: %d\n", value);
    const uint8_t stat_id = gm->norm_stat_id;
    const acc_t norm_max = gm->norm_max[stat_id];
    /* norm_inv_sum_exp is a fractional scale (127.0/sum, ~0.025); reading it into
     * an integer acc_t truncated it to 0, zeroing every softmax output. Must be
     * acc_scale_t (float), matching the field type and acc_scale()'s scale arg. */
    const acc_scale_t norm_inv_sum_exp = gm->norm_inv_sum_exp[stat_id];
    qemu_log("value : %d, norm_max : %d, igelu_qb : %d, igelu_qc : %d, qln2 : %d, qln2_inv : %d\n", value,norm_max,gm->igelu_qb, gm->igelu_qc, gm->qln2,gm->qln2_inv);
    qemu_log("apply_iexp : %d, scale with %d\n",apply_iexp(value - norm_max, gm->igelu_qb, gm->igelu_qc, gm->qln2, gm->qln2_inv), norm_inv_sum_exp);
    return acc_scale(apply_iexp(value - norm_max, gm->igelu_qb, gm->igelu_qc,
        gm->qln2, gm->qln2_inv), norm_inv_sum_exp);
  } else {
    return value;
  }
}

bool apply_norm(GemminiState *gm, const acc_t * x, size_t len, NormCmd cmd) {
  // The return value is whether or not the command should write to main memory
  const uint8_t stat_id = gm->norm_stat_id;
  acc_t *norm_sum = &gm->norm_sum[stat_id];
  acc_t *norm_count = &gm->norm_count[stat_id];
  acc_t *norm_mean = &gm->norm_mean[stat_id];
  acc_scale_t *norm_inv_stddev = &gm->norm_inv_stddev[stat_id];
  acc_t *norm_max = &gm->norm_max[stat_id];
  acc_t *norm_running_max = &gm->norm_running_max[stat_id];
  acc_scale_t *norm_inv_sum_exp = &gm->norm_inv_sum_exp[stat_id];
  bool *norm_reset = &gm->norm_reset[stat_id];
  qemu_log("APPLY NORM: cmd=%d, stat_id=%d, norm_max=%d, norm_reset=%d, norm_sum=%d, normcmd=%d\n", cmd, stat_id, *norm_max, *norm_reset, *norm_sum, cmd);

  if (*norm_reset) {
    *norm_sum = 0;
    *norm_count = 0;
    *norm_running_max = -2147483648;
    qemu_log("RESETTING NORM STATS\n");
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
      qemu_log("CHECKING MAX: current x[%lu]=%d, current norm_running_max=%d\n", i, x[i], *norm_running_max);
      if(x[i] > *norm_running_max){
        qemu_log("NEW MAX FOUND: %d (old: %d)\n", x[i], *norm_running_max);
      }

      *norm_running_max = x[i] > *norm_running_max ? x[i] : *norm_running_max;
    }
  } else if (cmd == SUM_EXP || cmd == INV_SUM_EXP) {
    *norm_max = *norm_running_max;
    for (size_t i = 0; i < len; i++) {
      *norm_sum += apply_iexp(x[i] - *norm_max, gm->igelu_qb, gm->igelu_qc,
          gm->qln2, gm->qln2_inv);
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
