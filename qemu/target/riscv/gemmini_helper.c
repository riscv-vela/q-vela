#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "trace.h"
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "hw/misc/gemmini/gemmini.h"
#include "hw/misc/gemmini/gemmini_manager.h"

GemminiState *riscv_cpu_get_gemmini(CPURISCVState *env);

/*
 * Returns a pointer to a Gemmini.
 * For now, only a single global GemminiState is supported.
 * Prototype for Gemmini cluster.
 * you can Select Gemmini by ID
 */
GemminiState *riscv_cpu_get_gemmini(CPURISCVState *env) {
    CPUState *cpu = env_cpu(env);
    uint32_t cpu_index = cpu->cpu_index;
    uint32_t gemmini_count = gemmini_registry_count();
    GemminiState *gm = gemmini_registry_get(cpu_index % gemmini_count);
    return gm;
    //return global_gemmini_state;
}

/* Callback function for Gemmini memory read, as DMA */
static uint64_t gemmini_mem_read_cb(void *opaque, uint64_t addr, uint32_t size) {
    CPURISCVState *cpu = (CPURISCVState *)opaque;
    switch (size){
    case 1: return cpu_ldub_data(cpu, addr);
    case 2: return cpu_lduw_data(cpu, addr);
    case 4: return cpu_ldl_data(cpu, addr);
    case 8: return cpu_ldq_data(cpu, addr);
    default:
        qemu_log("gemmini_mem_read_cb: unsupported size %u\n", size);
        return 0;
    }
}

/* Callback function for Gemmini memory write, as DMA */
static void gemmini_mem_write_cb(void *opaque, uint64_t addr, uint64_t data, uint32_t size) {
    CPURISCVState *cpu = (CPURISCVState *)opaque;
    switch (size){
    case 1: cpu_stb_data(cpu, addr, data); break;
    case 2: cpu_stw_data(cpu, addr, data); break;
    case 4: cpu_stl_data(cpu, addr, data); break;
    case 8: cpu_stq_data(cpu, addr, data); break;
    default:
        qemu_log("gemmini_mem_write_cb: unsupported size %u\n", size);
    }
}

/* Universal RoCC helper function, Will be decoded in Gemmini itself. */
void helper_rocc_exec(CPURISCVState *env,
                      uint32_t funct7,
                      uint32_t funct3,
                      target_ulong rs1,
                      target_ulong rs2)
{
    qemu_log("### helper_rocc_exec funct7=0x%x funct3=0x%x rs1=0x%lx rs2=0x%lx\n",
             funct7, funct3, rs1, rs2);

    GemminiState *gm = riscv_cpu_get_gemmini(env);
    if (!gm) {
        qemu_log("### Gemmini not attached (ignoring RoCC)\n");
        return;
    }

    /* CPUState is Passed on right now but not used in gemmini_exec_cmd */
    CPUState *cpu = env_cpu(env);

    gm->mem_opaque = env;
    gm->mem_read = gemmini_mem_read_cb;
    gm->mem_write = gemmini_mem_write_cb;

    qemu_log("Calling Gemmini #%d exec cmd from core %d\n", gm->id, cpu->cpu_index);

    gemmini_exec_cmd(gm, funct7, funct3, rs1, rs2, cpu);
}
