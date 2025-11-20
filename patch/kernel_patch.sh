#!/bin/bash

CUR_DIR="$(pwd)"

if [[ -d "patch" ]]; then
    PATCH_DIR="$CUR_DIR/patch"
    cd linux || { echo "❌ linux 디렉토리 없음"; exit 1; }
else
    PATCH_DIR="$CUR_DIR"
    cd ../linux || { echo "❌ ../linux 디렉토리 없음"; exit 1; }
fi

# 패치 적용
patch -p1 -f < "$PATCH_DIR/kernel_reri.patch"

echo "✅ PATCH COMPLETE"

