#!/bin/bash

export WORKSPACE =`pwd`
export GCC5_RISCV64_PREFIX=riscv64-linux-gnu-
export PACKAGES_PATH=$WORKSPACE/edk2
export EDK_TOOLS_PATH=$WORKSPACE/edk2/BaseTools
source $WORKSPACE/edk2/edksetup.sh --reconfig

cd e-test
patch -p1 -f < $WORKSPACE/patch/edk2_reri.patch

# after patching
	#make -C edk2/BaseTools
	#source edk2/edksetup.sh BaseTools
	#build -a RISCV64 --buildtarget RELEASE -p OvmfPkg/RiscVVirt/RiscVVirtQemu.dsc -t GCC5
	#truncate -s 32M /root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd
	#truncate -s 32M /root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd
