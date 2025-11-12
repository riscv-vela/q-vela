#!/bin/bash

QEMU_DIR ="../qemu"
CUR_DIR = `pwd`
cd $QEMU_DIR
$QEMU_DIR/configure --target-list=riscv64-softmmu --enable-slirp

cpu_cap = $nproc
echo "** USE $cpu_cap CPUs **"
make -j$cpu_cap

make install -j$cpu_cap

