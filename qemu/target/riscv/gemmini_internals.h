#ifndef TARGET_RISCV_GEMMINI_INTERNALS_H
#define TARGET_RISCV_GEMMINI_INTERNALS_H

#include "qemu/bitops.h"
#include "hw/registerfields.h"
#include "cpu.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
void mvin(CPURISCVState *env, uint64_t dram_addr, uint64_t sp_addr, int state_id);
void HELPER(mvin)(CPURISCVState *env, target_ulong dram_addr, target_ulong sp_addr, int state_id)
{
    // 실제 Gemmini 동작
    mvin(env, dram_addr, sp_addr, state_id);
}
void mvout(CPURISCVState *env, uint64_t dram_addr, uint64_t sp_addr);
void HELPER(mvout)(CPURISCVState *env, target_ulong dram_addr, target_ulong sp_addr)
{
    // 실제 Gemmini 동작
    mvout(env, dram_addr, sp_addr);
}
void preload(uint64_t bd_addr, uint64_t c_addr);
void HELPER(preload)(target_ulong bd_addr, target_ulong c_addr)
{
        // 실제 Gemmini 동작
    preload(bd_addr,c_addr);
}

void compute(uint64_t a_addr, uint64_t bd_addr, bool preload);
void HELPER(compute)(target_ulong a_addr, target_ulong bd_addr, int preload)
{
	// 실제 Gemmini 동작
    compute(a_addr,bd_addr,preload);
}
#endif
