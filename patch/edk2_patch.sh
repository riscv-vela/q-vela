#!/bin/bash

export WORKSPACE=`pwd`

if [[ ! -d "$WORKSPACE/edk2" ]]; then
	echo "You have to use patching script in outside of patch directory"
	exit 1
fi

cd $WORKSPACE/edk2

echo "🔧 Applying patch..."
patch -p1 -f < $WORKSPACE/patch/edk2_reri.patch

echo "✅ Patch applied successfully."

# after patching
	# export GCC5_RISCV64_PREFIX=riscv64-linux-gnu-
	# export PACKAGES_PATH=$WORKSPACE/edk2
	# export EDK_TOOLS_PATH=$WORKSPACE/edk2/BaseTools
	# source $WORKSPACE/edk2/edksetup.sh --reconfig
	#make -C edk2/BaseTools
	#source edk2/edksetup.sh BaseTools
	#build -a RISCV64 --buildtarget RELEASE -p OvmfPkg/RiscVVirt/RiscVVirtQemu.dsc -t GCC5
	#truncate -s 32M /root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_CODE.fd
	#truncate -s 32M /root/Build/RiscVVirtQemu/RELEASE_GCC5/FV/RISCV_VIRT_VARS.fd
