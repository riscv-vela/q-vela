#!/bin/bash

QEMU_DIR="../qemu"
CUR_DIR=`pwd`
cd $QEMU_DIR
$QEMU_DIR/configure --target-list=riscv64-softmmu --enable-slirp

cpu_cap = $nproc
echo "** USE $cpu_cap CPUs **"
make -j$cpu_cap

make install -j$cpu_cap

#!/usr/bin/env bash
set -euo pipefail

QEMU_DIR="../qemu"

QEMU_DIR="$(realpath "$QEMU_DIR")"
CUR_DIR="$(pwd)"

if [ ! -d "$QEMU_DIR" ]; then
  echo "Cannot find QEMU path $QEMU_DIR"
  echo "Please reset QEMU path"
  exit 1
fi

cd "$QEMU_DIR"

if command -v nproc >/dev/null 2>&1; then
  CPU_CAP="$(nproc)"
else
  CPU_CAP=4
fi

echo "** Using CPUs: $CPU_CAP  **"

echo "🔧 QEMU configure ..."
./configure --target-list=riscv64-softmmu --enable-slirp

echo "Building QEMU..."
make -j"$CPU_CAP"

echo "Installing QEMU..."
sudo make install -j"$CPU_CAP"

cd "$CUR_DIR"

echo "** Complite Installing **"

