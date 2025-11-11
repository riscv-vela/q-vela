#!/bin/bash

export WORKSPACE =`pwd`

cd opensbi
patch -p1 -f < $WORKSPACE/patch/opensbi_reri.patch

# after patching
	# make CROSS_COMPILE=riscv64-linux-gnu- PLATFORM_RISCV_XLEN=64 PLATFORM=generic -j22
