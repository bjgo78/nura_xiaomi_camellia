#!/bin/bash
# Build the camellia kernel (branch "nura") for Nura.
#
# Uses the same toolchain and make flags as the Lineage kernel build (taken from
# out/verbose.log.gz), but only reads the Lineage *source* tree (prebuilts and
# mkdtboimg.py). Nothing from Lineage's out/ is used and nothing is written
# outside $NURA_DIR, so Lineage builds are unaffected.
#
# Only NURA_* variables are taken from the environment, and make runs with a
# clean environment (env -i), so a shell with Lineage's envsetup/breakfast
# loaded (OUT, TARGET_*, ...) gives the same result as a fresh one.
set -euo pipefail

LOS=${NURA_LOS:-$HOME/android/lineage}
NDIR=${NURA_DIR:-$HOME/android/nura}
KSRC=$NDIR/kernel
KOBJ=$NDIR/KERNEL_OBJ
KDIST=$NDIR/dist
FRAGMENT=arch/arm64/configs/camellia_nura.config
DTB_CFG_LINE=mediatek/mt6833.dtb
# Lineage builds userdebug; several MTK connectivity Makefiles check this.
BUILD_VARIANT=userdebug

case "$KOBJ/" in "$LOS"/*) echo "refusing: $KOBJ is inside the Lineage tree" >&2; exit 1 ;; esac
case "$KDIST/" in "$LOS"/*) echo "refusing: $KDIST is inside the Lineage tree" >&2; exit 1 ;; esac

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

CLEAN_ENV=(env -i
    HOME="$HOME" TERM="${TERM:-dumb}" LANG=C.UTF-8
    PATH="$CLANG:$GCC64:$LOS/prebuilts/tools-lineage/linux-x86/bin:$GCC32:/usr/local/bin:/usr/bin:/bin"
    LD_LIBRARY_PATH="$CLANG/../lib64"
    PERL5LIB="$LOS/prebuilts/tools-lineage/common/perl-base"
    BISON_PKGDATADIR="$BT/common/bison"
    HIP_PATH=none)

MAKE_ARGS=(-j"$(nproc)" -C "$KSRC" O="$KOBJ" ARCH=arm64
    HOSTCFLAGS="-I/usr/include -I/usr/include/x86_64-linux-gnu"
    HOSTLDFLAGS="-L/usr/lib/x86_64-linux-gnu -L/usr/lib64 -fuse-ld=lld"
    HOSTCC="$CLANG/clang" HOSTCXX="$CLANG/clang++"
    CC=clang LD="$CLANG/ld.lld" AR="$CLANG/llvm-ar" NM=llvm-nm OBJCOPY=llvm-objcopy
    LLVM=1 LLVM_IAS=1
    LZ4="$KBT/lz4" PAHOLE="$KBT/pahole"
    LEX="$BT/linux-x86/bin/flex" YACC="$BT/linux-x86/bin/bison" M4="$BT/linux-x86/bin/m4"
    TARGET_PRODUCT=camellian TARGET_BUILD_VARIANT="$BUILD_VARIANT"
    CROSS_COMPILE="$GCC64/aarch64-linux-android-"
    CROSS_COMPILE_ARM32="$GCC32/arm-linux-androidkernel-"
    CROSS_COMPILE_COMPAT="$GCC32/arm-linux-androidkernel-"
    CLANG_TRIPLE=aarch64-linux-gnu-)

kmake() { "${CLEAN_ENV[@]}" "$BT/linux-x86/bin/make" "${MAKE_ARGS[@]}" "$@"; }

mkdir -p "$KOBJ" "$KDIST"
echo "kernel objects: $KOBJ"

kmake camellia_stock_defconfig
(cd "$KSRC" && "${CLEAN_ENV[@]}" ARCH=arm64 bash scripts/kconfig/merge_config.sh -m -O "$KOBJ" "$KOBJ/.config" "$FRAGMENT")
kmake olddefconfig

missing=0
while IFS= read -r line; do
    case "$line" in ''|'# Nura'*) continue ;; esac
    grep -qxF "$line" "$KOBJ/.config" || { echo "not applied: $line" >&2; missing=1; }
done < "$KSRC/$FRAGMENT"
[ "$missing" -eq 0 ] || { echo "fragment options did not stick (unmet dependencies?)" >&2; exit 1; }

kmake Image.gz dtbs

# dtb.img: DT table with one entry, same as BOARD_DTB_CFG in the Lineage device tree
echo "$DTB_CFG_LINE" > "$KOBJ/dtb.cfg"
"${CLEAN_ENV[@]}" python3 "$MKDTBOIMG" cfg_create "$KDIST/dtb.img" "$KOBJ/dtb.cfg" --dtb-dir "$KOBJ/arch/arm64/boot/dts"

cp "$KOBJ/arch/arm64/boot/Image.gz" "$KDIST/Image.gz"
cp "$KOBJ/.config" "$KDIST/config-xiaomi-camellia.aarch64"
git -C "$KSRC" rev-parse HEAD > "$KDIST/kernel-commit"

cd "$KDIST"
sha256sum Image.gz dtb.img config-xiaomi-camellia.aarch64
echo "kernel commit: $(cat kernel-commit)"
LOS_DTB=$LOS/out/target/product/camellia/dtb.img
if [ -e "$LOS_DTB" ]; then
    if cmp -s dtb.img "$LOS_DTB"; then echo "dtb.img: identical to Lineage's"; else echo "dtb.img: differs from Lineage's"; fi
fi
echo "done: $KDIST"
