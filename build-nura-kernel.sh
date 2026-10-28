#!/bin/bash
# Build the camellia kernel (branch "nura") for Nura.
#
# Uses the same toolchain and make flags as the Lineage kernel build (taken from
# out/verbose.log.gz), but only reads the Lineage *source* tree (prebuilts and
# mkdtboimg.py). Nothing from Lineage's out/ is used and nothing is written
# outside $NURA, so Lineage builds are unaffected.
set -euo pipefail

LOS=${LOS:-$HOME/android/lineage}
NURA=${NURA:-$HOME/android/nura}
KSRC=${KSRC:-$NURA/kernel}
OUT=${OUT:-$NURA/KERNEL_OBJ}
DIST=${DIST:-$NURA/dist}
FRAGMENT=arch/arm64/configs/camellia_nura.config
DTB_CFG_LINE=mediatek/mt6833.dtb

CLANG=$LOS/prebuilts/clang/host/linux-x86/clang-r383902/bin
GCC64=$LOS/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin
GCC32=$LOS/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin
BT=$LOS/prebuilts/build-tools
KBT=$LOS/prebuilts/kernel-build-tools/linux-x86/bin
MKDTBOIMG=$LOS/system/libufdt/utils/src/mkdtboimg.py

for f in "$CLANG/clang" "$GCC64/aarch64-linux-android-as" "$BT/linux-x86/bin/make" \
         "$MKDTBOIMG" "$KSRC/$FRAGMENT"; do
    [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

branch=$(git -C "$KSRC" rev-parse --abbrev-ref HEAD)
[ "$branch" = nura ] || echo "warning: $KSRC is on '$branch', not 'nura'" >&2

export PATH="$CLANG:$GCC64:$LOS/prebuilts/tools-lineage/linux-x86/bin:$GCC32:$PATH"
export LD_LIBRARY_PATH="$CLANG/../lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PERL5LIB=$LOS/prebuilts/tools-lineage/common/perl-base
export BISON_PKGDATADIR=$BT/common/bison
export HIP_PATH=none

MAKE=("$BT/linux-x86/bin/make" -j"$(nproc)" -C "$KSRC" O="$OUT" ARCH=arm64
    HOSTCFLAGS="-I/usr/include -I/usr/include/x86_64-linux-gnu"
    HOSTLDFLAGS="-L/usr/lib/x86_64-linux-gnu -L/usr/lib64 -fuse-ld=lld"
    HOSTCC="$CLANG/clang" HOSTCXX="$CLANG/clang++"
    CC=clang LD="$CLANG/ld.lld" AR="$CLANG/llvm-ar" NM=llvm-nm OBJCOPY=llvm-objcopy
    LLVM=1 LLVM_IAS=1
    LZ4="$KBT/lz4" PAHOLE="$KBT/pahole"
    LEX="$BT/linux-x86/bin/flex" YACC="$BT/linux-x86/bin/bison" M4="$BT/linux-x86/bin/m4"
    TARGET_PRODUCT=camellian
    CROSS_COMPILE="$GCC64/aarch64-linux-android-"
    CROSS_COMPILE_ARM32="$GCC32/arm-linux-androidkernel-"
    CROSS_COMPILE_COMPAT="$GCC32/arm-linux-androidkernel-"
    CLANG_TRIPLE=aarch64-linux-gnu-)

mkdir -p "$OUT" "$DIST"

"${MAKE[@]}" camellia_stock_defconfig
(cd "$KSRC" && ARCH=arm64 scripts/kconfig/merge_config.sh -m -O "$OUT" "$OUT/.config" "$FRAGMENT")
"${MAKE[@]}" olddefconfig

missing=0
while IFS= read -r line; do
    case "$line" in ''|'# Nura'*) continue ;; esac
    grep -qxF "$line" "$OUT/.config" || { echo "not applied: $line" >&2; missing=1; }
done < "$KSRC/$FRAGMENT"
[ "$missing" -eq 0 ] || { echo "fragment options did not stick (unmet dependencies?)" >&2; exit 1; }

"${MAKE[@]}" Image.gz dtbs

# dtb.img: DT table with one entry, same as BOARD_DTB_CFG in the Lineage device tree
echo "$DTB_CFG_LINE" > "$OUT/dtb.cfg"
python3 "$MKDTBOIMG" cfg_create "$DIST/dtb.img" "$OUT/dtb.cfg" --dtb-dir "$OUT/arch/arm64/boot/dts"

cp "$OUT/arch/arm64/boot/Image.gz" "$DIST/Image.gz"
cp "$OUT/.config" "$DIST/config-xiaomi-camellia.aarch64"
echo "$(git -C "$KSRC" rev-parse HEAD)" > "$DIST/kernel-commit"

cd "$DIST"
sha256sum Image.gz dtb.img config-xiaomi-camellia.aarch64
echo "kernel commit: $(cat kernel-commit)"
LOS_DTB=$LOS/out/target/product/camellia/dtb.img
if [ -e "$LOS_DTB" ]; then
    cmp -s dtb.img "$LOS_DTB" && echo "dtb.img: identical to Lineage's" || echo "dtb.img: differs from Lineage's"
fi
echo "done: $DIST"
