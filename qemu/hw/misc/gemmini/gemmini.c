#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "gemmini.h"
#include "gemmini_utils.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "gemmini_manager.h"
#include "qemu/thread.h"

/* Erase below for Debugging */
#undef qemu_log
#define qemu_log(...) do {} while (0)

/* QOM Initialization for Gemmini */

// GemminiState *global_gemmini_state;

// static void gemmini_realize(DeviceState *dev, Error **errp)
// {
//     GemminiState *s = GEMMINI_DEVICE(dev);

//     global_gemmini_state = s;
//     all_gemminis = g_list_append(all_gemminis, s);
//     s->id = atoi(dev->id + strlen("gemmini"));

//     memset(s->spad, 0, sizeof(s->spad));
//     memset(s->accumulator, 0, sizeof(s->accumulator));
//     memset(s->pe_state, 0, sizeof(s->pe_state));
//     memset(s->bias_buffer, 0, sizeof(s->bias_buffer));

//     qemu_log("Gemmini device realized, id=%s\n", dev->id);
// }

static void gemmini_realize(DeviceState *dev, Error **errp)
{
    GemminiState *s = GEMMINI_DEVICE(dev);

    s->id = atoi(dev->id + strlen("gemmini"));
    gemmini_registry_add(s, s->id);

    qemu_mutex_init(&s->gemmini_lock);
    s->busy = false;

    memset(s->spad, 0, sizeof(s->spad));
    memset(s->accumulator, 0, sizeof(s->accumulator));
    memset(s->pe_state, 0, sizeof(s->pe_state));
    memset(s->bias_buffer, 0, sizeof(s->bias_buffer));

    qemu_log("Gemmini device realized, id=%s\n", dev->id);
}

static void gemmini_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = gemmini_realize;
}

static const TypeInfo gemmini_info = {
    .name          = TYPE_GEMMINI_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(GemminiState),
    .class_init    = gemmini_class_init,
};

static void gemmini_register_types(void)
{
    type_register_static(&gemmini_info);
}

type_init(gemmini_register_types);

/*
 * BEGIN GEMMINI CUSTOM INSTRUCTIONS
 */

void gemmini_config(u_int64_t rs1, u_int64_t rs2, GemminiState *gm){
    if((rs1 & 0b11)==0){ // rs1[1:0] == 2'b00, config_ex, configure execute pipeline
        Dataflow new_mode;
        Activation new_act;
        reg_t new_sys_shift, new_sys_acc_shift, new_c_stride, new_a_stride, new_a_transpose, new_b_transpose;
        /*extract rs1[2], 0 = output stationary, 1 = weight stationary*/
        bool rs1_2 = (rs1 >> 2) & 0b1;
        if(rs1_2 == 0){
            new_mode = OS;
        }else{
            new_mode = WS;
        }
        /*extract rs1[4:3], for activation function*/
        uint32_t rs1_4_3 = (rs1 >> 3) & 0b11;
        if(rs1_4_3 == 0){
            if(gm->acc_act != 4){
                new_act = NONE;
            }else{
                new_act = SOFTMAX;
            }
        }else if (rs1_4_3 == 1) {
            new_act = RELU;
        }else if (rs1_4_3 == 2) {
            new_act = LAYERNORM;
        }else if (rs1_4_3 == 3) {
            new_act = IGELU;
        }else{
            /*ILLEGAL ACTIVATION FUNCTION*/
            assert(false);
        }

        new_sys_shift = (rs2) & 0xFFFFFFFF;
        new_sys_acc_shift = (rs1 >> 32) & 0xFFFFFFFF;
        new_c_stride = (rs2 >> 48) & 0xFFFF;
        new_a_stride = (rs1 >> 16) & 0xFFFF;
        new_a_transpose = (rs1 >> 8) & 0x1;
        new_b_transpose = (rs1 >> 9) & 0x1;

        const bool set_only_strides = (rs1 >> 7) & 0x1;
        /*DEBUG*/
        qemu_log("GEMMINI: config_ex - set dataflow mode from %d to %d\n", gm->mode, new_mode);
        qemu_log("GEMMINI: config_ex - set activation function from %d to %d\n", gm->sys_act, new_act);
        qemu_log("GEMMINI: config_ex - set sys_shift from %lu to %lu\n", gm->sys_shift, new_sys_shift);

        if (!set_only_strides) {
            gm->mode = new_mode;
            gm->sys_act = new_act;
            gm->sys_shift = new_sys_shift;
            gm->sys_acc_shift = new_sys_acc_shift;
            gm->a_transpose = new_a_transpose;
            gm->b_transpose = new_b_transpose;
        }	
        gm->c_stride = new_c_stride;
        gm->a_stride = new_a_stride;

    /*rs1[1:0] == 2'b01, config_mvin, configure load pipeline*/
    }else if ((rs1 & 0b11) == 1){
    	const int state_id = (rs1 >> 3) & 0x3;
        
    	qemu_log("GEMMINI: config_mvin(id=%d) - set load stride from %lu to %lu\n", state_id, gm->load_strides[state_id], rs2);
    	gm->load_strides[state_id] = rs2;
    	gm->load_block_strides[state_id] = (rs1 >> 16) & 0xFFFF;

#if defined(HAS_MVIN_SCALE) || defined(HAS_MVIN_ACC_SCALE)
    	qemu_log("GEMMINI: config_mvin - set load scale from %f to %f\n", gm->load_scales[state_id], scale_t_bits_to_scale_t(rs1 >> 32));
    	gm->load_scales[state_id] = scale_t_bits_to_scale_t(rs1 >> 32);
    	gm->load_shrunks[state_id] = (rs1 >> 2) & 1;
#endif
    	gm->pixels_per_rows[state_id] = (rs1 >> 8) & 0xFF;

    	if (gm->pixels_per_rows[state_id] == 0){
		    gm->pixels_per_rows[state_id] = 1;
        }
        /*DEBUG*/
        qemu_log("GEMMINI: config_mvin - [stride] = %lu (rs2)\n", rs2);
        qemu_log("GEMMINI: config_mvin - [scale float] = %f\n", scale_t_bits_to_scale_t(rs1 >> 32));
        qemu_log("GEMMINI: config_mvin - [shrunk] = %u\n", (uint8_t)((rs1 >> 2) & 0x1));
        qemu_log("GEMMINI: config_mvin - [id] = %u\n", (uint8_t)((rs1 >> 3) & 0x7));
        qemu_log("GEMMINI: config_mvin - [block_mvin_stride] = %u\n", (uint16_t)((rs1 >> 16) & 0xFFFF));
        qemu_log("GEMMINI: config_mvin - [pixel_repeats] = %u\n", (uint8_t)((rs1 >> 8) & 0xFF));
        qemu_log("\n");

#ifndef HAS_FIRST_LAYER_OPTIMIZATIONS
        /* Not So Sure About Here */
	    // assert(gemmini_state.pixels_per_rows[state_id] <= 1, "If Gemmini is built without first-layer optimizations, then 'pixels_per_rows' cannot be larger than 1\n");
#endif
    }else if ((rs1 & 0b11) == 2) {
	    /*rs1[1:0] == 2'b10, config_mvout, configure store pipeline*/
    	gm->store_stride = rs2 & 0xFFFFFFFF;
        qemu_log("GEMMINI: config_mvout - set store stride from %lu to %lu\n", gm->store_stride, rs2 & 0xFFFFFFFF);
	
	    Activation new_act;
	    uint64_t rs1_3_2 = (rs1 >> 2) & 0b11;
        /* Set Activation Function Based on rs1[1:0] */
    	if (rs1_3_2 == 0) {
     		if (gm->acc_act != 4) {
        		new_act = NONE;
      		} else {
        		new_act =SOFTMAX;
      		}	
    	} else if (rs1_3_2 == 1) {
     		new_act = RELU;
    	} else if (rs1_3_2 == 2) {
    		new_act = LAYERNORM;
    	} else if (rs1_3_2 == 3) {
      		new_act = IGELU;
    	} else {
    		assert(false);
   	    }
    	gm->acc_act = new_act;

    	uint64_t new_acc_shift = (rs2 >> 32) & 0xFFFFFFFF;
    	gm->acc_shift = acc_scale_t_bits_to_acc_scale_t(new_acc_shift);

        /* Pooling Configurations, Not Used so much */
    	gm->pool_stride = (rs1 >> 4) & 0x3;
    	gm->pool_size = (rs1 >> 6) & 0x3;
    	gm->pool_upad = (rs1 >> 8) & 0x3;
    	gm->pool_lpad = (rs1 >> 10) & 0x3;
    	gm->pool_out_dim = (rs1 >> 24) & 0xFF;
    	gm->pool_porows = (rs1 >> 32) & 0xFF;
    	gm->pool_pocols = (rs1 >> 40) & 0xFF;
    	gm->pool_orows = (rs1 >> 48) & 0xFF;
    	gm->pool_ocols = (rs1 >> 56) & 0xFF;

    	if (gm->pool_stride == 0) {
    	    qemu_log("GEMMINI: config_mvout - no pooling\n");
    	} else {
    	    qemu_log("GEMMINI: config_mvout - set pool_stride to %u\n", gm->pool_stride);
    	    qemu_log("GEMMINI: config_mvout - set pool_size to %u\n", gm->pool_size);
    	    qemu_log("GEMMINI: config_mvout - set pool_upad to %u\n", gm->pool_upad);
     	    qemu_log("GEMMINI: config_mvout - set pool_lpad to %u\n", gm->pool_lpad);
	        qemu_log("GEMMINI: config_mvout - set pool_out_dim to %u\n", gm->pool_out_dim);
	        qemu_log("GEMMINI: config_mvout - set pool_porows to %u\n", gm->pool_porows);
    	    qemu_log("GEMMINI: config_mvout - set pool_pocols to %u\n", gm->pool_pocols);
    	    qemu_log("GEMMINI: config_mvout - set pool_orows to %u\n", gm->pool_orows);
       	    qemu_log("GEMMINI: config_mvout - set pool_ocols to %u\n", gm->pool_ocols);
	        qemu_log("GEMMINI: config_mvout - rs1 is %lu\n", rs1);
    	}	
    }else if ((rs1 & 0b11) == 3) {
    /* rs1[1:0] == 2'b11, config_norm, configure norm pipeline */
	    gm->norm_stat_id = (rs1 >> 8) & 0xFF;
    	if (!((rs1 >> 17) & 1)) {
            /* set stat ID only */
		    gm->igelu_qb = rs2 & 0xFFFFFFFF;
      		gm->igelu_qc = (rs2 >> 32) & 0xFFFFFFFF;

      		uint8_t act_msb = (rs1 >> 16) & 0x1;
      		gm->acc_act = (act_msb << 2) | (int)gm->acc_act;
      		assert((int)(gm->acc_act) <= 4);

            if ((rs1 >> 18) & 1) { // q const type = 1
                gm->qln2_inv = (rs1 >> 32) & 0xFFFFFFFF;
                qemu_log("GEMMINI: config_norm - q_const_inv: %d\n", gm->qln2_inv);
            } else {
                gm->qln2 = (rs1 >> 32) & 0xFFFFFFFF;
                qemu_log("GEMMINI: config_norm - q_const: %d\n",  gm->qln2);
            }
        }
    }
}

/* Set Dimensions */
void loop_ws_config_bounds(u_int64_t rs1, u_int64_t rs2, GemminiState *gm) {
    gm->loop_ws_pad_I = rs1 & 0xFFFF;
    gm->loop_ws_pad_J = (rs1 >> 16) & 0xFFFF;
    gm->loop_ws_pad_K = (rs1 >> 32) & 0xFFFF;
    gm->loop_ws_I = rs2 & 0xFFFF;
    gm->loop_ws_J = (rs2 >> 16) & 0xFFFF;
    gm->loop_ws_K = (rs2 >> 32) & 0xFFFF;

    qemu_log("RoCC: LOOP_WS_CONFIG_BOUNDS\n");
    qemu_log("RoCC: pad_I = %d, pad_J = %d, pad_K = %d\n", gm->loop_ws_pad_I, gm->loop_ws_pad_J, gm->loop_ws_pad_K);
    qemu_log("RoCC: I = %d, J = %d, K = %d\n", gm->loop_ws_I, gm->loop_ws_J, gm->loop_ws_K);
}

/* Set Address of A and B for loop_ws*/
void loop_ws_addrs_ab(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->loop_ws_A = rs1;
    gm->loop_ws_B = rs2;

    qemu_log("RoCC: LOOP_WS_ADDRS_AB\n");
    qemu_log("RoCC: A base addr = 0x%08lx, B base addr = 0x%08lx\n", gm->loop_ws_A, gm->loop_ws_B);
}

/* Set Address of D and C for loop_ws*/
void loop_ws_addrs_dc(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->loop_ws_D = rs1;
    gm->loop_ws_C = rs2;

    qemu_log("RoCC: LOOP_WS_ADDRS_DC\n");
    qemu_log("RoCC: D base addr = 0x%08lx, C base addr = 0x%08lx\n", gm->loop_ws_D, gm->loop_ws_C);
}

/* Set Strides of A and B for loop_ws*/
void loop_ws_strides_ab(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->loop_ws_A_stride = rs1;
    gm->loop_ws_B_stride = rs2;

    qemu_log("RoCC: LOOP_WS_STRRIDES_AB\n");
    qemu_log("RoCC: A stride = %lu, B stride = %lu\n", gm->loop_ws_A_stride, gm->loop_ws_B_stride);
}

/* Set Strides of D and C for loop_ws*/
void loop_ws_strides_dc(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->loop_ws_D_stride = rs1;
    gm->loop_ws_C_stride = rs2;

    qemu_log("RoCC: LOOP_WS_STRRIDES_DC\n");
    qemu_log("RoCC: D stride = %lu, C stride = %lu\n", gm->loop_ws_D_stride, gm->loop_ws_C_stride);
}

/* Set Dimensions */
void k_gemv_loop_ws_config_bounds(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->gemv_loop_ws_pad_I = rs1 & 0xFFFF;
    gm->gemv_loop_ws_pad_J = (rs1 >> 16) & 0xFFFF;
    gm->gemv_loop_ws_pad_K = (rs1 >> 32) & 0xFFFF;
    gm->gemv_loop_ws_I = rs2 & 0xFFFF;
    gm->gemv_loop_ws_J = (rs2 >> 16) & 0xFFFF;
    gm->gemv_loop_ws_K = (rs2 >> 32) & 0xFFFF;

    qemu_log("RoCC: K_GEMV_LOOP_WS_CONFIG_BOUNDS\n");
    qemu_log("RoCC: pad_I = %d, pad_J = %d, pad_K = %d\n", gm->gemv_loop_ws_pad_I, gm->gemv_loop_ws_pad_J, gm->gemv_loop_ws_pad_K);
    qemu_log("RoCC: I = %d, J = %d, K = %d\n", gm->gemv_loop_ws_I, gm->gemv_loop_ws_J, gm->gemv_loop_ws_K);
}

/* Set Address of A and B for loop_ws*/
void k_gemv_loop_ws_addrs_ab(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->gemv_loop_ws_A = rs1;
    gm->gemv_loop_ws_B = rs2;

    qemu_log("RoCC: K_GEMV_LOOP_WS_ADDRS_AB\n");
    qemu_log("RoCC: A base addr = 0x%08lx, B base addr = 0x%08lx\n", gm->gemv_loop_ws_A, gm->gemv_loop_ws_B);
}

/* Set Address of D and C for loop_ws*/
void k_gemv_loop_ws_addrs_dc(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->gemv_loop_ws_D = rs1;
    gm->gemv_loop_ws_C = rs2;

    qemu_log("RoCC: K_GEMV_LOOP_WS_ADDRS_DC\n");
    qemu_log("RoCC: D base addr = 0x%08lx, C base addr = 0x%08lx\n", gm->gemv_loop_ws_D, gm->gemv_loop_ws_C);
}

/* Set Strides of A and B for loop_ws*/
void k_gemv_loop_ws_strides_ab(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->gemv_loop_ws_A_stride = rs1;
    gm->gemv_loop_ws_B_stride = rs2;

    qemu_log("RoCC: K_GEMV_LOOP_WS_STRRIDES_AB\n");
    qemu_log("RoCC: A stride = %lu, B stride = %lu\n", gm->gemv_loop_ws_A_stride, gm->gemv_loop_ws_B_stride);
}

/* Set Strides of D and C for loop_ws*/
void k_gemv_loop_ws_strides_dc(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    gm->gemv_loop_ws_D_stride = rs1;
    gm->gemv_loop_ws_C_stride = rs2;

    qemu_log("RoCC: K_GEMV_LOOP_WS_STRRIDES_DC\n");
    qemu_log("RoCC: D stride = %lu, C stride = %lu\n", gm->gemv_loop_ws_D_stride, gm->gemv_loop_ws_C_stride);
}

/* Set Gemmini States(Addr, Rows, Cols) before Compute */
void preload(uint64_t bd_addr, uint64_t c_addr, GemminiState *gm) {
    gm->preload_sp_addr = (uint32_t)(bd_addr & 0xFFFFFFFF);
    gm->output_sp_addr = (uint32_t)(c_addr & 0xFFFFFFFF);

    gm->preload_cols = (bd_addr >> addr_len) & 0xFFFF;
    gm->preload_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
    gm->output_cols = (c_addr >> addr_len) & 0xFFFF;
    gm->output_rows = (c_addr >> (addr_len + 16)) & 0xFFFF;

    qemu_log("GEMMINI: preload - scratchpad output addr = 0x%08x, scratchpad preload addr = 0x%08x\n",gm->output_sp_addr, gm->preload_sp_addr);
}

/* Move Elements from DRAM to Spad or Accumulator */
void mvin(uint64_t dram_addr, uint64_t sp_addr, int state_id, GemminiState *gm)
{
    /* Whether to Write to Accumulator or SPAD*/
    bool accumulator = (sp_addr >> 31) & 0x1;
    /* Whether to Accumulate or Overwrite */
    bool accumulate  = (sp_addr >> 30) & 0x1;
    /* Address to write on, Number of Cols and Rows to write */
    uint32_t base_row_addr = (uint32_t)(sp_addr & 0x1FFFFFFF);
    uint32_t cols = (sp_addr >> 32) & 0xFFFF;
    uint32_t rows = (sp_addr >> 48) & 0xFFFF;

    bool is_zeros = (dram_addr == 0);

    uint64_t load_stride        = gm->load_strides[state_id];
    uint16_t load_block_stride  = gm->load_block_strides[state_id];
    bool     load_shrunk        = gm->load_shrunks[state_id];
#if defined(HAS_MVIN_SCALE) || defined(HAS_MVIN_ACC_SCALE)
    float    load_scale         = gm->load_scales[state_id];
#endif
    /* How much Duplication will each row have(Not used frequently) */
    uint8_t  pixels_per_row     = gm->pixels_per_rows[state_id];
    /*DEBUG*/
    qemu_log(
        "MVIN: sp_addr=0x%lx acc=%d base_row=%lu rows=%lu cols=%lu\n",
        sp_addr,
        (int)((sp_addr >> 31) & 1),
        (unsigned long)(sp_addr & 0x3FFFFFF),
        (unsigned long)((sp_addr >> (ADDR_LEN + 16)) & 0xFFFF),
        (unsigned long)((sp_addr >> ADDR_LEN) & 0xFFFF)
    );

    /* Allocate A Temporary Buffer to Hold One Row from DRAM */
    /* Crank up elem size if destination is Bias spad (BANK_ROWS * 3) */
    size_t elem_size = (accumulator && !load_shrunk) || base_row_addr >= BANK_ROWS * 3 ? sizeof(acc_t) : sizeof(elem_t);
    size_t row_bytes = cols * elem_size;
    uint8_t *row_buf = g_malloc(row_bytes);

    for (uint32_t row = 0; row < rows; ++row) {
        if (!is_zeros) {
            for(uint32_t col = 0; col < row_bytes; col++) {
                /* Call a Callback Function to Helper, Read a byte */
                row_buf[col] = gm->mem_read(gm->mem_opaque, dram_addr + row * load_stride + col, 1);
                gm->total_reads_in_bytes++;
                //qemu_log("[MVIN] read from dram addr 0x%lx: %02x\n", dram_addr + row * load_stride + col, row_buf[col]);
            }
            qemu_log("[MVIN] row_buf dump:");
            for (uint32_t i = 0; i < row_bytes; i++) {
                qemu_log(" %02x", row_buf[i]);
            }
            qemu_log("\n");
        } else {
            /* No Address to Move in, fill with zeros */
            memset(row_buf, 0, row_bytes);
        }

        for (uint32_t col = 0; col < cols; ++col) {
            uint32_t block = col / DIM;
            uint32_t spad_col = col % DIM;
            uint32_t spad_row = base_row_addr + row + block * load_block_stride;

            for (size_t pixel = 0; pixel < pixels_per_row ; ++pixel) {
                if( spad_row + pixel >= BANK_NUM*BANK_ROWS ) {
                    /* Out of SPAD/ACC Bounds, Skip */
                    continue;
                }
                /* Load Buffer to Accumulator or Bias SPAD */
                if (accumulator) {
                    acc_t value;
                    if (!load_shrunk) {
                        memcpy(&value,
                               row_buf + col * sizeof(acc_t),
                               sizeof(acc_t));
#ifdef HAS_MVIN_ACC_SCALE
                        value = mvin_scale_acc(value, load_scale);
#endif
                    } else {
                        elem_t v;
                        memcpy(&v,
                               row_buf + col * sizeof(elem_t),
                               sizeof(elem_t));
#ifdef HAS_MVIN_SCALE
                        value = mvin_scale(v, load_scale);
#else
                        value = v;
#endif
                    }
                    /* Accumulate or Overwrite */
                    if (accumulate) {
                        gm->accumulator[spad_row + pixel][spad_col] += value;
                        qemu_log(
                            "[MVIN][ACC][ADD] row=%u col=%u pixel=%zu -> acc[%u][%u] = %ld\n",
                            row, col, pixel,
                            spad_row + (uint32_t)pixel,
                            spad_col,
                            (long)gm->accumulator[spad_row + pixel][spad_col]
                        );
                    } else {
                        gm->accumulator[spad_row + pixel][spad_col] = value;
                        qemu_log(
                            "[MVIN][ACC][OVERWRITE] row=%u col=%u pixel=%zu -> acc[%u][%u] = %ld\n",
                            row, col, pixel,
                            spad_row + (uint32_t)pixel,
                            spad_col,
                            (long)gm->accumulator[spad_row + pixel][spad_col]
                        );
                    }
                } else {
                    /* Accumulator = 0, Write to SPAD */
                    if( base_row_addr >= BANK_ROWS * 3 ){
                        acc_t value;
                        memcpy(&value,
                            row_buf + col * elem_size,
                            elem_size);
#ifdef HAS_MVIN_SCALE
                        elem_t scaled = mvin_scale(value, load_scale);
#endif
                        gm->spad[spad_row + pixel][spad_col] = scaled;
                        qemu_log(
                            "[MVIN][SPAD] state=%d dram=0x%lx "
                            "row=%u col=%u pixel=%zu -> spad[%u][%u] = %d\n",
                            state_id,
                            dram_addr,
                            row,
                            col,
                            pixel,
                            spad_row + (u_int32_t)pixel,
                            spad_col,
                            (int)value
                        );    
                    } else {
                        elem_t value;
                        memcpy(&value,
                            row_buf + col * sizeof(elem_t),
                            sizeof(elem_t));
#ifdef HAS_MVIN_SCALE
                        value = mvin_scale(value, load_scale);
#endif
                        gm->spad[spad_row + pixel][spad_col] = value;
                        qemu_log(
                            "[MVIN][SPAD] state=%d dram=0x%lx "
                            "row=%u col=%u pixel=%zu -> spad[%u][%u] = %d\n",
                            state_id,
                            dram_addr,
                            row,
                            col,
                            pixel,
                            spad_row + (u_int32_t)pixel,
                            spad_col,
                            (int)value
                        );
                    }
                }
            }
        }
    }
    g_free(row_buf);
}

/* Perform Matrix Multiplication */
void compute(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm) {
    uint32_t a_addr_real = (uint32_t)(a_addr & 0xFFFFFFFF);
    uint32_t bd_addr_real = (uint32_t)(bd_addr & 0xFFFFFFFF);

    bool accumulate = a_addr >> (addr_len + 31) & 0x1;
    
    const uint16_t a_cols = (a_addr >> addr_len) & 0xFFFF;
    const uint16_t a_rows = (a_addr >> (addr_len + 16)) & 0xFFFF;

    const uint16_t bd_cols = (bd_addr >> addr_len) & 0xFFFF;
    const uint16_t bd_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
    qemu_log("GEMMINI: compute - preload = %d, scratchpad A addr = 0x%08x,"
           "scratchpad B addr 0x%08x\n", preload, a_addr_real, bd_addr_real);
    // Preload
    if (preload) {
	qemu_log("GEMMINI: compute - PEs after preloading:\n");
        for (size_t i = 0; i < DIM; i++) {
            for (size_t j = 0; j < DIM; j++) {
                if (~gm->preload_sp_addr != 0) {
                    // 현재는 Acc에서 preload는 미지원
                    assert(((gm->preload_sp_addr >> 30) & 0b11) == 0);
                }
                bool preload_transpose = (gm->mode == WS) && gm->b_transpose;
                size_t r = preload_transpose ? j : i;
                size_t c = preload_transpose ? i : j;
                // In OS mode, pe_state stores the accumulator values
                // In WS mode, pe_state stores the persistent weight matrix
                if (i < gm->preload_rows && j < gm->preload_cols) {
                    int8_t preload_value = (~gm->preload_sp_addr == 0)? 0 : gm->spad[gm->preload_sp_addr + r][c];
		            gm->pe_state[i][j] = preload_value;
                } else {
                    gm->pe_state[i][j] = 0;
                }
#ifdef ELEM_T_IS_FLOAT
		        qemu_log("%f ", gm->pe_state[i][j]);
#else
		        qemu_log("%d ", gm->pe_state[i][j]);
#endif
            }
	        qemu_log("\n");
        }
    }
    /* Make a Buffer for Results of single multiplication */
    acc_t results[DIM][DIM] = {0};
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            if (i < bd_rows && j < bd_cols) {
                results[i][j] = (~bd_addr_real == 0) ? 0 : gm->spad[bd_addr_real + i][j];
            } else {
                results[i][j] = 0;
            }
        }
    }
    qemu_log("compute A: ");
    for( size_t k = 0; k < DIM; ++k ) {
        qemu_log("%d ", gm->spad[a_addr_real][k]);
    }
    qemu_log("\n");

    /* Perform Matrix Multiplication */
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            for (size_t k = 0; k < DIM; ++k) {
                elem_t a=0;
                if (~a_addr_real != 0) {
                    const size_t r = gm->a_stride * (gm->a_transpose ? k : i);
                    const size_t c = gm->a_transpose ? i : k;

                    a = i < a_rows && k < a_cols ? gm->spad[a_addr_real + r][c]: 0 ;
                }
                /* WS Mode, Most Operations are Done in WS Mode */
                if (gm->mode == WS) {
                    results[i][j] += a * gm->pe_state[k][j];
                } else {
                    /* OS Mode */
                    elem_t b = 0;
                    if (~bd_addr_real != 0) {
                        const size_t r = gm->b_transpose ? j : k;
                        const size_t c = gm->b_transpose ? k : j;

                        b = k < bd_rows && j < bd_cols ? gm->spad[bd_addr_real+r][c] : 0;
                    }
		            gm->pe_state[i][j] += a * b;
                }
            }
        }
    }
    qemu_log("GEMMINI: compute - PEs after matmul:\n");
    for (size_t i = 0; i < DIM; ++i) {
	    for (size_t j = 0; j < DIM; ++j) {
#ifdef ELEM_T_IS_FLOAT
            qemu_log("%f ", gm->pe_state[i][j]);
#else
            qemu_log("%d ", gm->pe_state[i][j]);
#endif
        }
        qemu_log("\n");
    }
    qemu_log("GEMMINI: compute - Results of matmul:\n");
    for (size_t i = 0; i < gm->output_rows; ++i) {
        for (size_t j = 0; j < gm->output_cols; ++j) {
#ifdef ELEM_T_IS_FLOAT
            qemu_log("%f ", results[i][j]);
#else
            qemu_log("%d ", results[i][j]);
#endif
        }
        qemu_log("\n");
    }
    /* Store Results Back to Spad or Accumulator */
    if (~gm->output_sp_addr != 0) {
        /* output addr is valid (not -1)*/
        bool acc = (gm->output_sp_addr >> 31) & 0x1;
        bool acc_accum = (gm->output_sp_addr >> 30) & 0x1;

        qemu_log("GEMMINI: compute - storing results to %s %s\n",
               acc ? "accumulator" : "scratchpad",
               acc_accum ? "with accumulation" : "without accumulation");
        
        uint32_t base_sp_addr = (gm->output_sp_addr & 0x1FFFFFFF) % BANK_ROWS;

        for (size_t i = 0; i < gm->output_rows; ++i) {
            for (size_t j = 0; j < gm->output_cols; ++j) {
                acc_t value = gm->mode == OS ? gm->pe_state[i][j] : results[i][j];
                if (acc) {
		            if (acc_accum) {
                        qemu_log("accumulator[%zu][%zu] : %d = %d + %d\n",
                                base_sp_addr + gm->c_stride* i,j,
                                gm->accumulator[base_sp_addr + gm->c_stride * i][j] + value,
                                gm->accumulator[base_sp_addr + gm->c_stride* i][j], value);
			            gm->accumulator[base_sp_addr + gm->c_stride * i][j] += value;
                    } else {
                        qemu_log("accumulator[%zu][%zu] : %d = %d <- %d\n",
                                base_sp_addr + gm->c_stride* i,j,value,
                                gm->accumulator[base_sp_addr + gm->c_stride* i][j],
                                value);
			            gm->accumulator[base_sp_addr + gm->c_stride* i][j] = value;
                        //gemmini_state.accumulator[base_sp_addr + gemmini_state.loop_ws_C_stride*i][j] = shifted;
                    }
#ifdef ELEM_T_IS_FLOAT
         	        // printf("%f ", gm->accumulator[base_sp_addr + gm->c_stride* i][j]);
#else
		            //qemu_log("%d ", gm->accumulator[base_sp_addr + gm->c_stride* i][j]);
         	        //qemu_log("accumulator[%zu][%zu] : %d\n", base_sp_addr + gm->c_stride* i,j,gm->accumulator[base_sp_addr + gm->c_stride* i][j]);
#endif
                } else {
                    /* Write to SPAD */
		            // elem_t shifted = gm->mode == OS ?
                    // sys_shift(value, gm->sys_shift) :
                    // sys_shift(value, 0);
		            // elem_t activated = apply_activation_sys(gm, shifted);
                    // gm->spad[base_sp_addr + gm->c_stride * i][j] = activated;

                    /* BIAS BUFFER CALCULATION FOR GEMV_LOOP_WS, UNDER CONSTRUCTION */

                    //qemu_log("value : %d, shifted : %d, activated : %d, accumulate: %d\n",value, shifted, activated,accumulate);
                    const size_t row_addr = base_sp_addr + gm->c_stride * i;
                    const size_t col_addr = j;
                    if(accumulate) {
                        gm->bias_buffer[col_addr] += value;
                    } else {
                        elem_t total = gm->bias_buffer[col_addr] + value;
                        gm->spad[row_addr][col_addr] = total;
                        gm->bias_buffer[col_addr] = 0;
                    }
#ifdef ELEM_T_IS_FLOAT
		        printf("%f ", gm->spad[base_sp_addr + gm->c_stride * i][j]);
#else
		        //printf("%d ", gm->spad[base_sp_addr + gm->c_stride * i][j]);
          	    qemu_log("spad[%zu][%zu] : %d\n",base_sp_addr + gm->c_stride * i,j, gm->spad[base_sp_addr + gm->c_stride * i][j]);
#endif
                }
            }
        }
    }
    gm->preload_rows=gm->preload_cols=0;
}

void compute_gemv(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm) {
    uint32_t a_addr_real = (uint32_t)(a_addr & 0xFFFFFFFF);
    uint32_t bd_addr_real = (uint32_t)(bd_addr & 0xFFFFFFFF);
    
    const uint16_t a_cols = (a_addr >> addr_len) & 0xFFFF;
    // const uint16_t a_rows = (a_addr >> (addr_len + 16)) & 0xFFFF;

    // const uint16_t bd_cols = (bd_addr >> addr_len) & 0xFFFF;
    // const uint16_t bd_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
    qemu_log("GEMMINI: compute_gemv - preload = %d, scratchpad A addr = 0x%08x,"
           "scratchpad B addr 0x%08x\n", preload, a_addr_real, bd_addr_real);
    // Preload
    if (preload) {
	qemu_log("GEMMINI: compute_gemv - PEs after preloading:\n");
        for (size_t i = 0; i < DIM; i++) {
            for (size_t j = 0; j < DIM; j++) {
                if (~gm->preload_sp_addr != 0) {
                    // 현재는 Acc에서 preload는 미지원
                    assert(((gm->preload_sp_addr >> 30) & 0b11) == 0);
                }
                bool preload_transpose = (gm->mode == WS) && gm->b_transpose;
                size_t r = preload_transpose ? j : i;
                size_t c = preload_transpose ? i : j;
                // In OS mode, pe_state stores the accumulator values
                // In WS mode, pe_state stores the persistent weight matrix
                if (i < gm->preload_rows && j < gm->preload_cols) {
                    int8_t preload_value = (~gm->preload_sp_addr == 0)? 0 : gm->spad[gm->preload_sp_addr + r][c];
		            gm->pe_state[i][j] = preload_value;
                } else {
                    gm->pe_state[i][j] = 0;
                }
		        qemu_log("%d ", gm->pe_state[i][j]);
            }
	        qemu_log("\n");
        }
    }
    /* Perform Matrix Multiplication */
    qemu_log("compute_gemv A:\n");
    for( size_t k = 0; k < DIM; ++k ) {
        qemu_log("%d ", gm->spad[a_addr_real][k]);
    }
    qemu_log("\n");
    for (size_t j = 0; j < DIM; ++j) {
        for (size_t k = 0; k < DIM; ++k) {
            elem_t a=0;
            if (~a_addr_real != 0) {
                const size_t c = k;

                a = k < a_cols ? gm->spad[a_addr_real][c]: 0 ;
            }
            /* WS Mode Only */
            if (gm->mode == WS) {
                gm->bias_buffer[j] += a * gm->pe_state[k][j];
            } else {
                qemu_log("GEMMINI: compute_gemv - Performing GEMV in OS mode is not supported yet.\n");
                exit(1);
            }
        }
    }

    qemu_log("GEMMINI: compute - PEs after matmul:\n");
    for (size_t i = 0; i < DIM; ++i) {
	    for (size_t j = 0; j < DIM; ++j) {
            qemu_log("%d ", gm->pe_state[i][j]);
        }
        qemu_log("\n");
    }

    qemu_log("GEMMINI: compute - Results of matmul:\n");
    for (size_t j = 0; j < gm->output_cols; ++j) {
        qemu_log("%d ", gm->bias_buffer[j]);
    }
    qemu_log("\n");

    /* Store Results Back to Spad or Accumulator */
    if (~gm->output_sp_addr != 0) {
        /* output addr is valid (not -1)*/
        bool accumulate = a_addr >> (addr_len + 31) & 0x1;
        
        if (!accumulate) {
            qemu_log("GEMMINI: compute - caching results back to spad\n");
            uint32_t base_sp_addr = gm->output_sp_addr & 0x1FFFFFFF;

            for (size_t col = 0; col < DIM; ++col) {
                /* Get value from bias buffer, scale it down to elem_t and cache it. */
                uint32_t value = gm->bias_buffer[col];
                elem_t shifted = sys_shift(value, 0);
                gm->spad[base_sp_addr][col] = shifted;
                qemu_log("Cached! spad[%d][%ld] : %d\n", base_sp_addr, col, gm->spad[base_sp_addr][col]);
            }
            qemu_log("GEMMINI: compute - Results cached to spad, Clearing bias buffer\n");
            for (size_t col = 0; col < DIM; ++col) {
                /* Clear Bias buffer after moving to spad. */
                gm->bias_buffer[col] = 0;
            }
        }
    }
    gm->preload_rows=gm->preload_cols=0;
}

/* 
 * Compute Function(Matmul) For Ternary Weights. Multiplexed with original compute by mpgemm flag 
 * Doesn't Support Floating points or OS mode for now.
 */
void compute_ternary(uint64_t a_addr, uint64_t bd_addr, bool preload, GemminiState *gm) {
    uint32_t a_addr_real = (uint32_t)(a_addr & 0xFFFFFFFF);
    uint32_t bd_addr_real = (uint32_t)(bd_addr & 0xFFFFFFFF);

    //bool accumulate = a_addr >> (addr_len + 31) & 0x1;
    
    const uint16_t a_cols = (a_addr >> addr_len) & 0xFFFF;
    const uint16_t a_rows = (a_addr >> (addr_len + 16)) & 0xFFFF;

    const uint16_t bd_cols = (bd_addr >> addr_len) & 0xFFFF;
    const uint16_t bd_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
    qemu_log("GEMMINI: compute - preload = %d, scratchpad A addr = 0x%08x,"
           "scratchpad B addr 0x%08x\n", preload, a_addr_real, bd_addr_real);
    // Preload
    if (preload) {
	qemu_log("GEMMINI: compute - PEs after preloading:\n");
        for (size_t i = 0; i < DIM; i++) {
            for (size_t j = 0; j < DIM * 4; j++) {
                if (~gm->preload_sp_addr != 0) {
                    // 현재는 Acc에서 preload는 미지원
                    assert(((gm->preload_sp_addr >> 30) & 0b11) == 0);
                }
                bool preload_transpose = (gm->mode == WS) && gm->b_transpose;
                size_t r = preload_transpose ? j : i;
                size_t c = preload_transpose ? i : j;
                // In OS mode, pe_state stores the accumulator values
                // In WS mode, pe_state stores the persistent weight matrix
                if(j < DIM * 3){
                    if (i < gm->preload_rows && j < gm->preload_cols) {
                        /* Ternary weights can be stored in Ternary PE state */
                        int8_t packed_btye = (~gm->preload_sp_addr == 0)? 0 : gm->spad[gm->preload_sp_addr + r][c/4];
                        uint8_t two_bits = packed_btye >> ((c % 4) *2) & 0b11;
                        int8_t preload_value;
                        //sign Extension for 2-bit to int8_t
                        switch(two_bits) {
                            case 0b11: preload_value = -1; break;
                            case 0b00: preload_value =  0; break;
                            case 0b01: preload_value =  1; break;
                            default:   preload_value =  0; break;
                        }
                        gm->ternary_pe_state[i][j] = preload_value;
                    }else{
                        gm->ternary_pe_state[i][j] = 0;
                    }
                    qemu_log("%d ", gm->ternary_pe_state[i][j]);
                }else{
                    /* We use Ordinary int8 pe state for the rest */
                    if (i < gm->preload_rows && j < gm->preload_cols) {
                        int8_t packed_btye = (~gm->preload_sp_addr == 0)? 0 : gm->spad[gm->preload_sp_addr + r][c/4];
                        uint8_t two_bits = packed_btye >> ((c % 4) *2) & 0b11;
                        int8_t preload_value;
                        //sign Extension for 2-bit to int8_t
                        switch(two_bits) {
                            case 0b11: preload_value = -1; break;
                            case 0b00: preload_value =  0; break;
                            case 0b01: preload_value =  1; break;
                            default:   preload_value =  0; break;
                        }
                        gm->pe_state[i][j - DIM * 3] = preload_value;
                    } else {
                        gm->pe_state[i][j - DIM * 3] = 0;
                    }     
                    qemu_log("%d ", gm->pe_state[i][j - DIM * 3]);
                }   
            }
	        qemu_log("\n");
        }
    }

    /* Make a Buffer for Results of single multiplication */
    /* For Ternary GEMM, we have a result 4 times(in terms of col) larger */
    acc_t results[DIM][DIM * 4] = {0};
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            if (i < bd_rows && j < bd_cols) {
                results[i][j] = (~bd_addr_real == 0) ? 0 : gm->spad[bd_addr_real + i][j];
            } else {
                results[i][j] = 0;
            }
        }
    }
    /* Perform Matrix Multiplication in Ternary PE */
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM * 3; ++j) {
            for (size_t k = 0; k < DIM; ++k) {
                elem_t a=0;
                if (~a_addr_real != 0) {
                    const size_t r = gm->a_stride * (gm->a_transpose ? k : i);
                    const size_t c = gm->a_transpose ? i : k;

                    a = i < a_rows && k < a_cols ? gm->spad[a_addr_real + r][c]: 0 ;
                }
                /* WS Mode, Most Operations are Done in WS Mode */
                if (gm->mode == WS) {
                    results[i][j] += a * gm->ternary_pe_state[k][j];
                } else {
                    qemu_log("Error: Ternary PE only supports WS mode currently.\n");
                    exit(1);
                }
            }
        }
    }

    /* Perform Matrix Multiplication in Original PE*/
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            for (size_t k = 0; k < DIM; ++k) {
                elem_t a=0;
                if (~a_addr_real != 0) {
                    const size_t r = gm->a_stride * (gm->a_transpose ? k : i);
                    const size_t c = gm->a_transpose ? i : k;

                    a = i < a_rows && k < a_cols ? gm->spad[a_addr_real + r][c]: 0 ;
                }
                /* WS Mode, Most Operations are Done in WS Mode */
                if (gm->mode == WS) {
                    results[i][j + DIM * 3] += a * gm->pe_state[k][j];
                } else {
                    /* OS Mode */
                    qemu_log("Error: Ternary PE only supports WS mode currently.\n");
                    exit(1);
                }
            }
        }
    }

    qemu_log("GEMMINI: compute - Ternary PEs after matmul:\n");
    for (size_t i = 0; i < DIM; ++i) {
	    for (size_t j = 0; j < DIM * 3; ++j) {
            qemu_log("%d ", gm->ternary_pe_state[i][j]);
        }
        qemu_log("\n");
    }
    qemu_log("GEMMINI: compute - Ordinary PEs after matmul:\n");
    for (size_t i = 0; i < DIM; ++i) {
	    for (size_t j = 0; j < DIM; ++j) {
            qemu_log("%d ", gm->pe_state[i][j]);
        }
        qemu_log("\n");
    }
    qemu_log("GEMMINI: compute - Results of matmul:\n");
    for (size_t i = 0; i < gm->output_rows; ++i) {
        for (size_t j = 0; j < gm->output_cols; ++j) {
            qemu_log("%d ", results[i][j]);
        }
        qemu_log("\n");
    }

    /* Store Results Back to Spad or Accumulator */
    if (~gm->output_sp_addr != 0) {
        /* output addr is valid (not -1)*/
        bool acc = (gm->output_sp_addr >> 31) & 0x1;
        bool acc_accum = (gm->output_sp_addr >> 30) & 0x1;

        qemu_log("GEMMINI: compute - storing results to %s %s\n",
               acc ? "accumulator" : "scratchpad",
               acc_accum ? "with accumulation" : "without accumulation");
        
        uint32_t base_sp_addr = gm->output_sp_addr & 0x1FFFFFFF;

        for (size_t i = 0; i < gm->output_rows; ++i) {
            /* Output cols is 4 * DIM for Ternary GEMM (If ignoring Paddings) */
            for (size_t j = 0; j < gm->output_cols; ++j) {
                acc_t value = results[i][j];
                /* Dest addr by blocking Addr space by 16. maybe not needed */
                // uint64_t dest_row = base_sp_addr + gm->c_stride * (( i * 4 ) + ( j / DIM ));
                // uint64_t dest_col = j % DIM;
                // uint64_t dest_row = base_sp_addr + gm->c_stride * i ;
                // uint64_t dest_col = j;
                uint64_t block_idx = j / DIM;  // which DIM-sized block (0-3)
                uint64_t dest_row = base_sp_addr + gm->c_stride * i + block_idx * DIM;
                uint64_t dest_col = j % DIM;
                if (acc) {
		            if (acc_accum) {
                        qemu_log("accumulator[%zu][%zu] : %d = %d + %d\n", 
                                dest_row,dest_col,
                                gm->accumulator[dest_row][dest_col] + value,
                                gm->accumulator[dest_row][dest_col],
                                value);
			            gm->accumulator[dest_row][dest_col] += value;
                    } else {
                        qemu_log("accumulator[%zu][%zu] : %d = %d <- %d\n",
                                dest_row,dest_col,
                                value,
                                gm->accumulator[dest_row][dest_col],
                                value);
			            gm->accumulator[dest_row][dest_col] = value;
                        //gemmini_state.accumulator[base_sp_addr + gemmini_state.loop_ws_C_stride*i][j] = shifted;
                    }
		            //qemu_log("%d ", gm->accumulator[base_sp_addr + gm->c_stride* i][j]);
         	        //qemu_log("accumulator[%zu][%zu] : %d\n", base_sp_addr + gm->c_stride* i,j,gm->accumulator[base_sp_addr + gm->c_stride* i][j]);
                } else {
                /* Write to SPAD */
		            elem_t shifted = gm->mode == OS ?
                        sys_shift(value, gm->sys_shift) :
                        sys_shift(value, 0);
		            elem_t activated = apply_activation_sys(gm, shifted);
                    gm->spad[base_sp_addr + gm->c_stride * i][j] = activated;

                    /* BIAS BUFFER CALCULATION FOR GEMV_LOOP_WS, UNDER CONSTRUCTION */

                    // const size_t row_addr = base_sp_addr + gm->c_stride * i;
                    // const size_t col_addr = j;
                    //         if(accumulate) {
                    //             gm->bias_buffer[0][col_addr] += activated;
                    // }
                    // else {
                    //             elem_t total = gm->bias_buffer[0][col_addr] + activated;
                    // gm->spad[row_addr][col_addr] = total;
                    // gm->bias_buffer[0][col_addr] = 0;
                    // }
		        //printf("%d ", gm->spad[base_sp_addr + gm->c_stride * i][j]);
          	    qemu_log("spad[%zu][%zu] : %d\n",base_sp_addr + gm->c_stride * i,j, gm->spad[base_sp_addr + gm->c_stride * i][j]);
                }
	            //gm->pe_state[i][j]=0;	
            }
        }
    }
    gm->preload_rows=gm->preload_cols=0;
}

/* Move Elements from SPAD or Accumulator to DRAM(Or Possibly SPAD for caching) */
void mvout(uint64_t dram_addr, uint64_t sp_addr, GemminiState *gm)
{
    bool accumulator = (sp_addr >> 31) & 0x1;
    //bool to_dram   = (sp_addr >> 30) & 0x1; //if 1, to DRAM; if 0, to SPAD
    bool full        = (sp_addr >> 29) & 0x1;
    NormCmd norm_cmd = (sp_addr >> 26) & 0x7;

    uint64_t base_row_addr = (sp_addr & 0x3FFFFFF) % BANK_ROWS;
    uint64_t cols = (sp_addr >> ADDR_LEN) & 0xFFFF;
    uint64_t rows = (sp_addr >> (ADDR_LEN + 16)) & 0xFFFF;

    const int block_stride = DIM;

    /* Row Buffer for elements of row to be written to DRAM */
    size_t elem_size = full ? sizeof(acc_t) : sizeof(elem_t);
    size_t row_bytes = cols * elem_size;
    uint8_t *row_buf = g_malloc(row_bytes);

    for (size_t row = 0; row < rows; ++row) {

        uint64_t dram_row_addr = dram_addr + row * gm->store_stride;
        bool should_write = true;

        /* APPLY NORM */
        for (size_t col = 0; col < cols; col += DIM) {
            size_t block = col / DIM;
            size_t spad_row = base_row_addr + block * block_stride + row;
            size_t len = (cols - col > DIM) ? DIM : (cols - col);

            bool is_last = (col + DIM >= cols);
            NormCmd n_cmd = is_last ?
            norm_cmd : non_terminating_norm_cmd(norm_cmd);

            should_write = apply_norm(gm, &gm->accumulator[spad_row][0], len, n_cmd);
            qemu_log("MVOUT: Applied NORM for row %lu/%lu, col %lu/%lu\n", row, rows, col, cols);
        }

        if (!should_write) {
            qemu_log("MVOUT: skipping write for row %lu/%lu, cols %lu\n", row, rows, cols);
            continue;
        }

        /* Store Intermediate results to buffer */
        qemu_log("MVOUT: writing for row %lu/%lu, cols %lu\n", row, rows, cols);
        for (size_t col = 0; col < cols; ++col) {
            size_t block = col / DIM;
            size_t spad_col = col % DIM;
            size_t spad_row = base_row_addr + row + block * block_stride;

            if (accumulator) {
                acc_t acc_value = gm->accumulator[spad_row][spad_col];
                qemu_log("MVOUT: mvout from acc[%lu][%lu] = %d, activation = %d\n", spad_row, spad_col, acc_value, gm->acc_act);
                acc_value = apply_pre_activation_acc(gm, acc_value);
                qemu_log("MVOUT: after pre-activation = %d\n", acc_value);
                acc_t shifted = acc_scale(acc_value, gm->acc_shift);
                qemu_log("MVOUT: after shifting = %d\n", shifted);
                elem_t activated = apply_activation_acc(gm, shifted);
                qemu_log("MVOUT: after activation = %d\n", activated);

                if (full) {
                    memcpy(row_buf + col * sizeof(acc_t),
                           &acc_value,
                           sizeof(acc_t));
                } else {
                    memcpy(row_buf + col * sizeof(elem_t),
                           &activated,
                           sizeof(elem_t));
                }
            } else {
                elem_t value = gm->spad[spad_row][spad_col];
                qemu_log("MVOUT: mvout from spad[%lu][%lu] = %d\n", spad_row, spad_col, value);
                memcpy(row_buf + col * sizeof(elem_t),
                       &value,
                       sizeof(elem_t));
            }
        }
        /* WRITE */
        for (uint32_t i = 0; i < row_bytes; i++) {
            qemu_log("[MVOUT] write to dram addr 0x%lx: %02x\n", dram_row_addr + i, row_buf[i]);
            /* Callback Function to helper for Memory Access */
            gm->mem_write(gm->mem_opaque, dram_row_addr + i, row_buf[i], 1);
            gm->total_writes_in_bytes++;
        }
    }
    g_free(row_buf);
}

/* Conventional Loop_ws for Matrix multiplication + Biasing + Activation of large Dimension */
void loop_ws(GemminiState *gm, uint64_t rs1, uint64_t rs2) {
    const bool ex_accumulate = rs1 & 1;
    const bool full_C = (rs1 >> 1) & 1;
    const bool low_D = (rs1 >> 2) & 1;
    const uint8_t act = (rs1 >> 8) & 0x7;
    /*Temporaly added activation state update*/
    gm->acc_act = act;
    const bool a_transpose = rs2 & 1;
    const bool b_transpose = (rs2 >> 1) & 1;
    /* We Don't make use of IDs for Conventional loop_ws now */
    // uint8_t a_spad_id = (rs1 >> 18) & 0b11;
    // uint8_t b_spad_id = (rs1 >> 16) & 0b11;
    const bool is_resadd = (rs2 >> 2) & 1;

    const uint16_t I = gm->loop_ws_I;
    const uint16_t J = gm->loop_ws_J;
    const uint16_t K = gm->loop_ws_K;

    const uint16_t pad_I = gm->loop_ws_pad_I;
    const uint16_t pad_J = gm->loop_ws_pad_J;
    const uint16_t pad_K = gm->loop_ws_pad_K;
    

    const uint32_t GARBAGE_ADDR = ~0;

    const int total_spad_rows = (I * K + K * J) * DIM;
    const int total_acc_rows = (I * J) * DIM;
    if (total_spad_rows > BANK_NUM * BANK_ROWS / 2 || total_acc_rows > ACC_ROWS / 2){
        printf("LOOP_WS bounds were too large for double-buffering\n");
        exit(1);
    }
    uint32_t A_sp_addr_start = 0;
    uint32_t B_sp_addr_start = (BANK_NUM * BANK_ROWS / 2) - K * J * DIM;
    uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
    uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3));

    /* resadd - just add A and B and mvout to output addr */
    if(is_resadd) {
        A_sp_addr_start = 1 << (ADDR_LEN - 1);
        B_sp_addr_start = 3 << (ADDR_LEN - 2);
        for(uint16_t i = 0; i < I; i++){
            for(uint16_t j = 0; j < J; j++){
                const uint32_t A_sp_addr = A_sp_addr_start + (i*J+j)*DIM;
                const uint32_t B_sp_addr = B_sp_addr_start + (i*J+j)*DIM;
                const uint32_t C_sp_addr = C_sp_addr_start + (i*J+j)*DIM;
                uint64_t dram_addr, cols, rows;
                dram_addr = gm->loop_ws_A + (i*gm->loop_ws_A_stride + j) * DIM * sizeof(elem_t);
                cols = DIM - (j == J-1 ? pad_J : 0);
                rows = DIM - (i == I-1 ? pad_I : 0);
                mvin(dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0, gm);

                dram_addr = gm->loop_ws_B + (i*gm->loop_ws_B_stride + j) * DIM * sizeof(elem_t);
                cols = DIM - (j == J-1 ? pad_J : 0);
                rows = DIM - (i == I-1 ? pad_I : 0);
                mvin(dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1, gm);
                if(gm->loop_ws_C != 0){
                const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);
                const uint64_t C_dram_addr = gm->loop_ws_C +
                    (i*gm->loop_ws_C_stride + j) * DIM * sizeof_C;
                const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                mvout(C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr, gm);
                }
            }
        }
        return;
    }

    /* Checking if Bias exists and load it to accumulator */
    if (gm->loop_ws_D != 0) {
        for (uint16_t i = 0; i < I; i++) {
            for (uint16_t j = 0; j < J; j++) {
                const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);

                const uint64_t dram_addr = gm->loop_ws_D + (i * gm->loop_ws_D_stride + j) * DIM * sizeof_D;
                const uint64_t sp_addr = D_sp_addr_start + (i*J + j)*DIM;
                const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
                const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);
                qemu_log("MVIN D: dram_addr=0x%lu, sp_addr=0x%lu, rows=%lu, cols=%lu\n", dram_addr, sp_addr, rows, cols);
                mvin(dram_addr, (rows << 48) | (cols << 32) | sp_addr, 2, gm);
            }
        }
    }
    for (uint16_t k = 0; k < K; k++) {
        for (uint16_t j = 0; j < J; j++) {
            for (uint16_t i = 0; i < I; i++) {
                    const uint32_t A_sp_addr = a_transpose ? (A_sp_addr_start + (k*I + i)*DIM) :(A_sp_addr_start + (i*K + k)*DIM);
                    const uint32_t B_sp_addr = b_transpose ? (B_sp_addr_start + (j*K + k)*DIM) :(B_sp_addr_start + (k*J + j)*DIM);
                    const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j)*DIM;
                    if (j == 0) {
                        /* MVIN A */
                        uint64_t dram_addr, cols, rows;
                        if (a_transpose) {
                                dram_addr = gm->loop_ws_A + (k*gm->loop_ws_A_stride + i) * DIM * sizeof(elem_t);
                                cols = DIM - (i == I-1 ? pad_I : 0);
                                rows = DIM - (k == K-1 ? pad_K : 0);
                        } else {
                                dram_addr = gm->loop_ws_A +(i*gm->loop_ws_A_stride + k) * DIM * sizeof(elem_t);
                                cols = DIM - (k == K-1 ? pad_K : 0);
                                rows = DIM - (i == I-1 ? pad_I : 0);
                        }
                        mvin(dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0, gm);
                    } if (i == 0) {
                        /* MVIN B */
                        int64_t dram_addr, cols, rows;
                        if (b_transpose) {
                            dram_addr = gm->loop_ws_B + (j*gm->loop_ws_B_stride + k) * DIM * sizeof(elem_t);
                            cols = DIM - (k == K-1 ? pad_K : 0);
                            rows = DIM - (j == J-1 ? pad_J : 0);
                        } else {
                            dram_addr = gm->loop_ws_B + (k*gm->loop_ws_B_stride + j) * DIM * sizeof(elem_t);
                            cols = DIM - (j == J-1 ? pad_J : 0);
                            rows = DIM - (k == K-1 ? pad_K : 0);
                        }
                        mvin(dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1, gm);
                    }
                    if(!is_resadd) {
                        /* COMPUTE */
                        uint32_t pre_sp_addr = i == 0 ? B_sp_addr : GARBAGE_ADDR;
                        uint32_t out_sp_addr = C_sp_addr;
                        /* Added Condition For avoiding overwriting on bias */
                        if (!ex_accumulate && k == 0 && gm->loop_ws_D == 0) {
                                out_sp_addr &= ~(1 << (ADDR_LEN-2));       
                        }
                        const uint64_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
                        const uint64_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
                        const uint64_t B_cols = DIM - (j == J - 1 ? pad_J : 0);
                        const uint64_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
                        const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                        const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                        preload((B_rows << 48) | (B_cols << 32) | pre_sp_addr, (C_rows << 48) | (C_cols << 32) | out_sp_addr, gm);
                        qemu_log("Computing for i=%d/%d, j=%d/%d, k=%d/%d\n", i, I, j, J, k, K);
                        compute((A_rows << 48) | (A_cols << 32) | A_sp_addr, ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0, gm);
                    }
                    if (gm->loop_ws_C != 0 && k == K-1) {
                        /* C Spad address is Valid and Computation is Done */
                        const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);

                        /* Activation Process Before MVOUT */
                        if ((act != LAYERNORM) && (act != SOFTMAX)) {
                            /* Not much to Worry, just take them out Block by Block */
                            const uint64_t C_dram_addr = gm->loop_ws_C +
                            (i*gm->loop_ws_C_stride + j) * DIM * sizeof_C;

                            const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                            const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                            mvout(C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr, gm);
                        } else if (((act == LAYERNORM) || (act == SOFTMAX)) && j == J - 1){
                            /* Softmax and LAYERNORM needs to look through whole row so Additional Norm across row is needed */
                            int norm_cmds_size;
                            uint32_t (*norm_cmds)[2];
                            /* Norm Cmds needed for Softmax and LayerNorm in each step */
                            uint32_t ln_norm_cmds[][2] = {{1,2},{3,4},{0,0}};
                            uint32_t sm_norm_cmds[][2] = {{5,5},{6,7},{0,0}};

                            if (act == LAYERNORM) {
                                norm_cmds = ln_norm_cmds;
                                norm_cmds_size = 3;
                            } else {
                                /* Softmax */
                                norm_cmds = sm_norm_cmds;
                                norm_cmds_size = 3;
                            }

                            const size_t rows = DIM - (i == I-1 ? pad_I : 0);

                            for (size_t row = 0; row < rows; row += NORM_STAT_IDS) {
                                const size_t stat_ids = rows - row > NORM_STAT_IDS ?
                                    NORM_STAT_IDS : rows - row;

                            for (int cmd = 0; cmd < norm_cmds_size; cmd++) {
                                for (size_t stat_id = 0; stat_id < stat_ids; stat_id++) {
                                    gemmini_config((1 << 17) | (stat_id << 8) | 3, 0, gm);
                                    const size_t r = row + stat_id;
                                
                                    for (size_t jj = 0; jj < J; jj++) {
                                        uint32_t norm_C_sp_addr = C_sp_addr_start + (i*J + jj)*DIM + r;
                                        if (jj + 1 == J) {
                                            norm_C_sp_addr |= (norm_cmds[cmd][1] << 26); // Final mean/inv-std-dev calculation
                                        } else {
                                            norm_C_sp_addr |= (norm_cmds[cmd][0] << 26); // Accumulate sum/variance
                                        }
                                        const uint64_t C_dram_addr = gm->loop_ws_C +
                                        (i*gm->loop_ws_C_stride + jj) * DIM * sizeof_C +
                                        r * gm->loop_ws_C_stride * sizeof_C;
                                        const size_t cols = DIM - (jj + 1 == J ? pad_J : 0);
                                        /* MVOUT will be returned without writing(Contributing to norm) if not last row */
                                        mvout(C_dram_addr, ((uint64_t)1 << 48) | (cols << 32) | norm_C_sp_addr, gm);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void ternary_loop_ws(GemminiState *gm, uint64_t rs1, uint64_t rs2) {
    const bool ex_accumulate = rs1 & 1;
    const bool full_C = (rs1 >> 1) & 1;
    const bool low_D = (rs1 >> 2) & 1;
    const uint8_t act = (rs1 >> 8) & 0x7;
    /*Temporaly added activation state update*/
    gm->acc_act = act;
    const bool a_transpose = rs2 & 1;
    const bool b_transpose = (rs2 >> 1) & 1;
    /* We Don't make use of IDs for Conventional loop_ws now */
    // uint8_t a_spad_id = (rs1 >> 18) & 0b11;
    // uint8_t b_spad_id = (rs1 >> 16) & 0b11;
    const bool is_resadd = (rs2 >> 2) & 1;

    const uint16_t I = gm->loop_ws_I;
    const uint16_t J = gm->loop_ws_J;
    const uint16_t K = gm->loop_ws_K;
    const uint16_t J_blocks = (J + 3) / 4;

    const uint16_t pad_I = gm->loop_ws_pad_I;
    const uint16_t pad_J = gm->loop_ws_pad_J;
    const uint16_t pad_K = gm->loop_ws_pad_K;

    const uint32_t GARBAGE_ADDR = ~0;

    const int total_spad_rows = (I * K + K * J) * DIM;
    const int total_acc_rows = (I * J) * DIM;
    if (total_spad_rows > BANK_NUM * BANK_ROWS / 2 || total_acc_rows > ACC_ROWS / 2){
        printf("LOOP_WS bounds were too large for double-buffering\n");
        exit(1);
    }
    uint32_t A_sp_addr_start = 0;
    uint32_t B_sp_addr_start = (BANK_NUM * BANK_ROWS / 2) - K * J * DIM;
    uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
    uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3));

    /* Ternary gemm does not support resadd */
    if(is_resadd) {
        qemu_log("Ternary GEMM does not support resadd operation currently.\n");
        exit(1);
    }
    /* Checking if Bias exists and load it to accumulator */
    if (gm->loop_ws_D != 0) {
        for (uint16_t i = 0; i < I; i++) {
            for (uint16_t j = 0; j < J; j++) {
                const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);

                const uint64_t dram_addr = gm->loop_ws_D + (i * gm->loop_ws_D_stride + j) * DIM * sizeof_D;
                const uint64_t sp_addr = D_sp_addr_start + (i*J + j)*DIM;
                const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
                const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);
                qemu_log("MVIN D: dram_addr=0x%lu, sp_addr=0x%lu, rows=%lu, cols=%lu\n", dram_addr, sp_addr, rows, cols);
                mvin(dram_addr, (rows << 48) | (cols << 32) | sp_addr, 2, gm);
            }
        }
    }
    for (uint16_t k = 0; k < K; k++) {
        /* We process four columns per iteration */
        for (uint16_t j = 0; j < J_blocks; j++) {
            for (uint16_t i = 0; i < I; i++) {
                    const uint16_t j_base = j * 4;
                    const uint16_t tiles_in_block = (J - j_base > 4) ? 4 : (J - j_base);
                    const uint16_t block_cols = tiles_in_block * DIM - (j == J_blocks - 1 ? pad_J : 0);
                    const uint16_t block_packed_cols = (block_cols + 3) / 4;
                    const uint32_t A_sp_addr = a_transpose ? (A_sp_addr_start + (k*I + i)*DIM) :(A_sp_addr_start + (i*K + k)*DIM);
                    const uint32_t B_sp_addr = b_transpose ? (B_sp_addr_start + (j*K + k)*DIM) :(B_sp_addr_start + (k*J_blocks + j)*DIM);
                    const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j_base)*DIM;
                    if (j == 0) {
                        /* MVIN A */
                        uint64_t dram_addr, cols, rows;
                        if (a_transpose) {
                                dram_addr = gm->loop_ws_A + (k*gm->loop_ws_A_stride + i) * DIM * sizeof(elem_t);
                                cols = DIM - (i == I-1 ? pad_I : 0);
                                rows = DIM - (k == K-1 ? pad_K : 0);
                        } else {
                                dram_addr = gm->loop_ws_A +(i*gm->loop_ws_A_stride + k) * DIM * sizeof(elem_t);
                                cols = DIM - (k == K-1 ? pad_K : 0);
                                rows = DIM - (i == I-1 ? pad_I : 0);
                        }
                        mvin(dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0, gm);
                    } if (i == 0) {
                        /* MVIN B */
                        int64_t dram_addr, cols, rows;
                        if (b_transpose) {
                            dram_addr = gm->loop_ws_B + (j*gm->loop_ws_B_stride/4 + k) * DIM * sizeof(elem_t);
                            // cols = DIM - (k == K-1 ? pad_K : 0);
                            // rows = DIM - (j == J-1 ? pad_J : 0);
                            cols = block_packed_cols;
                            rows = DIM - (k == K-1 ? pad_K : 0);
                        } else {
                            dram_addr = gm->loop_ws_B + (k*gm->loop_ws_B_stride/4 + j) * DIM * sizeof(elem_t);
                            cols = block_packed_cols;
                            rows = DIM - (k == K-1 ? pad_K : 0);
                        }
                        mvin(dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1, gm);
                    }
                    if(!is_resadd) {
                        /* COMPUTE */
                        uint32_t pre_sp_addr = i == 0 ? B_sp_addr : GARBAGE_ADDR;
                        uint32_t out_sp_addr = C_sp_addr;
                        /* Added Condition For avoiding overwriting on bias */
                        if (!ex_accumulate && k == 0 && gm->loop_ws_D == 0) {
                                out_sp_addr &= ~(1 << (ADDR_LEN-2));       
                        }
                        const uint64_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
                        const uint64_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
                        /* Ternary GEMM has x4 throughput than Ordinary GEMM */
                        const uint64_t B_cols = block_cols;
                        const uint64_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
                        /* Ternary GEMM has x4 throughput than Ordinary GEMM */
                        const uint64_t C_cols = block_cols;
                        const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                        preload((B_rows << 48) | (B_cols << 32) | pre_sp_addr, (C_rows << 48) | (C_cols << 32) | out_sp_addr, gm);
                        compute_ternary((A_rows << 48) | (A_cols << 32) | A_sp_addr, ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0, gm);
                    }
                    if (gm->loop_ws_C != 0 && k == K-1) {
                        /* C Spad address is Valid and Computation is Done */
                        const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);

                        /* Activation Process Before MVOUT */
                        if ((act != LAYERNORM) && (act != SOFTMAX)) {
                            /* Not much to Worry, just take them out Block by Block */
                            const uint64_t C_dram_addr = gm->loop_ws_C +
                            (i*gm->loop_ws_C_stride + j_base) * DIM * sizeof_C;

                            const uint64_t C_cols = block_cols;
                            const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                            mvout(C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr, gm);
                        } else if (((act == LAYERNORM) || (act == SOFTMAX)) && j == J_blocks - 1){
                            /* Softmax and LAYERNORM needs to look through whole row so Additional Norm across row is needed */
                            int norm_cmds_size;
                            uint32_t (*norm_cmds)[2];
                            /* Norm Cmds needed for Softmax and LayerNorm in each step */
                            uint32_t ln_norm_cmds[][2] = {{1,2},{3,4},{0,0}};
                            uint32_t sm_norm_cmds[][2] = {{5,5},{6,7},{0,0}};

                            if (act == LAYERNORM) {
                                norm_cmds = ln_norm_cmds;
                                norm_cmds_size = 3;
                            } else {
                                /* Softmax */
                                norm_cmds = sm_norm_cmds;
                                norm_cmds_size = 3;
                            }

                            const size_t rows = DIM - (i == I-1 ? pad_I : 0);

                            for (size_t row = 0; row < rows; row += NORM_STAT_IDS) {
                                const size_t stat_ids = rows - row > NORM_STAT_IDS ?
                                    NORM_STAT_IDS : rows - row;

                            for (int cmd = 0; cmd < norm_cmds_size; cmd++) {
                                for (size_t stat_id = 0; stat_id < stat_ids; stat_id++) {
                                    gemmini_config((1 << 17) | (stat_id << 8) | 3, 0, gm);
                                    const size_t r = row + stat_id;
                                
                                    for (size_t jj = 0; jj < J; jj++) {
                                        uint32_t norm_C_sp_addr = C_sp_addr_start + (i*J + jj)*DIM + r;
                                        if (jj + 1 == J) {
                                            norm_C_sp_addr |= (norm_cmds[cmd][1] << 26); // Final mean/inv-std-dev calculation
                                        } else {
                                            norm_C_sp_addr |= (norm_cmds[cmd][0] << 26); // Accumulate sum/variance
                                        }
                                        const uint64_t C_dram_addr = gm->loop_ws_C +
                                        (i*gm->loop_ws_C_stride + jj) * DIM * sizeof_C +
                                        r * gm->loop_ws_C_stride * sizeof_C;
                                        const size_t cols = 4 * DIM - (jj + 1 == J ? pad_J : 0);
                                        /* MVOUT will be returned without writing(Contributing to norm) if not last row */
                                        mvout(C_dram_addr, ((uint64_t)1 << 48) | (cols << 32) | norm_C_sp_addr, gm);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void k_gemv_loop_ws(uint64_t rs1, uint64_t rs2, GemminiState *gm) {
    const bool ex_accumulate = rs1 & 1;
    const bool full_C = (rs1 >> 1) & 1;
    const bool low_D = (rs1 >> 2) & 1;
    const uint8_t act = (rs1 >> 8) & 0x7;
    /*Temporaly added activation state update*/
    gm->acc_act = act;
    const bool a_transpose = rs2 & 1;
    const bool b_transpose = (rs2 >> 1) & 1;
    uint8_t a_spad_id = (rs1 >> 22) & 0b111;
    uint8_t b_spad_id = (rs1 >> 19) & 0b111;
    uint8_t c_spad_id = (rs1 >> 16) & 0b111;
    const bool is_resadd = (rs2 >> 2) & 1;
    qemu_log("ex_accumulate=%d, full_C=%d, low_D=%d, act=0x%02x, a_transpose=%d, b_transpose=%d, a_spad_id=0x%02x, b_spad_id=0x%02x, c_spad_id=0x%02x, is_resadd=%d\n",
            ex_accumulate, full_C, low_D, act, a_transpose, b_transpose, a_spad_id, b_spad_id, c_spad_id, is_resadd);

    const uint16_t I = gm->gemv_loop_ws_I;
    const uint16_t J = gm->gemv_loop_ws_J;
    const uint16_t K = gm->gemv_loop_ws_K;

    const uint16_t pad_I = gm->gemv_loop_ws_pad_I;
    const uint16_t pad_J = gm->gemv_loop_ws_pad_J;
    const uint16_t pad_K = gm->gemv_loop_ws_pad_K;

    const uint32_t GARBAGE_ADDR = ~0;

    const int total_spad_rows = (I * K + K * J) * DIM;
    const int total_acc_rows = (I * J) * DIM;
    //TODO : It's not true
    if (total_spad_rows > BANK_NUM * BANK_ROWS / 2 || total_acc_rows > ACC_ROWS / 2) {
        printf("LOOP_WS bounds were too large for double-buffering\n");
        exit(1);
    }

    /* Straightforward address assignment for Caching (May waste some memory space tho) */
    uint32_t A_sp_addr_start = BANK_ROWS * 0;
    uint32_t B_sp_addr_start = BANK_ROWS * 1;
    /* Set C for acc = 1, acc_accum = 1, lower bits of C is 0 to match with accumulator addr */
    uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3)) | BANK_ROWS * 3; 
    /* D is always mvin to accumulator (Only MSB is 1)*/
    uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
    uint32_t bias_sp_addr_start = BANK_ROWS * 3;

    /* UNDER CONSTRUCTION - NEED MORE SPECIFICITY OF CACHING */
    if(a_spad_id != 0) A_sp_addr_start = (a_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;
    if(b_spad_id != 0) B_sp_addr_start = (b_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;
    if(c_spad_id != 0) C_sp_addr_start = (c_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;

    /* resadd - just add A and B and mvout to output addr */
    if(is_resadd) {
        A_sp_addr_start = 1 << (ADDR_LEN - 1);
        B_sp_addr_start = 3 << (ADDR_LEN - 2);
        for(uint16_t i = 0; i < I; i++){
            for(uint16_t j = 0; j < J; j++){
                const uint32_t A_sp_addr = A_sp_addr_start + (i*J+j)*DIM;
                const uint32_t B_sp_addr = B_sp_addr_start + (i*J+j)*DIM;
                const uint32_t C_sp_addr = C_sp_addr_start + (i*J+j)*DIM;
                uint64_t dram_addr, cols, rows;
                dram_addr = gm->gemv_loop_ws_A + (i*gm->gemv_loop_ws_A_stride + j) * DIM * sizeof(elem_t);
                cols = DIM - (j == J-1 ? pad_J : 0);
                rows = DIM - (i == I-1 ? pad_I : 0);
                mvin(dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0, gm);

                dram_addr = gm->gemv_loop_ws_B + (i*gm->gemv_loop_ws_B_stride + j) * DIM * sizeof(elem_t);
                cols = DIM - (j == J-1 ? pad_J : 0);
                rows = DIM - (i == I-1 ? pad_I : 0);
                mvin(dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1, gm);
                if(gm->gemv_loop_ws_C != 0){
                const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);
                const uint64_t C_dram_addr = gm->gemv_loop_ws_C +
                    (i*gm->gemv_loop_ws_C_stride + j) * DIM * sizeof_C;
                const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                mvout(C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr, gm);
                }
            }
        }
        return;
    }

    /* Check if bias exists and load it to accumulator */
    if (gm->gemv_loop_ws_D != 0) {
        for (uint16_t i = 0; i < I; i++) {
            for (uint16_t j = 0; j < J; j++) {
                if(c_spad_id == 0){
                    /* no caching, preload bias to accumulator */
                    const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);
                    const uint64_t dram_addr = gm->gemv_loop_ws_D +
                        (i * gm->gemv_loop_ws_D_stride + j) * DIM * sizeof_D;
                    const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
                    const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);
                    const uint64_t bias_sp_addr = D_sp_addr_start + (i*J + j)*DIM;
                    mvin(dram_addr, (rows << 48) | (cols << 32) | bias_sp_addr, 2, gm);
                } else {
                    /* caching, preload bias to spad, for future load on bias buffer */
                    const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);
                    const uint64_t dram_addr = gm->gemv_loop_ws_D +
                        (i * gm->gemv_loop_ws_D_stride + j) * DIM * sizeof_D;
                    const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
                    const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);
                    const uint64_t bias_sp_addr = bias_sp_addr_start + (i*J + j)*DIM;
                    mvin(dram_addr, (rows << 48) | (cols << 32) | bias_sp_addr, 2, gm);           
                }
    //             size_t elem_size = accumulator && !load_shrunk ? sizeof(acc_t) : sizeof(elem_t);
    //             size_t row_bytes = cols * elem_size;
            }
        }
    }
    //k> j> i 순서로 순회
    for (uint16_t i = 0; i < I; i++) {
        for (uint16_t j = 0; j < J; j++) {
            for (uint16_t k = 0; k < K; k++) {
                const uint32_t A_sp_addr = a_transpose ? (A_sp_addr_start + (k*I + i)*DIM) : (A_sp_addr_start + (i*K + k)*DIM);
                const uint32_t B_sp_addr = b_transpose ? (B_sp_addr_start + ((k*J + j)*DIM) % BANK_ROWS) : (B_sp_addr_start + ((j*K + k)*DIM) % BANK_ROWS);
                const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j)*DIM;
                /* MVIN A */
                if (j == 0 && gm->gemv_loop_ws_A != 0) {
                    uint64_t dram_addr, cols, rows;
                    if (a_transpose) {
                        dram_addr = gm->gemv_loop_ws_A +
                            (k*gm->gemv_loop_ws_A_stride + i) * DIM * sizeof(elem_t);
                        cols = DIM - (i == I-1 ? pad_I : 0);
                        rows = DIM - (k == K-1 ? pad_K : 0);
                    } else {
                        dram_addr = gm->gemv_loop_ws_A +
                            (i*gm->gemv_loop_ws_A_stride + k) * DIM * sizeof(elem_t);
                        cols = DIM - (k == K-1 ? pad_K : 0);
                        rows = DIM - (i == I-1 ? pad_I : 0);
                    }
                    mvin(dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0, gm);
                }
                /* MVIN B */
                if (i == 0 && gm->gemv_loop_ws_B != 0) {
                    uint64_t dram_addr, cols, rows;
                    /*
                     * dram_addr 계산하는 방식 유의!
                     * (k와 j 바뀌어 있음-> 미리 k번째 블록이 가로에 위치하도록 정렬해둘 것이기 때문)
                     * pad 값 상관없이 다 가져옴, execute에서 pad 고려
                     */
                     if (b_transpose) {
                        dram_addr = gm->gemv_loop_ws_B +
                            (k*gm->gemv_loop_ws_B_stride + j) * DIM * sizeof(elem_t);
                        cols = DIM;
                        rows = DIM;
                    } else {
                        dram_addr = gm->gemv_loop_ws_B +
                            (j*gm->gemv_loop_ws_B_stride + k) * DIM * sizeof(elem_t);
                        cols = DIM;
                        rows = DIM;
                    }
                    mvin(dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1, gm);
                }
                if (k == 0 && c_spad_id != 0) {
                    if(gm->gemv_loop_ws_D != 0){
                        /* Load bias from spad to the accumulator's bias buffer */
                        const uint64_t bias_sp_addr = bias_sp_addr_start + (i*J + j) * DIM;
                        const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
                        // memcpy(&gm->bias_buffer[0], &gm->spad[bias_sp_addr][0], DIM * sizeof(elem_t));
                        qemu_log("Bias buffer loaded: ");
                        for(size_t col = 0; col < cols; col++){
                            gm->bias_buffer[col] = gm->spad[bias_sp_addr][col];
                            qemu_log("%d ", gm->bias_buffer[col]);
                        }
                        qemu_log("\n");
                    } else {
                        /* If no bias provided, zero out the bias buffer */
                        for(size_t col = 0; col < DIM; col++){
                            gm->bias_buffer[col] = 0;
                            qemu_log("%d ", gm->bias_buffer[col]);
                        }
                    }
                }
                /* COMPUTE */
                if(!is_resadd) {
                    uint32_t pre_sp_addr = i == 0 ? B_sp_addr : GARBAGE_ADDR;
                    uint32_t out_sp_addr = C_sp_addr;

                    if (!ex_accumulate && k == 0 && c_spad_id == 0 && gm->gemv_loop_ws_D == 0) { // Added Condition For avoiding overwriting on bias
                        out_sp_addr &= ~(1 << (ADDR_LEN-2));
                    }

                    const uint64_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
                    const uint64_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
                    const uint64_t B_cols = DIM - (j == J - 1 ? pad_J : 0);
                    const uint64_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
                    const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                    const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                    preload((B_rows << 48) | (B_cols << 32) | pre_sp_addr,
                        (C_rows << 48) | (C_cols << 32) | out_sp_addr, gm);
                    
                    /*결과 값 재사용을 하고, K가 마지막이 아닌 경우*/
                    /* compute 명령어의 MSB에 1을 넘겨서 결과를 bias buffer에 누적한다.*/
                    if (c_spad_id == 0){
                        qemu_log("Computing for i=%d/%d, j=%d/%d, k=%d/%d\n", i, I, j, J, k, K);
                        compute((A_rows << 48) | (A_cols << 32) | A_sp_addr,
                        ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0, gm);
                    } else {
                        qemu_log("Computing for i=%d/%d, j=%d/%d, k=%d/%d\n", i, I, j, J, k, K);
                        compute_gemv((k == K-1 ? (uint64_t)0 : (uint64_t)1 << 63) | (A_rows << 48) | (A_cols << 32) | A_sp_addr,
                        ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0, gm);
                    }
                }
                /* MVOUT C (If c_spad_id == 0, if it wasn't, already done in compute)*/
                if (gm->gemv_loop_ws_C != 0 && k == K-1 && c_spad_id == 0) {
                    const size_t sizeof_C = full_C && c_spad_id == 0 ? sizeof(acc_t) : sizeof(elem_t);
                    
                    /* Activation Before MVOUT */
                    if ((act != LAYERNORM) && (act != SOFTMAX) && (c_spad_id == 0)) {
                        qemu_log("act = %d && (c_spad_id == 0), c_spad_id == %d\n", act, c_spad_id);
                        const uint64_t C_dram_addr = gm->gemv_loop_ws_C +
                            (i*gm->gemv_loop_ws_C_stride + j) * DIM * sizeof_C;

                        const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                        const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);
                        mvout(C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr, gm);
                    }

                    /* Softmax or Layernorm  */
                    else if (((act ==LAYERNORM) || (act == SOFTMAX)) && j == J - 1){
                        qemu_log("((act ==LAYERNORM) || (act == SOFTMAX)) && j == J - 1\n");
                        int norm_cmds_size;
                        uint32_t (*norm_cmds)[2];
                        uint32_t ln_norm_cmds[][2] = {{1,2},{3,4},{0,0}};
                        uint32_t sm_norm_cmds[][2] = {{5,5},{6,7},{0,0}};

                        if (act == LAYERNORM) {
                            norm_cmds = ln_norm_cmds;
                            norm_cmds_size = 3;
                        } else {
                            norm_cmds = sm_norm_cmds;
                            norm_cmds_size = 3;
                        }

                        const size_t rows = DIM - (i == I-1 ? pad_I : 0);

                        for (size_t row = 0; row < rows; row += NORM_STAT_IDS) {
                            const size_t stat_ids = rows - row > NORM_STAT_IDS ?
                            NORM_STAT_IDS : rows - row;

                            for (int cmd = 0; cmd < norm_cmds_size; cmd++) {
                                for (size_t stat_id = 0; stat_id < stat_ids; stat_id++) {
                                gemmini_config((1 << 17) | (stat_id << 8) | 3, 0, gm);
                                const size_t r = row + stat_id;

                                    for (size_t jj = 0; jj < J; jj++) {
                                        uint32_t norm_C_sp_addr = C_sp_addr_start + (i*J + jj)*DIM + r;
                                        if (jj + 1 == J) {
                                            norm_C_sp_addr |= (norm_cmds[cmd][1] << 26); // Final mean/inv-std-dev calculation
                                        } else {
                                            norm_C_sp_addr |= (norm_cmds[cmd][0] << 26); // Accumulate sum/variance
                                        }

                                        const uint64_t C_dram_addr = gm->gemv_loop_ws_C +
                                        (i*gm->gemv_loop_ws_C_stride + jj) * DIM * sizeof_C +
                                        r * gm->gemv_loop_ws_C_stride * sizeof_C;

                                        const size_t cols = DIM - (jj + 1 == J ? pad_J : 0);
                                        qemu_log("calling mvout with C_dram_addr=0x%016"PRIx64", i=%d/%d, j=%d/%d, k=%d/%d, normcmd=%d, jj=%ld/%d\n", C_dram_addr, i, I, j, J, k, K, norm_cmds[cmd][0], jj, J);
                                        /* MVOUT will be returned without writing(Contributing to norm) if not last row */
                                        mvout(C_dram_addr, ((uint64_t)1 << 48) | (cols << 32) | norm_C_sp_addr, gm);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void init_trace(GemminiState *gm) {
    gm->total_reads_in_bytes = 0;
    gm->total_writes_in_bytes = 0;
}

void dump_trace(GemminiState *gm) {
    qemu_log("------- Gemmini #%d Trace Dump -------\n", gm->id);
    qemu_log("Total Reads = %lu bytes, Total Writes = %lu bytes\n",
             gm->total_reads_in_bytes, gm->total_writes_in_bytes);
    qemu_log("------- End of Gemmini #%d Trace Dump -------\n", gm->id);
}

/* Decodes ROCC Instructions and calls the appropriate Gemmini function */
void gemmini_exec_cmd(GemminiState *s, uint32_t funct7, uint32_t funct3, uint64_t rs1, u_int64_t rs2, CPUState *env)
{
    qemu_log("Gemmini #%d Decoding cmd: funct7=0x%08x funct3=0x%08x rs1=0x%016"PRIx64" rs2=0x%016"PRIx64"\n",
             s->id, funct7, funct3, rs1, rs2);

    switch (funct7) {
        case 0b0000000: // CONFIG
            gemmini_config(rs1, rs2, s);
            break;
        /* 
         * MVIN, MVOUT, COMPUTE are not executed via ROCC
         * Instead, they are called inside higher level functions like loop_ws
         * So the cases below are just placeholders
         */
        case 0b0000010: // MVIN
            mvin(rs1, rs2, 0, s);
            break;
        case 0b0000011: // MVOUT
            mvout(rs1, rs2, s);
            break;
        case 0b0000100: // Compute Preloaded
            compute(rs1, rs2, 1, s);
            break;
        case 0b0000101: // Compute Accumulate
            compute(rs1, rs2, 0, s);
            break;
        case 0b0001000: // loop ws
            loop_ws(s, rs1, rs2);
            break;
        case 0b0011110: // ternary_loop_ws (Funct7 not given, so using arbitrary)
            ternary_loop_ws(s, rs1, rs2);
            break;
        case 0b0001001: // loop_ws_config_bounds
            loop_ws_config_bounds(rs1, rs2, s);
            break;
        case 0b0001010: // loop_ws_addrs_ab
            loop_ws_addrs_ab(rs1, rs2, s);
            break;
        case 0b0001011: // loop_ws_addrs_dc
            loop_ws_addrs_dc(rs1, rs2, s);
            break;  
        case 0b0001100: // loop_ws_strides_ab
            loop_ws_strides_ab(rs1, rs2, s);
            break;
        case 0b0001101: // loop_ws_strides_dc
            loop_ws_strides_dc(rs1, rs2, s);
            break;
        case 0b0011000: // k_gemv_loop_ws_config_bounds
            k_gemv_loop_ws_config_bounds(rs1, rs2, s);
            break;
        case 0b0011001: // k_gemv_loop_ws_addrs_ab
            k_gemv_loop_ws_addrs_ab(rs1, rs2, s);
            break;
        case 0b0011010: // k_gemv_loop_ws_addrs_dc
            k_gemv_loop_ws_addrs_dc(rs1, rs2, s);
            break;  
        case 0b0011011: // k_gemv_loop_ws_strides_ab
            k_gemv_loop_ws_strides_ab(rs1, rs2, s);
            break;
        case 0b0011100: // k_gemv_loop_ws_strides_dc
            k_gemv_loop_ws_strides_dc(rs1, rs2, s);
            break;
        case 0b0010111: // k_gemv_loop_ws_flags, k_gemv_loop_ws
            k_gemv_loop_ws(rs1, rs2, s);
            break;
        case 0b1111110: // init_trace
            init_trace(s);
            break;
        case 0b1111111: // dump_trace
            dump_trace(s);
            break;
        default:
            qemu_log("Gemmini #%d: Unknown funct7 0x%08x\n", s->id, funct7);
            break;
    }
}