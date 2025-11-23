#!/bin/bash


CUR_DIR="$(pwd)"
TARGET_DIR="edk2"
if [[ -d "patch" ]]; then
    PATCH_DIR="$CUR_DIR/patch"
    cd $TARGET_DIR || { echo "❌ $TARGET_DIR 디렉토리 없음"; exit 1; }
else
    PATCH_DIR="$CUR_DIR"
    cd ../$TARGET_DIR || { echo "❌ ../$TARGET_DIR 디렉토리 없음"; exit 1; }
fi


echo "🔧 Applying patch..."
patch -p1 -f < "$PATCH_DIR/edk2_reri.patch"

echo "✅ Patch applied successfully."
