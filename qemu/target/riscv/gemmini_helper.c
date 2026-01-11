#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "trace.h"
#include "gemmini.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <gemmini_internals.h>
void helper_gemmini_config(CPURISCVState *env,target_ulong rs1, target_ulong rs2){
    if((rs1 & 0b11)==0){
	Dataflow new_mode;
	Activation new_act;
	reg_t new_sys_shift, new_sys_acc_shift, new_c_stride, new_a_stride, new_a_transpose, new_b_transpose;
	
	bool rs1_2 = (rs1 >> 2) & 0b1; //rs1 [2]
	if(rs1_2 == 0){
		new_mode = OS;
	}
	else{
		new_mode = WS;
	}
	uint32_t rs1_4_3 = (rs1 >> 3) & 0b11; //rs1 [4:3]
	if(rs1_4_3 == 0){
		if(gemmini_state.acc_act != 4){
			new_act = NONE;
		}
		else{
			new_act = SOFTMAX;
		}
	}else if (rs1_4_3 == 1) {
		new_act = RELU;
	}else if (rs1_4_3 == 2) {
		new_act = LAYERNORM;
	}else if (rs1_4_3 == 3) {
		new_act = IGELU;
	}else{
		assert(false);
	}

	new_sys_shift = (rs2) & 0xFFFFFFFF;
	new_sys_acc_shift = (rs1 >> 32) & 0xFFFFFFFF;
 	new_c_stride = (rs2 >> 48) & 0xFFFF;
    	new_a_stride = (rs1 >> 16) & 0xFFFF;
   	new_a_transpose = (rs1 >> 8) & 0x1;
  	new_b_transpose = (rs1 >> 9) & 0x1;

    	const bool set_only_strides = (rs1 >> 7) & 0x1;
        
	/*printf("RS1 Fields:\n");
    	printf("  acc_scale: %lx, %lu\n",new_sys_acc_shift, new_sys_acc_shift);
   	printf("  A_stride: %lu\n", new_a_stride);
   	printf("  B_transpose: %lu\n", new_b_transpose);
   	printf("  A_transpose: %lu\n", new_a_transpose);
   	printf("  set_only_strides: %d\n", set_only_strides);
    	printf("  sys_act: %u\n", rs1_4_3);
    	printf("  dataflow: %u\n", rs1_2);
    	printf("  CONFIG_EX: %lu\n", (rs1 & 0b11));

   	printf("RS2 Fields:\n");
   	printf("  C_stride: %lu\n", new_c_stride);
   	printf("  sys_shift: %lu\n", new_sys_shift);
*/
	printf("GEMMINI: config_ex - set dataflow mode from %d to %d\n", gemmini_state.mode, new_mode);
    	printf("GEMMINI: config_ex - set activation function from %d to %d\n", gemmini_state.sys_act, new_act);
    	printf("GEMMINI: config_ex - set sys_shift from %lu to %lu\n", gemmini_state.sys_shift, new_sys_shift);
	// assert(new_acc_shift >= 0 && new_acc_shift < sizeof(acc_t)*8);
    	//assert(new_sys_shift >= 0 && new_sys_shift < sizeof(output_t)*8);
    	if (!set_only_strides) {
      		gemmini_state.mode = new_mode;
      		gemmini_state.sys_act = new_act;
      		gemmini_state.sys_shift = new_sys_shift;
      		gemmini_state.sys_acc_shift = new_sys_acc_shift;
      		gemmini_state.a_transpose = new_a_transpose;
      		gemmini_state.b_transpose = new_b_transpose;
    	}	
    	gemmini_state.c_stride = new_c_stride;
    	gemmini_state.a_stride = new_a_stride;
    }else if ((rs1 & 0b11) == 1) { // rs1[1:0] == 2'b01, config_mvin, configure load pipeline
    	const int state_id = (rs1 >> 3) & 0x3;
    	printf("GEMMINI: config_mvin - set load stride from %lu to %lu\n", gemmini_state.load_strides[state_id], rs2);
    	gemmini_state.load_strides[state_id] = rs2;
    	gemmini_state.load_block_strides[state_id] = (rs1 >> 16) & 0xFFFF;
#if defined(HAS_MVIN_SCALE) || defined(HAS_MVIN_ACC_SCALE)
    	printf("GEMMINI: config_mvin - set load scale from %f to %f\n", gemmini_state.load_scales[state_id], scale_t_bits_to_scale_t(rs1 >> 32));
    	gemmini_state.load_scales[state_id] = scale_t_bits_to_scale_t(rs1 >> 32);
    	gemmini_state.load_shrunks[state_id] = (rs1 >> 2) & 1;
#endif
    	gemmini_state.pixels_per_rows[state_id] = (rs1 >> 8) & 0xFF;

    	if (gemmini_state.pixels_per_rows[state_id] == 0)
		gemmini_state.pixels_per_rows[state_id] = 1;
	
/*	printf("GEMMINI: config_mvin - [stride] = %lu (rs2)\n", rs2);
	printf("GEMMINI: config_mvin - [scale float] = %f\n", scale_t_bits_to_scale_t(rs1 >> 32));
	printf("GEMMINI: config_mvin - [shrunk] = %u\n", (uint8_t)((rs1 >> 2) & 0x1));
	printf("GEMMINI: config_mvin - [id] = %u\n", (uint8_t)((rs1 >> 3) & 0x7));
	printf("GEMMINI: config_mvin - [block_mvin_stride] = %u\n", (uint16_t)((rs1 >> 16) & 0xFFFF));
	printf("GEMMINI: config_mvin - [pixel_repeats] = %u\n", (uint8_t)((rs1 >> 8) & 0xFF));
	printf("\n");*/

#ifndef HAS_FIRST_LAYER_OPTIMIZATIONS
	//assert(gemmini_state.pixels_per_rows[state_id] <= 1, "If Gemmini is built without first-layer optimizations, then 'pixels_per_rows' cannot be larger than 1\n");
#endif
  }else if ((rs1 & 0b11) == 2) {
	printf("GEMMINI: config_mvout - set store stride from %lu to %lu\n", gemmini_state.store_stride, rs2 & 0xFFFFFFFF);
    	gemmini_state.store_stride = rs2 & 0xFFFFFFFF;
	
	Activation new_act;
	uint64_t rs1_3_2 = (rs1 >> 2) & 0b11; // extract rs1[3:2], 0 = no activation, 1 = ReLU, 2 = ReLU6
    	if (rs1_3_2 == 0) {
     		if (gemmini_state.acc_act != 4) {
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
    	gemmini_state.acc_act = new_act;

    	uint64_t new_acc_shift = (rs2 >> 32) & 0xFFFFFFFF;
    	gemmini_state.acc_shift = acc_scale_t_bits_to_acc_scale_t(new_acc_shift);

    	gemmini_state.pool_stride = (rs1 >> 4) & 0x3;
    	gemmini_state.pool_size = (rs1 >> 6) & 0x3;
    	gemmini_state.pool_upad = (rs1 >> 8) & 0x3;
    	gemmini_state.pool_lpad = (rs1 >> 10) & 0x3;
    	gemmini_state.pool_out_dim = (rs1 >> 24) & 0xFF;
    	gemmini_state.pool_porows = (rs1 >> 32) & 0xFF;
    	gemmini_state.pool_pocols = (rs1 >> 40) & 0xFF;
    	gemmini_state.pool_orows = (rs1 >> 48) & 0xFF;
    	gemmini_state.pool_ocols = (rs1 >> 56) & 0xFF;
	//printf("GEMMINI: config_mvout - acc_act %u\n",new_act);
    	//printf("GEMMINI: config_mvout - acc_scale %f\n", gemmini_state.acc_shift);	
	if (gemmini_state.pool_stride == 0) {
    	    printf("GEMMINI: config_mvout - no pooling\n");
    	} else {
    	    printf("GEMMINI: config_mvout - set pool_stride to %u\n", gemmini_state.pool_stride);
    	    printf("GEMMINI: config_mvout - set pool_size to %u\n", gemmini_state.pool_size);
    	    printf("GEMMINI: config_mvout - set pool_upad to %u\n", gemmini_state.pool_upad);
     	    printf("GEMMINI: config_mvout - set pool_lpad to %u\n", gemmini_state.pool_lpad);
	    printf("GEMMINI: config_mvout - set pool_out_dim to %u\n", gemmini_state.pool_out_dim);
	    printf("GEMMINI: config_mvout - set pool_porows to %u\n", gemmini_state.pool_porows);
    	    printf("GEMMINI: config_mvout - set pool_pocols to %u\n", gemmini_state.pool_pocols);
    	    printf("GEMMINI: config_mvout - set pool_orows to %u\n", gemmini_state.pool_orows);
       	    printf("GEMMINI: config_mvout - set pool_ocols to %u\n", gemmini_state.pool_ocols);
	    printf("GEMMINI: config_mvout - rs1 is %lu\n", rs1);
    	}	
    }else if ((rs1 & 0b11) == 3) { // rs1[1:0] == 2'b11, config_norm, configure norm pipeline
	gemmini_state.norm_stat_id = (rs1 >> 8) & 0xFF;
    	if (!((rs1 >> 17) & 1)) { // set stat_id only
		gemmini_state.igelu_qb = rs2 & 0xFFFFFFFF;
      		gemmini_state.igelu_qc = (rs2 >> 32) & 0xFFFFFFFF;

      		uint8_t act_msb = (rs1 >> 16) & 0x1;
      		gemmini_state.acc_act = (act_msb << 2) | (int)gemmini_state.acc_act;
      		assert((int)(gemmini_state.acc_act) <= 4);

      	if ((rs1 >> 18) & 1) { // q const type = 1
        	gemmini_state.qln2_inv = (rs1 >> 32) & 0xFFFFFFFF;
		 printf("GEMMINI: config_norm - q_const_inv: %d\n", gemmini_state.qln2_inv);
      	} else {
        	gemmini_state.qln2 = (rs1 >> 32) & 0xFFFFFFFF;
		printf("GEMMINI: config_norm - q_const: %d\n",  gemmini_state.qln2);
      	}
/*	uint8_t  q_const_type    = (rs1 >> 18) & 0x1;
	uint8_t  set_stats_id    = (rs1 >> 17) & 0x1;
	uint8_t  stat_id         = (rs1 >> 8)  & 0xFF;
	
	int32_t igelu_qc        = (int32_t)(rs2 >> 32);
	int32_t igelu_qb        = (int32_t)(rs2 & 0xFFFFFFFF);
	
	printf("GEMMINI: config_norm - q_const_type: %u\n", q_const_type);
	printf("GEMMINI: config_norm - set_stats_id_only: %u\n", set_stats_id);
	printf("GEMMINI: config_norm - act_msb: %u\n", act_msb);
	printf("GEMMINI: config_norm - stat_id: %u\n", stat_id);
	printf("GEMMINI: config_norm - igelu_qc: %d\n", igelu_qc);
	printf("GEMMINI: config_norm - igelu_qb: %d\n", igelu_qb);*/
    }
  }
}

void helper_loop_ws_config_bounds(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // rs1과 rs2에서 값을 추출하여 전역 gemmini_state에 저장
    gemmini_state.loop_ws_pad_I = rs1 & 0xFFFF;
    gemmini_state.loop_ws_pad_J = (rs1 >> 16) & 0xFFFF;
    gemmini_state.loop_ws_pad_K = (rs1 >> 32) & 0xFFFF;

    gemmini_state.loop_ws_I = rs2 & 0xFFFF;
    gemmini_state.loop_ws_J = (rs2 >> 16) & 0xFFFF;
    gemmini_state.loop_ws_K = (rs2 >> 32) & 0xFFFF;

    // 값 출력
    printf("RoCC: LOOP_WS_CONFIG_BOUNDS\n");
    printf("RoCC: pad_I = %d, pad_J = %d, pad_K = %d\n", gemmini_state.loop_ws_pad_I, gemmini_state.loop_ws_pad_J, gemmini_state.loop_ws_pad_K);
    printf("RoCC: I = %d, J = %d, K = %d\n", gemmini_state.loop_ws_I, gemmini_state.loop_ws_J, gemmini_state.loop_ws_K);
}

void helper_loop_ws_addrs_ab(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // A와 B 주소를 설정
    gemmini_state.loop_ws_A = rs1;
    gemmini_state.loop_ws_B = rs2;

    // 값 출력
    printf("RoCC: LOOP_WS_ADDRS_AB\n");
    printf("RoCC: A base addr = 0x%08lx\n", gemmini_state.loop_ws_A);
    printf("RoCC: B base addr = 0x%08lx\n", gemmini_state.loop_ws_B);
}

void helper_loop_ws_addrs_dc(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // D와 C 주소를 설정
    gemmini_state.loop_ws_D = rs1;
    gemmini_state.loop_ws_C = rs2;

    // 값 출력
    printf("RoCC: LOOP_WS_ADDRS_DC\n");
    printf("RoCC: D base addr = 0x%08lx\n", gemmini_state.loop_ws_D);
    printf("RoCC: C base addr = 0x%08lx\n", gemmini_state.loop_ws_C);
}

void helper_loop_ws_strides_ab(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // A와 B stride 값을 설정
    gemmini_state.loop_ws_A_stride = rs1;
    gemmini_state.loop_ws_B_stride = rs2;

    // 값 출력
    printf("RoCC: LOOP_WS_STRRIDES_AB\n");
    printf("RoCC: A stride = %lu\n", gemmini_state.loop_ws_A_stride);
    printf("RoCC: B stride = %lu\n", gemmini_state.loop_ws_B_stride);
}

void helper_loop_ws_strides_dc(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // D와 C stride 값을 설정
    gemmini_state.loop_ws_D_stride = rs1;
    gemmini_state.loop_ws_C_stride = rs2;

    // 값 출력
    printf("RoCC: LOOP_WS_STRRIDES_DC\n");
    printf("RoCC: D stride = %lu\n", gemmini_state.loop_ws_D_stride);
    printf("RoCC: C stride = %lu\n", gemmini_state.loop_ws_C_stride);
}

void helper_k_gemv_loop_ws_config_bounds(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // rs1과 rs2에서 값을 추출하여 전역 gemmini_state에 저장
    gemmini_state.gemv_loop_ws_pad_I = rs1 & 0xFFFF;
    gemmini_state.gemv_loop_ws_pad_J = (rs1 >> 16) & 0xFFFF;
    gemmini_state.gemv_loop_ws_pad_K = (rs1 >> 32) & 0xFFFF;

    gemmini_state.gemv_loop_ws_I = rs2 & 0xFFFF;
    gemmini_state.gemv_loop_ws_J = (rs2 >> 16) & 0xFFFF;
    gemmini_state.gemv_loop_ws_K = (rs2 >> 32) & 0xFFFF;

    // 값 출력
    printf("RoCC: K_GEMV_LOOP_WS_CONFIG_BOUNDS\n");
    printf("RoCC: pad_I = %d, pad_J = %d, pad_K = %d\n", gemmini_state.gemv_loop_ws_pad_I, gemmini_state.gemv_loop_ws_pad_J, gemmini_state.gemv_loop_ws_pad_K);
    printf("RoCC: I = %d, J = %d, K = %d\n", gemmini_state.gemv_loop_ws_I, gemmini_state.gemv_loop_ws_J, gemmini_state.gemv_loop_ws_K);
}

void helper_k_gemv_loop_ws_addrs_ab(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // A와 B 주소를 설정
    gemmini_state.gemv_loop_ws_A = rs1;
    gemmini_state.gemv_loop_ws_B = rs2;

    // 값 출력
    printf("RoCC: K_GEMV_LOOP_WS_ADDRS_AB\n");
    printf("RoCC: A base addr = 0x%08lx\n", gemmini_state.gemv_loop_ws_A);
    printf("RoCC: B base addr = 0x%08lx\n", gemmini_state.gemv_loop_ws_B);
}

void helper_k_gemv_loop_ws_addrs_dc(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // D와 C 주소를 설정
    gemmini_state.gemv_loop_ws_D = rs1;
    gemmini_state.gemv_loop_ws_C = rs2;

    // 값 출력
    printf("RoCC: K_GEMV_LOOP_WS_ADDRS_DC\n");
    printf("RoCC: D base addr = 0x%08lx\n", gemmini_state.gemv_loop_ws_D);
    printf("RoCC: C base addr = 0x%08lx\n", gemmini_state.gemv_loop_ws_C);
}

void helper_k_gemv_loop_ws_strides_ab(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // A와 B stride 값을 설정
    gemmini_state.gemv_loop_ws_A_stride = rs1;
    gemmini_state.gemv_loop_ws_B_stride = rs2;

    // 값 출력
    printf("RoCC: K_GEMV_LOOP_WS_STRRIDES_AB\n");
    printf("RoCC: A stride = %lu\n", gemmini_state.gemv_loop_ws_A_stride);
    printf("RoCC: B stride = %lu\n", gemmini_state.gemv_loop_ws_B_stride);
}

void helper_k_gemv_loop_ws_strides_dc(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
    // D와 C stride 값을 설정
    gemmini_state.gemv_loop_ws_D_stride = rs1;
    gemmini_state.gemv_loop_ws_C_stride = rs2;

    // 값 출력
    printf("RoCC: K_GEMV_LOOP_WS_STRRIDES_DC\n");
    printf("RoCC: D stride = %lu\n", gemmini_state.gemv_loop_ws_D_stride);
    printf("RoCC: C stride = %lu\n", gemmini_state.gemv_loop_ws_C_stride);
}

void preload(uint64_t bd_addr, uint64_t c_addr) {
  gemmini_state.preload_sp_addr = (uint32_t)(bd_addr & 0xFFFFFFFF);
  gemmini_state.output_sp_addr = (uint32_t)(c_addr & 0xFFFFFFFF);

  gemmini_state.preload_cols = (bd_addr >> addr_len) & 0xFFFF;
  gemmini_state.preload_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
  gemmini_state.output_cols = (c_addr >> addr_len) & 0xFFFF;
  gemmini_state.output_rows = (c_addr >> (addr_len + 16)) & 0xFFFF;

  printf("GEMMINI: preload - scratchpad output addr = 0x%08x, scratchpad preload addr = 0x%08x\n",gemmini_state.output_sp_addr, gemmini_state.preload_sp_addr);
}

void mvin(CPURISCVState *env, uint64_t dram_addr, uint64_t sp_addr, int state_id) {
    printf("mvin_func\n");
    bool const accumulator = (sp_addr >> 31) & 0x1;
    bool const accumulate = (sp_addr >> 30) & 0x1;    
    uint32_t base_row_addr = (sp_addr & 0x1FFFFFFF);
    uint32_t cols = (sp_addr >> 32) & 0xFFFF;
    uint32_t rows = (sp_addr >> 48) & 0xFFFF;

    bool is_zeros = dram_addr == 0;
    
    uint64_t const load_stride = gemmini_state.load_strides[state_id];
    uint16_t const load_block_stride = gemmini_state.load_block_strides[state_id];
    bool const load_shrunk = gemmini_state.load_shrunks[state_id]; 
#if defined(HAS_MVIN_SCALE) || defined(HAS_MVIN_ACC_SCALE)
    float const load_scale = gemmini_state.load_scales[state_id];
#endif
    uint8_t const pixels_per_row = gemmini_state.pixels_per_rows[state_id];
    //printf("GEMMINI: mvin - %u cols and %u rows from 0x%08lx to addr 0x%08lx\n", cols, rows, dram_addr, sp_addr & 0xFFFFFFFF);
    for (uint32_t row = 0; row < rows; ++row) {
        uint64_t dram_row_addr = dram_addr + row * load_stride;
        //printf(" row : %d, rows : %d\n", row,rows);
        for (uint32_t col = 0; col < cols; ++col) {
	    	uint32_t block = col / DIM;
		uint32_t spad_col = col % DIM;
    		uint32_t spad_row = base_row_addr + row + block * load_block_stride; // block stride = cols (대충 고정)
		//printf("pixels_per_row : %u, spad_row : %u\n",pixels_per_row, spad_row);
		for (size_t pixel = 0; pixel < pixels_per_row && pixel <= spad_row; pixel++) {
	    		if (accumulator) {
				uint64_t const dram_byte_addr = dram_row_addr + col * (load_shrunk ? sizeof(elem_t) : sizeof(acc_t));
				acc_t value;
		                if (is_zeros) {
					value = 0;
            			} else if (!load_shrunk) {
#ifdef ELEM_T_IS_FLOAT
              				value = acc_t_bits_to_acc_t(cpu_lduw_data(env, dram_byte_addr));
#else
              				value = cpu_lduw_data(env, dram_byte_addr);
#endif

#ifdef HAS_MVIN_ACC_SCALE
     			        	value = mvin_scale_acc(value, load_scale);
#endif
            			} else {
#ifdef ELEM_T_IS_FLOAT
              				value = elem_t_bits_to_elem_t(cpu_ldub_data(env, dram_byte_addr));
#else
              				value = cpu_ldub_data(env, dram_byte_addr);
#endif

#ifdef HAS_MVIN_SCALE
              				value = mvin_scale(value, load_scale);
#endif
           		 	}

            			if (accumulate) {
              				gemmini_state.accumulator[spad_row - pixel][spad_col + pixel*cols] += value;
            			} else {
              				gemmini_state.accumulator[spad_row - pixel][spad_col + pixel*cols] = value;
            			}

#ifdef ELEM_T_IS_FLOAT
            			printf("%f ",  gemmini_state.accumulator[spad_row][spad_col]);
#else
            			printf("%d ",  gemmini_state.accumulator[spad_row][spad_col]);
#endif
        		} else {
            			uint64_t const dram_byte_addr = dram_row_addr + col*sizeof(elem_t);
            			//printf("dram_addr : 0x%08lx\n",dram_byte_addr);
				elem_t value;
            			if (is_zeros) {
              				value = 0;
           			 }else {
#ifdef ELEM_T_IS_FLOAT
              				value = elem_t_bits_to_elem_t(cpu_ldub_data(env, dram_byte_addr));
#else
              				value = cpu_ldub_data(env, dram_byte_addr);
#endif

#ifdef HAS_MVIN_SCALE
              				value = mvin_scale(value, load_scale);
#endif
         			}

            			gemmini_state.spad[spad_row - pixel][spad_col + pixel * cols] = value;
				//printf("spad[%lu][%li]=%d\n",spad_row - pixel,spad_col + pixel * cols,gemmini_state.spad[spad_row - pixel][spad_col + pixel * cols]);

#ifdef ELEM_T_IS_FLOAT
            			printf("%f ",gemmini_state.spad[spad_row][spad_col]);
#else
            			printf("%d ", gemmini_state.spad[spad_row][spad_col]);
#endif
        		}
      		}
    	}
	printf("\n");
  }
}
void compute(uint64_t a_addr, uint64_t bd_addr, bool preload) {
    uint32_t a_addr_real = (uint32_t)(a_addr & 0xFFFFFFFF);
    uint32_t bd_addr_real = (uint32_t)(bd_addr & 0xFFFFFFFF);

    bool accumulate = a_addr >> (addr_len + 31) & 0x1;
    
    const uint16_t a_cols = (a_addr >> addr_len) & 0xFFFF;
    const uint16_t a_rows = (a_addr >> (addr_len + 16)) & 0xFFFF;

    const uint16_t bd_cols = (bd_addr >> addr_len) & 0xFFFF;
    const uint16_t bd_rows = (bd_addr >> (addr_len + 16)) & 0xFFFF;
    printf("GEMMINI: compute - preload = %d, scratchpad A addr = 0x%08x,"
           "scratchpad B addr 0x%08x\n", preload, a_addr_real, bd_addr_real);
    // Preload
    if (preload) {
	printf("GEMMINI: compute - PEs after preloading:\n");
        for (size_t i = 0; i < DIM; i++) {
            for (size_t j = 0; j < DIM; j++) {
                if (~gemmini_state.preload_sp_addr != 0) {
                    // 현재는 Acc에서 preload는 미지원
                    assert(((gemmini_state.preload_sp_addr >> 30) & 0b11) == 0);
                }

                bool preload_transpose = (gemmini_state.mode == WS) && gemmini_state.b_transpose;
                size_t r = preload_transpose ? j : i;
                size_t c = preload_transpose ? i : j;
                if (i < gemmini_state.preload_rows && j < gemmini_state.preload_cols) {
                    int8_t preload_value = (~gemmini_state.preload_sp_addr == 0)? 0
			    : gemmini_state.spad[gemmini_state.preload_sp_addr + r][c];
		    gemmini_state.pe_state[i][j] = preload_value;
                } else {
                    gemmini_state.pe_state[i][j] = 0;
                }
#ifdef ELEM_T_IS_FLOAT
		printf("%f ", gemmini_state.pe_state[i][j]);
#else
		printf("%d ", gemmini_state.pe_state[i][j]);
#endif
            }
	    printf("\n");
        }
    }
    // 결과용 버퍼
    acc_t results[DIM][DIM] = {0};
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            if (i < bd_rows && j < bd_cols) {
                results[i][j] = (~bd_addr_real == 0) ? 0 : gemmini_state.spad[bd_addr_real + i][j];
		//printf("result[%lu]%lu] = %d\n",i,j,results[i][j]);
            } else {
                results[i][j] = 0;
            }
        }
    }

    // 행렬 곱
    for (size_t i = 0; i < DIM; ++i) {
        for (size_t j = 0; j < DIM; ++j) {
            for (size_t k = 0; k < DIM; ++k) {
                elem_t a=0;
                if (~a_addr_real != 0) {
                    const size_t r = gemmini_state.a_stride * (gemmini_state.a_transpose ? k : i);
                    const size_t c = gemmini_state.a_transpose ? i : k;

                    a = i < a_rows && k < a_cols ? gemmini_state.spad[a_addr_real + r][c]: 0 ;
                }
                if (gemmini_state.mode == WS) {//ws mode
                    results[i][j] += a * gemmini_state.pe_state[k][j];
                } else { //os mdoe
                    elem_t b = 0;
                    if (~bd_addr_real != 0) {
                        const size_t r = gemmini_state.b_transpose ? j : k;
                        const size_t c = gemmini_state.b_transpose ? k : j;

                        b = k < bd_rows && j < bd_cols ? gemmini_state.spad[bd_addr_real+r][c] : 0;
                    }
		     gemmini_state.pe_state[i][j] += a * b;
                }
            }
        }
    }
    printf("GEMMINI: compute - PEs after matmul:\n");
    for (size_t i = 0; i < DIM; ++i) {
	for (size_t j = 0; j < DIM; ++j) {
#ifdef ELEM_T_IS_FLOAT
        printf("%f ", gemmini_state.pe_state[i][j]);
#else
        printf("%d ", gemmini_state.pe_state[i][j]);
#endif
        }
        printf("\n");
    }

    // 결과 저장
    if (~gemmini_state.output_sp_addr != 0) {
        bool acc = (gemmini_state.output_sp_addr >> 31) & 0x1;
	printf("acc : %d\n",acc);
        bool acc_accum = (gemmini_state.output_sp_addr >> 30) & 0x1;
        uint32_t base_sp_addr = gemmini_state.output_sp_addr & 0x1FFFFFFF;
        //printf("output_rows : %d , output_cols : %d, acc_accum : %d\n", gemmini_state.output_rows,gemmini_state.output_cols,acc_accum);
        for (size_t i = 0; i < gemmini_state.output_rows; ++i) {
            for (size_t j = 0; j < gemmini_state.output_cols; ++j) {
                acc_t value = gemmini_state.mode == OS ? gemmini_state.pe_state[i][j] : results[i][j];
                if (acc) {
		    //output_t shifted = gemmini_state.mode == OS ? sys_shift(value, gemmini_state.sys_shift) : sys_shift(value, 0);
		    if (acc_accum) {
			gemmini_state.accumulator[base_sp_addr + gemmini_state.c_stride * i][j] += value;
                        //gemmini_state.accumulator[base_sp_addr + gemmini_state.loop_ws_C_stride*i][j] += shifted;
                    } else {
			gemmini_state.accumulator[base_sp_addr + gemmini_state.c_stride* i][j] = value;
                        //gemmini_state.accumulator[base_sp_addr + gemmini_state.loop_ws_C_stride*i][j] = shifted;
                    }
#ifdef ELEM_T_IS_FLOA
         	   // printf("%f ", gemmini_state.accumulator[base_sp_addr + gemmini_state.c_stride* i][j]);
#else
		    printf("%d ", gemmini_state.accumulator[base_sp_addr + gemmini_state.c_stride* i][j]);
         	    //printf("accumulator[%zu][%zu] : %d\n", base_sp_addr + gemmini_state.c_stride* i,j,gemmini_state.accumulator[base_sp_addr + gemmini_state.c_stride* i][j]);
#endif
                } 
		else { //spad에 저장
		    elem_t shifted = gemmini_state.mode == OS ?
                    sys_shift(value, gemmini_state.sys_shift) :
                    sys_shift(value, 0);
		    elem_t activated = apply_activation_sys(shifted);
		    //printf("vlaue : %d, shifted : %d, activated : %d, accumulate: %d\n",value, shifted, activated,accumulate);
		    const size_t row_addr = base_sp_addr + gemmini_state.c_stride * i;
		    const size_t col_addr = j;
                    if(accumulate) {
                        gemmini_state.bias_buffer[0][col_addr] += activated;
		    }
		    else {
                        elem_t total = gemmini_state.bias_buffer[0][col_addr] + activated;
			gemmini_state.spad[row_addr][col_addr] = total;
			gemmini_state.bias_buffer[0][col_addr] = 0;
		    }
#ifdef ELEM_T_IS_FLOAT
		    printf("%f ", gemmini_state.spad[base_sp_addr + gemmini_state.c_stride * i][j]);
#else
		    printf("%d ", gemmini_state.spad[base_sp_addr + gemmini_state.c_stride * i][j]);
          	    //printf("spad[%zu][%zu] : %d\n",base_sp_addr + gemmini_state.c_stride * i,j, gemmini_state.spad[base_sp_addr + gemmini_state.c_stride * i][j]);

#endif
                }
	        gemmini_state.pe_state[i][j]=0;	
            }
        }
    }
    gemmini_state.preload_rows=gemmini_state.preload_cols=0;
}

void mvout(CPURISCVState *env, uint64_t dram_addr, uint64_t sp_addr){   //수정이 필요함
    printf("mvout func\n");
    bool accumulator = (sp_addr >> 31) & 0x1;
    bool full = (sp_addr >> 29) & 0x1;
    NormCmd norm_cmd = (sp_addr >> 26) & 0x7;
    uint64_t base_row_addr = sp_addr & 0x3FFFFFF;
    uint64_t cols = (sp_addr >> ADDR_LEN) & 0xFFFF;
    uint64_t rows = (sp_addr >> (ADDR_LEN + 16)) & 0xFFFF;
    //printf("accumulator : %d\n",accumulator); 
    const int block_stride = DIM;
    //printf("GEMMINI: mvout - %lu cols and %lu rows from 0x%08lx to addr 0x%08lx\n", cols, rows, sp_addr, dram_addr);

    if(gemmini_state.pool_stride == 0){
	    for (size_t i = 0; i < rows; ++i) {
		    uint64_t dram_row_addr = dram_addr + i * gemmini_state.store_stride;
		    bool should_write = true;
		    for (size_t j = 0; j < cols; j += DIM) {
			    size_t block = j / DIM;
			    size_t spad_row = base_row_addr + block * block_stride + i;
			    size_t len = (cols - j > DIM) ? DIM : (cols - j);

			    bool is_last = (j + DIM >= cols);
			    NormCmd n_cmd = is_last ? norm_cmd : non_terminating_norm_cmd(norm_cmd);
			    //printf("n_cmd : %d\n", n_cmd);
			    //printf("accumulator[%lu][0] : %d\n",spad_row, gemmini_state.accumulator[spad_row][0]);
			    should_write = apply_norm(&gemmini_state.accumulator[spad_row][0], len, n_cmd);
		    }
		    if (!should_write)
			    continue;

		    for (size_t j = 0; j < cols; ++j) {
			    size_t block = j / DIM;
		    	    size_t spad_col = j % DIM;
		  	    size_t spad_row = base_row_addr + block *block_stride + i;
		 	    if (accumulator) {
		      		    acc_t acc_value = gemmini_state.accumulator[spad_row][spad_col];
				    //printf("acc_value_accumulator[%zu][%zu] = %d\n",spad_row,spad_col, acc_value);
		    		    acc_value = apply_pre_activation_acc(acc_value);
				    //printf("acc_value = %d\n",acc_value);
				    acc_t shifted = acc_scale(acc_value, gemmini_state.acc_shift);
			 	    //printf("shifted = %d\n",shifted);
				    elem_t activated = apply_activation_acc(shifted);
				   // printf("activated = %d\n",activated);
		     		    uint64_t dram_byte_addr = dram_row_addr + j * (full ? sizeof(acc_t) : sizeof(elem_t));
		    		    //printf("i : %lu, j : %lu, addr : 0x%lu, value : %d\n ",i,j, dram_byte_addr,acc_value);
#ifdef ELEMT_IS_FLOAT
			  	    if (full) {  //FLOAT은 다 수정해야함.
					    cpu_stl_data(env, dram_byte_addr, acc_value);
					    printf("%f ", acc_value);
			  	    } else {
					    cpu_stb_data(env, dram_byte_addr, activated);
					    printf("%f ", activated);
			  	    }
#else
			  	    if (full) {
					    cpu_stl_data(env, dram_byte_addr, acc_value);
					    printf("%d ", acc_value);
			  	    } else {
					    cpu_stb_data(env, dram_byte_addr, activated);
					    printf("%d ", activated);
				    }
#endif
			    }
			    else { // Scratchpad, write to DRAM directly

				 uint64_t const dram_byte_addr = dram_row_addr + j*sizeof(elem_t);
		       		 elem_t value = gemmini_state.spad[spad_row][spad_col];
				// printf("value : %d\n", value);
				 
#ifdef ELEM_T_IS_FLOAT
				 cpu_stb_data(env, dram_byte_addr, value);
		       		 printf("%f ", value);
#else
				 cpu_stb_data(env, dram_byte_addr, value);
		       		 printf("%d ", value);
#endif
		    	    }
	      	    }
	      	    printf("\n");
	    }

    }
    else{
	    uint8_t const pool_stride = gemmini_state.pool_stride;
	    uint8_t const pool_size = gemmini_state.pool_size;
	    uint8_t const pool_out_dim = gemmini_state.pool_out_dim;
	    uint8_t const porows = gemmini_state.pool_porows;
	    uint8_t const pocols = gemmini_state.pool_pocols;
	    uint8_t const orows = gemmini_state.pool_orows;
	    uint8_t const ocols = gemmini_state.pool_ocols;
	    uint8_t const plpad = gemmini_state.pool_lpad;
	    uint8_t const pupad = gemmini_state.pool_upad;
	    uint8_t const channels = cols;

	    for (int porow = 0; porow < porows; porow++) {
	      	    for (int pocol = 0; pocol < pocols; pocol++) {
		    	    for (int poch = 0; poch < (int)channels; poch++) {
			  	    elem_t value = elem_t_min;

			  	    for (int wrow = 0; wrow < pool_size; wrow++) {
					    for (int wcol = 0; wcol < pool_size; wcol++) {

				      		    const int orow = porow * pool_stride + wrow - pupad;
				      		    const int ocol = pocol * pool_stride + wcol - plpad;

				      		    const int row_addr = base_row_addr + orow*ocols + ocol;

				      		    elem_t elem;

				      		    if (orow < 0 || ocol < 0 || orow >= orows || ocol >= ocols) {
					    		    elem = 0;
				      		    } else if (accumulator) {
					    		    acc_t acc_value = gemmini_state.accumulator[row_addr][poch];
					    		    int8_t shifted = acc_scale(acc_value, gemmini_state.acc_shift);
					    		    elem = apply_activation_acc(shifted); // Activation is always applied in either WS/OS mode
						    } else {
					    		    elem = gemmini_state.spad[row_addr][poch];
				      		    }
						    
				      		    if (elem > value) {
					    		    value = elem;
				      		    }
					    }
			  	    }
			  	    uint64_t const dram_byte_addr = dram_addr + (porow * pool_out_dim + pocol) * gemmini_state.store_stride + poch * sizeof(elem_t);

#ifdef ELEM_T_IS_FLOAT
			  	    cpu_stb_data(env, dram_byte_addr, value);
#else
				    cpu_stb_data(env, dram_byte_addr, value);
#endif
			    }
	      	    }
	    }
    }
}
void helper_loop_ws(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
        /*printf("RoCC: struct K_GEMV_LOOP_WS_CONFIG_BOUNDS\n");
        printf("RoCC: pad_I = %d, pad_J = %d, pad_K = %d\n", gemmini_state.loop_ws_pad_I, gemmini_state.loop_ws_pad_J, gemmini_state.loop_ws_pad_K);
        printf("RoCC: I = %d, J = %d, K = %d\n", gemmini_state.loop_ws_I, gemmini_state.loop_ws_J, gemmini_state.loop_ws_K);
        printf("\nRoCC: struct K_GEMV_LOOP_WS_ADDRS_AB 실행\n");
        printf("RoCC: A base addr = 0x%lu\n", gemmini_state.loop_ws_A);
        printf("RoCC: B base addr = 0x%lu\n", gemmini_state.loop_ws_B);
        printf("\nRoCC: struct K_GEMV_LOOP_WS_ADDRS_DC 실행\n");
        printf("RoCC: D base addr = 0x%lu\n", gemmini_state.loop_ws_D);
        printf("RoCC: C base addr = 0x%lu\n", gemmini_state.loop_ws_C);
        printf("\nRoCC: K_GEMV_LOOP_WS_STRIDES_AB 실행\n");
        printf("RoCC: A_stride = %lu\n", gemmini_state.loop_ws_A_stride);
        printf("RoCC: B_stride = %lu\n", gemmini_state.loop_ws_B_stride);
        printf("\nRoCC: K_GEMV_LOOP_WS_STrIDES_DC 실행\n");
        printf("RoCC: D_stride = %lu\n", gemmini_state.loop_ws_D_stride);
        printf("RoCC: C_stride = %lu\n", gemmini_state.loop_ws_C_stride);
        printf("\nRoCC: K_GEMV_LOOP_WS_Flag 실행\n");*/
        const bool ex_accumulate = rs1 & 1;
        const bool full_C = (rs1 >> 1) & 1;
        const bool low_D = (rs1 >> 2) & 1;
        const uint8_t act = (rs1 >> 8) & 0x7;
        const bool a_transpose = rs2 & 1;
        const bool b_transpose = (rs2 >> 1) & 1;
	printf("RoCC: ex_accumulate = %d, full_C = %d, low_D = %d, act = %d\n", ex_accumulate,full_C,low_D,act);
        printf("RoCC: a_transpose = %d, b_transpose = %d\n",a_transpose,b_transpose);

        const uint16_t I = gemmini_state.loop_ws_I;
        const uint16_t J = gemmini_state.loop_ws_J;
        const uint16_t K = gemmini_state.loop_ws_K;

        const uint16_t pad_I = gemmini_state.loop_ws_pad_I;
        const uint16_t pad_J = gemmini_state.loop_ws_pad_J;
        const uint16_t pad_K = gemmini_state.loop_ws_pad_K;

        const uint32_t GARBAGE_ADDR = ~0;

        const int total_spad_rows = (I * K + K * J) * DIM;
        const int total_acc_rows = (I * J) * DIM;
        if (total_spad_rows > BANK_NUM * BANK_ROWS / 2 || total_acc_rows > ACC_ROWS / 2){
                printf("LOOP_WS bounds were too large for double-buffering\n");
                exit(1);
        }
        const uint32_t A_sp_addr_start = 0;
        const uint32_t B_sp_addr_start = (BANK_NUM * BANK_ROWS / 2) - K * J * DIM;
	const uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
	const uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3));
        //printf("A_sp_addr_start : 0x%u\n", A_sp_addr_start);
        //printf("B_sp_addr_start : 0x%08x\n", B_sp_addr_start);
        //printf("D_sp_addr_start : 0x%08x\n", D_sp_addr_start);
        //printf("C_sp_addr_start : 0x%08x\n", C_sp_addr_start);
	if (gemmini_state.loop_ws_D != 0) {
		for (uint16_t i = 0; i < I; i++) {
		     	for (uint16_t j = 0; j < J; j++) {
				const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);

				const uint64_t dram_addr = gemmini_state.loop_ws_D + (i * gemmini_state.loop_ws_D_stride + j) * DIM * sizeof_D;
				const uint64_t sp_addr = D_sp_addr_start + (i*J + j)*DIM;
				const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
				const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);

				mvin(env, dram_addr, (rows << 48) | (cols << 32) | sp_addr, 2);
		  	}
	    	}
       	}
        for (uint16_t k = 0; k < K; k++) {
                for (uint16_t j = 0; j < J; j++) {
                        for (uint16_t i = 0; i < I; i++) {
                                //printf("i = %u, I = %u\n",i, I);
                                //printf("j = %u, J = %u\n",j, J);
                                //printf("k = %u, K = %u\n",k, K);
                                const uint32_t A_sp_addr = a_transpose ? (A_sp_addr_start + (k*I + i)*DIM) :(A_sp_addr_start + (i*K + k)*DIM);
                                const uint32_t B_sp_addr = b_transpose ? (B_sp_addr_start + (j*K + k)*DIM) :(B_sp_addr_start + (k*J + j)*DIM);
                                const uint32_t C_sp_addr = C_sp_addr_start + (i*J + j)*DIM;
                                if (j == 0) { //Mvin A
                                        uint64_t dram_addr, cols, rows;
                                        if (a_transpose) {
                                                dram_addr = gemmini_state.loop_ws_A + (k*gemmini_state.loop_ws_A_stride + i) * DIM * sizeof(elem_t);
                                                cols = DIM - (i == I-1 ? pad_I : 0);
                                                rows = DIM - (k == K-1 ? pad_K : 0);
                                        }else{
                                                dram_addr = gemmini_state.loop_ws_A +(i*gemmini_state.loop_ws_A_stride + k) * DIM * sizeof(elem_t);
                                                cols = DIM - (k == K-1 ? pad_K : 0);
                                                rows = DIM - (i == I-1 ? pad_I : 0);
                                        }
                                       //printf("dram_addr : 0x%lu\n", dram_addr);
                                       // printf("A_sp_addr : 0x%u\n", A_sp_addr);
                                       // printf("cols : %lu, rows : %lu\n", cols, rows);
                                        mvin(env,dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0);
                                }
                                if (i==0){
                                        int64_t dram_addr, cols, rows;
                                        if (b_transpose) {
                                                dram_addr = gemmini_state.loop_ws_B + (j*gemmini_state.loop_ws_B_stride + k) * DIM * sizeof(elem_t);
                                                cols = DIM - (k == K-1 ? pad_K : 0);
                                                rows = DIM - (j == J-1 ? pad_J : 0);
                                        }
                                        else{
                                                dram_addr = gemmini_state.loop_ws_B + (k*gemmini_state.loop_ws_B_stride + j) * DIM * sizeof(elem_t);
                                                cols = DIM - (j == J-1 ? pad_J : 0);
                                                rows = DIM - (k == K-1 ? pad_K : 0);
                                        }
                                        //printf("dram_addr : 0x%lu\n", dram_addr);
                                        //printf("B_sp_addr : 0x%u\n", B_sp_addr);
                                        //printf("cols : %lu, rows : %lu\n", cols, rows);
                                        mvin(env, dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1);
                                }
                                { //compute
                                        uint32_t pre_sp_addr = i == 0 ? B_sp_addr : GARBAGE_ADDR;
                                        uint32_t out_sp_addr = C_sp_addr;

                                        if (!ex_accumulate && k == 0){
                                                out_sp_addr &= ~(1 << (ADDR_LEN-2));
                                        }

                                        const uint64_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
                                        const uint64_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
                                        const uint64_t B_cols = DIM - (j == J - 1 ? pad_J : 0);
                                        const uint64_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
                                        const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
                                        const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

                                       // printf("B_sp_addr : 0x%08x\n", B_sp_addr);
                                       // printf("C_sp_addr : 0x%08x\n", C_sp_addr);
                                       // printf("out_sp_addr : 0x%08x\n", out_sp_addr);
                                        preload((B_rows << 48) | (B_cols << 32) | pre_sp_addr, (C_rows << 48) | (C_cols << 32) | out_sp_addr);

                                        compute((A_rows << 48) | (A_cols << 32) | A_sp_addr, ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0);
                                }
				if (gemmini_state.loop_ws_C != 0 && k == K-1) {
			      		const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);

			      		if ((act != LAYERNORM) && (act != SOFTMAX)) {
				    		const uint64_t C_dram_addr = gemmini_state.loop_ws_C +
							(i*gemmini_state.loop_ws_C_stride + j) * DIM * sizeof_C;

				    		const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
				    		const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

				    		mvout(env, C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr);
			      		}

			      		else if (((act == LAYERNORM) || (act == SOFTMAX)) && j == J - 1){
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
						      			helper_gemmini_config(env, (1 << 17) | (stat_id << 8) | 3, 0);
						      			const size_t r = row + stat_id;
									
						      			for (size_t jj = 0; jj < J; jj++) {
							    			uint32_t norm_C_sp_addr = C_sp_addr_start + (i*J + jj)*DIM + r;
							    			if (jj + 1 == J) {
								  			norm_C_sp_addr |= (norm_cmds[cmd][1] << 26); // Final mean/inv-std-dev calculation
										} else {
											norm_C_sp_addr |= (norm_cmds[cmd][0] << 26); // Accumulate sum/variance
								                }

							    			const uint64_t C_dram_addr = gemmini_state.loop_ws_C +
								  			(i*gemmini_state.loop_ws_C_stride + jj) * DIM * sizeof_C +
								  			r * gemmini_state.loop_ws_C_stride * sizeof_C;

							    			const size_t cols = DIM - (jj + 1 == J ? pad_J : 0);
							    			// gemmini_extended_mvout(C_dram_addr, norm_C_sp_addr, cols, 1);
								                mvout(env, C_dram_addr, ((uint64_t)1 << 48) | (cols << 32) | norm_C_sp_addr);
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

void helper_k_gemv_loop_ws_flags(CPURISCVState *env, target_ulong rs1, target_ulong rs2) {
  const bool ex_accumulate = rs1 & 1;
  const bool full_C = (rs1 >> 1) & 1;
  const bool low_D = (rs1 >> 2) & 1;
  const uint8_t act = (rs1 >> 8) & 0x7;
  const bool a_transpose = rs2 & 1;
  const bool b_transpose = (rs2 >> 1) & 1;
  uint8_t a_spad_id = (rs1 >> 22) & 0b111;
  uint8_t b_spad_id = (rs1 >> 19) & 0b111;
  uint8_t c_spad_id = (rs1 >> 16) & 0b111;
  const bool is_resadd = (rs2 >> 2) & 1;
  printf("ex_accumulate=%d, full_C=%d, low_D=%d, act=0x%02x, a_transpose=%d, b_transpose=%d, a_spad_id=0x%02x, b_spad_id=0x%02x, c_spad_id=0x%02x, is_resadd=%d\n",
       ex_accumulate, full_C, low_D, act, a_transpose, b_transpose, a_spad_id, b_spad_id, c_spad_id, is_resadd);

  const uint16_t I = gemmini_state.gemv_loop_ws_I;
  const uint16_t J = gemmini_state.gemv_loop_ws_J;
  const uint16_t K = gemmini_state.gemv_loop_ws_K;

  const uint16_t pad_I = gemmini_state.gemv_loop_ws_pad_I;
  const uint16_t pad_J = gemmini_state.gemv_loop_ws_pad_J;
  const uint16_t pad_K = gemmini_state.gemv_loop_ws_pad_K;

  const uint32_t GARBAGE_ADDR = ~0;

  const int total_spad_rows = (I * K + K * J) * DIM;
  const int total_acc_rows = (I * J) * DIM;
  //TODO : It's not true
  if (total_spad_rows > BANK_NUM * BANK_ROWS / 2 || total_acc_rows > ACC_ROWS / 2) {
    printf("LOOP_WS bounds were too large for double-buffering\n");
    exit(1);
  }

  //spad id에 따라 a, b, c matrix 모두 bank 지정 가능

  uint32_t A_sp_addr_start = 0;
  uint32_t B_sp_addr_start = (BANK_NUM * BANK_ROWS / 2);
  const uint32_t D_sp_addr_start = 1 << (ADDR_LEN-1);
  uint32_t C_sp_addr_start = (3 << (ADDR_LEN-2)) | (full_C << (ADDR_LEN-3));
  if(a_spad_id != 0) A_sp_addr_start = (a_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;
  if(b_spad_id != 0) B_sp_addr_start = (b_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;
  if(c_spad_id != 0) C_sp_addr_start = (c_spad_id -1) * (BANK_NUM * BANK_ROWS) / BANK_NUM;
  if(is_resadd) {
    A_sp_addr_start = 1 << (ADDR_LEN - 1);
    B_sp_addr_start = 3 << (ADDR_LEN - 2);
    for(uint16_t i = 0; i < I; i++){
      for(uint16_t j = 0; j < J; j++){
        const uint32_t A_sp_addr = A_sp_addr_start + (i*J+j)*DIM;
        const uint32_t B_sp_addr = B_sp_addr_start + (i*J+j)*DIM;
        const uint32_t C_sp_addr = C_sp_addr_start + (i*J+j)*DIM;
        uint64_t dram_addr, cols, rows;
        dram_addr = gemmini_state.gemv_loop_ws_A + (i*gemmini_state.gemv_loop_ws_A_stride + j) * DIM * sizeof(elem_t);
        cols = DIM - (j == J-1 ? pad_J : 0);
        rows = DIM - (i == I-1 ? pad_I : 0);
        mvin(env,dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0);

        dram_addr = gemmini_state.gemv_loop_ws_B + (i*gemmini_state.gemv_loop_ws_B_stride + j) * DIM * sizeof(elem_t);
        cols = DIM - (j == J-1 ? pad_J : 0);
        rows = DIM - (i == I-1 ? pad_I : 0);
        mvin(env,dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1);
        if(gemmini_state.gemv_loop_ws_C != 0){
          const size_t sizeof_C = full_C ? sizeof(acc_t) : sizeof(elem_t);
          const uint64_t C_dram_addr = gemmini_state.gemv_loop_ws_C +
            (i*gemmini_state.gemv_loop_ws_C_stride + j) * DIM * sizeof_C;
          const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
          const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

          mvout(env,C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr);
        }
      }
    }
    return;
  }

  if (gemmini_state.gemv_loop_ws_D != 0) {
    for (uint16_t i = 0; i < I; i++) {
      for (uint16_t j = 0; j < J; j++) {
        const size_t sizeof_D = low_D ? sizeof(elem_t) : sizeof(acc_t);

        const uint64_t dram_addr = gemmini_state.gemv_loop_ws_D +
          (i * gemmini_state.gemv_loop_ws_D_stride + j) * DIM * sizeof_D;

        const uint64_t sp_addr = D_sp_addr_start + (i*J + j)*DIM;

        const uint64_t cols = DIM - (j == J-1 ? pad_J : 0);
        const uint64_t rows = DIM - (i == I-1 ? pad_I : 0);

        mvin(env,dram_addr, (rows << 48) | (cols << 32) | sp_addr, 2);
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
	// Mvin A
        if (j == 0 && gemmini_state.gemv_loop_ws_A != 0) {
          uint64_t dram_addr, cols, rows;

          if (a_transpose) {
            dram_addr = gemmini_state.gemv_loop_ws_A +
                (k*gemmini_state.gemv_loop_ws_A_stride + i) * DIM * sizeof(elem_t);
            cols = DIM - (i == I-1 ? pad_I : 0);
            rows = DIM - (k == K-1 ? pad_K : 0);
          } else {
            dram_addr = gemmini_state.gemv_loop_ws_A +
                (i*gemmini_state.gemv_loop_ws_A_stride + k) * DIM * sizeof(elem_t);
            cols = DIM - (k == K-1 ? pad_K : 0);
            rows = DIM - (i == I-1 ? pad_I : 0);
          }
	 printf("MVIN - A\n"); 
         mvin(env,dram_addr, (rows << 48) | (cols << 32) | A_sp_addr, 0);
       	}
	
        // Mvin B
        if (i == 0 && gemmini_state.gemv_loop_ws_B != 0) {
          uint64_t dram_addr, cols, rows;
          //dram_addr 계산하는 방식 유의!(k와 j 바뀌어 있음-> 미리 k번째 블록이 가로에 위치하도록 정렬해둘 것이기 때문), pad 값 상관없이 다 가져옴, execute에서 pad 고려
          if (b_transpose) {
            dram_addr = gemmini_state.gemv_loop_ws_B +
                (k*gemmini_state.gemv_loop_ws_B_stride + j) * DIM * sizeof(elem_t);
            cols = DIM;
            rows = DIM;
          } else {
            dram_addr = gemmini_state.gemv_loop_ws_B +
                (j*gemmini_state.gemv_loop_ws_B_stride + k) * DIM * sizeof(elem_t);
            cols = DIM;
            rows = DIM;
          }
	  printf("MVIN - B\n");
          mvin(env,dram_addr, (rows << 48) | (cols << 32) | B_sp_addr, 1);
        }
        // Compute
        if(!is_resadd) {
          uint32_t pre_sp_addr = i == 0 ? B_sp_addr : GARBAGE_ADDR;
          uint32_t out_sp_addr = C_sp_addr;

          if (!ex_accumulate && k == 0 && c_spad_id == 0) {
            out_sp_addr &= ~(1 << (ADDR_LEN-2));
          }

          const uint64_t A_cols = DIM - (k == K - 1 ? pad_K : 0);
          const uint64_t A_rows = DIM - (i == I - 1 ? pad_I : 0);
          const uint64_t B_cols = DIM - (j == J - 1 ? pad_J : 0);
          const uint64_t B_rows = DIM - (k == K - 1 ? pad_K : 0);
          const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
          const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);

          preload((B_rows << 48) | (B_cols << 32) | pre_sp_addr,
              (C_rows << 48) | (C_cols << 32) | out_sp_addr);
          if (c_spad_id != 0 && k != K-1){                                         /*결과 값 재사용을 하고, K가 마지막이 아닌 경우*/
            compute((uint64_t)1 << 63 | (A_rows << 48) | (A_cols << 32) | A_sp_addr,         /* compute 명령어의 MSB에 1을 넘겨서 결과를 bias buffer에 누적한다.*/
                          ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0);
          }
          else {
            compute((A_rows << 48) | (A_cols << 32) | A_sp_addr,
              ((uint64_t)DIM << 48) | ((uint64_t)DIM << 32) | GARBAGE_ADDR, i == 0);
          }
        }
        // Move-out C
        if (gemmini_state.gemv_loop_ws_C != 0 && k == K-1) {
          const size_t sizeof_C = full_C && c_spad_id == 0 ? sizeof(acc_t) : sizeof(elem_t);

          if ((act != LAYERNORM) && (act != SOFTMAX) && (c_spad_id == 0)) {
            const uint64_t C_dram_addr = gemmini_state.gemv_loop_ws_C +
                (i*gemmini_state.gemv_loop_ws_C_stride + j) * DIM * sizeof_C;

            const uint64_t C_cols = DIM - (j == J - 1 ? pad_J : 0);
            const uint64_t C_rows = DIM - (i == I - 1 ? pad_I : 0);
            mvout(env,C_dram_addr, (C_rows << 48) | (C_cols << 32) | C_sp_addr);
          }

          else if (((act ==LAYERNORM) || (act == SOFTMAX)) && j == J - 1){
            //printf("i : %d, j : %d, k : %d,act : %d, mvout start\n", i,j,k,act);
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
                  helper_gemmini_config(env,(1 << 17) | (stat_id << 8) | 3, 0);
                  const size_t r = row + stat_id;

                  for (size_t jj = 0; jj < J; jj++) {
                    uint32_t norm_C_sp_addr = C_sp_addr_start + (i*J + jj)*DIM + r;
                    if (jj + 1 == J) {
                      norm_C_sp_addr |= (norm_cmds[cmd][1] << 26); // Final mean/inv-std-dev calculation
                    } else {
                      norm_C_sp_addr |= (norm_cmds[cmd][0] << 26); // Accumulate sum/variance
                    }

                    const uint64_t C_dram_addr = gemmini_state.gemv_loop_ws_C +
                      (i*gemmini_state.gemv_loop_ws_C_stride + jj) * DIM * sizeof_C +
                      r * gemmini_state.gemv_loop_ws_C_stride * sizeof_C;

                    const size_t cols = DIM - (jj + 1 == J ? pad_J : 0);

                    // gemmini_extended_mvout(C_dram_addr, norm_C_sp_addr, cols, 1);
                    mvout(env,C_dram_addr, ((uint64_t)1 << 48) | (cols << 32) | norm_C_sp_addr);
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
