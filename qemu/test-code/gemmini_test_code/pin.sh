#!/bin/bash
#
# pin_threads.sh — Pin QEMU vCPU and Gemmini worker threads to physical cores
#
# Usage: ./pin_threads.sh [logfile]
#
# Default logfile: qemu_thread.log
# Layout:
#   vCPU  0,1,2,3   → physical cores 0,1,2,3
#   Worker 0,1,2,3  → physical cores 4,5,6,7
#

LOGFILE="${1:-qemu.log}"

if [[ ! -f "$LOGFILE" ]]; then
    echo "ERROR: log file '$LOGFILE' not found."
    echo "Start QEMU first with: -D qemu_thread.log"
    exit 1
fi

# ---- Parse TIDs from log ----
# Log lines:
#   qemu_thread_start called for CPU 0/TCG at TID 606793
#   qemu_thread_start called for gemmini-worker at TID 606797

declare -a VCPU_TIDS
declare -a GEMMINI_TIDS

while IFS= read -r line; do
    if [[ "$line" =~ "qemu_thread_start called for CPU "([0-9]+)"/TCG at TID "([0-9]+) ]]; then
        cpu_id="${BASH_REMATCH[1]}"
        tid="${BASH_REMATCH[2]}"
        VCPU_TIDS[$cpu_id]=$tid
    elif [[ "$line" =~ "qemu_thread_start called for gemmini-worker at TID "([0-9]+) ]]; then
        GEMMINI_TIDS+=("${BASH_REMATCH[1]}")
    fi
done < "$LOGFILE"

echo "=== Parsed TIDs from $LOGFILE ==="
for i in "${!VCPU_TIDS[@]}"; do
    echo "  vCPU $i   → TID ${VCPU_TIDS[$i]}"
done
for i in "${!GEMMINI_TIDS[@]}"; do
    echo "  Gemmini $i → TID ${GEMMINI_TIDS[$i]}"
done
echo ""

NUM_VCPUS=${#VCPU_TIDS[@]}
NUM_GEMMINIS=${#GEMMINI_TIDS[@]}

if [[ $NUM_VCPUS -eq 0 ]]; then
    echo "ERROR: No vCPU TIDs found in log. Check log format."
    exit 1
fi
if [[ $NUM_GEMMINIS -eq 0 ]]; then
    echo "ERROR: No gemmini-worker TIDs found in log. Check log format."
    exit 1
fi

pin_thread() {
    local tid=$1
    local core=$2
    local label=$3

    if ! kill -0 "$tid" 2>/dev/null; then
        echo "  [SKIP] $label TID $tid — thread not running"
        return
    fi

    taskset -cp "$core" "$tid" > /dev/null 2>&1
    if [[ $? -eq 0 ]]; then
        echo "  [OK]   $label TID $tid → core $core"
    else
        echo "  [FAIL] $label TID $tid → core $core (taskset failed — core out of range?)"
    fi
}

echo "=== Pinning vCPU threads ==="
for i in "${!VCPU_TIDS[@]}"; do
    pin_thread "${VCPU_TIDS[$i]}" "$i" "vCPU $i  "
done

echo ""
echo "=== Pinning Gemmini threads ==="
for i in "${!GEMMINI_TIDS[@]}"; do
    core=$((NUM_VCPUS + i))
    pin_thread "${GEMMINI_TIDS[$i]}" "$core" "Gemmini $i"
done

echo ""
echo "Done. Verify with: taskset -cp <TID>"
