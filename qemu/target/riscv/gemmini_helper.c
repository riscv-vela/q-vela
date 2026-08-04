#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "trace.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include <time.h>

#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/misc/gemmini/gemmini.h"
#include "hw/misc/gemmini/gemmini_manager.h"

#include "qemu/timer.h"
#include "qemu/thread.h"

/* Erase below for Debugging */
// #undef qemu_log
// #define qemu_log(...) do {} while (0)

typedef enum GemminiPolicy { MODULO, RANDOM, IDLE_FIRST } GemminiPolicy;

/*
 * Per-CPU Gemmini binding table for IDLE_FIRST policy.
 *
 * A computation spans multiple RoCC instructions (config → mvin → compute).
 * All instructions in one computation MUST go to the same Gemmini.
 * binding[cpu_idx] stores which Gemmini this CPU is currently using.
 * -1 means unbound (no computation in progress).
 * The binding is released when the CPU issues a fence.
 *
 * NOTE: Correctness assumes no guest thread migration during a computation.
 *       Use sched_setaffinity in the guest if strict correctness is required.
 */
#define MAX_CPUS 64
static int cpu_gemmini_binding[MAX_CPUS];
static bool cpu_binding_initialized = false;

static void ensure_binding_initialized(void) {
    if (!cpu_binding_initialized) {
        for (int i = 0; i < MAX_CPUS; i++)
            cpu_gemmini_binding[i] = -1;
        cpu_binding_initialized = true;
    }
}

/*
 * Try to find and lock an idle Gemmini using trylock.
 * Returns the Gemmini with worker_lock HELD, or NULL if all are busy.
 * On success, also updates cpu_gemmini_binding[cpu_idx].
 */
static GemminiState *find_idle_gemmini_and_lock(uint32_t cpu_idx) {
    uint32_t count = gemmini_registry_count();
    for (uint32_t i = 0; i < count; i++) {
        GemminiState *gm = gemmini_registry_get(i);
        if (!gm) continue;
        if (qemu_mutex_trylock(&gm->worker_lock) == 0) {
            if (!gm->has_work && !gm->worker_running) {
                /* Idle and locked — claim it */
                cpu_gemmini_binding[cpu_idx] = (int)i;
                qemu_log("[IDLE_FIRST] CPU %d bound to Gemmini %d\n", cpu_idx, i);
                return gm;  /* Return with lock held */
            }
            qemu_mutex_unlock(&gm->worker_lock);
        }
    }
    return NULL;
}

GemminiState *riscv_cpu_get_gemmini(CPURISCVState *env, GemminiPolicy policy);

/*
 * Returns a pointer to a Gemmini based on the selection policy.
 * MODULO:     cpu_index % gemmini_count  (deterministic, 1:1 mapping)
 * RANDOM:     random Gemmini (per-instruction — use only for stateless ops)
 * IDLE_FIRST: find any idle Gemmini; fall back to MODULO if all busy
 *             (binding is managed externally by helper_rocc_exec)
 */
GemminiState *riscv_cpu_get_gemmini(CPURISCVState *env, GemminiPolicy policy) {
    CPUState *cpu = env_cpu(env);
    uint32_t cpu_index = cpu->cpu_index;
    uint32_t gemmini_count = gemmini_registry_count();
    if (gemmini_count == 0) {
        qemu_log("No Gemmini devices registered!\n");
        return NULL;
    }

    if (policy == MODULO) {
        return gemmini_registry_get(cpu_index % gemmini_count);
    } else if (policy == RANDOM) {
        uint32_t random_id = g_random_int_range(0, gemmini_count);
        return gemmini_registry_get((int)random_id);
    } else if (policy == IDLE_FIRST) {
        ensure_binding_initialized();
        int bound = cpu_gemmini_binding[cpu_index];
        if (bound >= 0) {
            /* Already bound: return the same Gemmini for this computation */
            return gemmini_registry_get(bound);
        }
        /* Not yet bound: caller (helper_rocc_exec) will use
         * find_idle_gemmini_and_lock() to atomically pick and bind */
        return NULL;
    } else {
        qemu_log("Unknown Gemmini selection policy!\n");
        return NULL;
    }
}

/* Callback function for Gemmini memory read, as DMA */
/* Uses cpu_memory_rw_debug to allow safe call from non-vCPU threads (worker thread) */
static uint64_t gemmini_mem_read_cb(void *opaque, uint64_t addr, uint32_t size) {
    CPUState *cpu = (CPUState *)opaque;
    uint64_t result = 0;
    cpu_memory_rw_debug(cpu, (target_ulong)addr, (void *)&result, size, false);
    return result;
}

/* Callback function for Gemmini memory write, as DMA */
/* Uses cpu_memory_rw_debug to allow safe call from non-vCPU threads (worker thread) */
static void gemmini_mem_write_cb(void *opaque, uint64_t addr, uint64_t data, uint32_t size) {
    CPUState *cpu = (CPUState *)opaque;
    cpu_memory_rw_debug(cpu, (target_ulong)addr, (void *)&data, size, true);
}

/* Universal RoCC helper function, Will be decoded in Gemmini itself. */
// void helper_rocc_exec(CPURISCVState *env,
//                       uint32_t funct7,
//                       uint32_t funct3,
//                       target_ulong rs1,
//                       target_ulong rs2)
// {
//     qemu_log("### helper_rocc_exec funct7=0x%x funct3=0x%x rs1=0x%lx rs2=0x%lx\n",
//              funct7, funct3, rs1, rs2);

//     CPUState *cpu = env_cpu(env);
//     GemminiState *gm = riscv_cpu_get_gemmini(env);
//     if (!gm) {
//         qemu_log("### Gemmini not attached (ignoring RoCC)\n");
//         return;
//     }
//     qemu_log("CPU %d trying to acquire Gemmini #%d\n", cpu->cpu_index, gm->id);
//     int64_t waitstart = cpu_get_host_ticks();

//     /* Multiple CPUs shouldn't access Gemmini simultaneously, requiring a lock */
//     qemu_mutex_lock(&gm->gemmini_lock);
//     gm->busy = true;
//     gm->locked_by_cpu = cpu->cpu_index;

//     gm->mem_opaque = env;
//     gm->mem_read = gemmini_mem_read_cb;
//     gm->mem_write = gemmini_mem_write_cb;

//     int64_t waitend = cpu_get_host_ticks();
//     qemu_log("Obtained Gemmini #%d from core %d (cycles: %ld)\n", gm->id, cpu->cpu_index, waitend - waitstart);


//     int64_t start = cpu_get_host_ticks();
//     /* Actual Execution happens here (definition at gemmini.c) */
//     gemmini_exec_cmd(gm, funct7, funct3, rs1, rs2, cpu);
//     int64_t end = cpu_get_host_ticks();
//     qemu_log("CPU %d completed Gemmini command (cycles: %ld)\n", 
//              cpu->cpu_index, end - start);

//     /* Release the lock after the command execution is complete */
//     gm->busy = false;
//     gm->locked_by_cpu = -1;
//     qemu_mutex_unlock(&gm->gemmini_lock);
// }

/* Thread Enqueuing Helper — uses IDLE_FIRST policy */
void helper_rocc_exec(CPURISCVState *env,
                      uint32_t funct7,
                      uint32_t funct3,
                      target_ulong rs1,
                      target_ulong rs2)
{
    qemu_log("[HELPER CALL] funct7=0x%x rs1=0x%lx rs2=0x%lx\n",
             funct7, rs1, rs2);

    CPUState *cpu = env_cpu(env);
    uint32_t cpu_index = cpu->cpu_index;
    ensure_binding_initialized();

    /* ---- FENCE ---- */
    if (funct7 == 0b0111111) {
        int bound = cpu_gemmini_binding[cpu_index];
        if (bound < 0) {
            /* No computation in progress — nothing to wait for */
            qemu_log("[FENCE NOP][Core %d] no binding\n", cpu_index);
            return;
        }
        GemminiState *gm = gemmini_registry_get(bound);
        if (!gm) return;

        qemu_mutex_lock(&gm->worker_lock);
        qemu_log("[WAITING FENCE][Core %d-Gemmini %d]\n", cpu_index, gm->id);
        while ((gm->has_work || gm->worker_running) && !gm->stopped) {
            qemu_cond_wait(&gm->worker_cond, &gm->worker_lock);
        }
        qemu_log("[FENCE OPEN][Core %d-Gemmini %d] → binding released\n",
                 cpu_index, gm->id);
        cpu_gemmini_binding[cpu_index] = -1;   /* Release binding */
        qemu_mutex_unlock(&gm->worker_lock);
        return;
    }

    /* ---- Non-fence command ---- */

    GemminiState *gm = NULL;

    int bound = cpu_gemmini_binding[cpu_index];
    if (bound >= 0) {
        /* Already bound to a Gemmini for this computation */
        gm = gemmini_registry_get(bound);
        qemu_mutex_lock(&gm->worker_lock);
    } else {
        /* Not yet bound: try to find an idle Gemmini atomically */
        gm = find_idle_gemmini_and_lock(cpu_index);

        if (!gm) {
            /* All Gemminis busy — fall back to MODULO and wait */
            uint32_t count = gemmini_registry_count();
            gm = gemmini_registry_get(cpu_index % count);
            cpu_gemmini_binding[cpu_index] = (int)(cpu_index % count);
            qemu_log("[IDLE_FIRST FALLBACK] CPU %d → Gemmini %d (all busy)\n",
                     cpu_index, cpu_index % count);
            qemu_mutex_lock(&gm->worker_lock);
        }
    }

    if (!gm) {
        qemu_log("### Gemmini not attached (ignoring RoCC)\n");
        return;
    }

    qemu_log("[MUTEX LOCKED][Core %d-Gemmini %d] funct7=0x%x rs1=0x%lx rs2=0x%lx\n",
             cpu_index, gm->id, funct7, rs1, rs2);

    while ((gm->has_work || gm->worker_running) && !gm->stopped) {
        qemu_cond_wait(&gm->worker_cond, &gm->worker_lock);
    }

    gm->mem_opaque = env_cpu(env);
    gm->mem_read   = gemmini_mem_read_cb;
    gm->mem_write  = gemmini_mem_write_cb;

    gm->cmd.funct7 = funct7;
    gm->cmd.funct3 = funct3;
    gm->cmd.rs1    = rs1;
    gm->cmd.rs2    = rs2;
    gm->has_work   = true;
    qemu_log("[WORK ENQUEUED][Core %d-Gemmini %d] funct7=0x%x rs1=0x%lx rs2=0x%lx\n",
             cpu_index, gm->id, funct7, rs1, rs2);
    qemu_cond_signal(&gm->worker_cond);

    /* For commands that access guest DRAM via cpu_memory_rw_debug, block the vCPU
     * until the worker finishes — otherwise the worker's DRAM read/write races with
     * vCPU TLB modifications, giving non-deterministic, partially-read operands
     * (output depends on timing and on residual state from prior workloads).
     * DRAM-accessing funct7: mvin2=1, mvin=2, mvout=3, mvin3=14(0xe),
     * loop_conv_ws execute=15(0xf). compute/preload touch only spad/acc, no sync. */
    if (funct7 == 0x1 || funct7 == 0x2 || funct7 == 0x3 ||
        funct7 == 0xe || funct7 == 0xF) {
        while ((gm->has_work || gm->worker_running) && !gm->stopped) {
            qemu_cond_wait(&gm->worker_cond, &gm->worker_lock);
        }
        qemu_log("[SYNC WAIT DONE][Core %d-Gemmini %d] funct7=0x%x\n",
                 cpu_index, gm->id, funct7);
    }

    qemu_mutex_unlock(&gm->worker_lock);
}