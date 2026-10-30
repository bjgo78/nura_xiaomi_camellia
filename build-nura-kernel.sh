#!/bin/bash
# Build the camellia kernel (branch "nura") for Nura.
#
# Toolchain: $NURA_DIR/toolchain (tools/setup-toolchain.sh): clang r383902 with LTO + CFI,
# GNU binutils 4.9 for arm64 and the 32-bit compat vDSO, mkdtboimg.py. Host tools: make,
# flex, bison, perl, python3. Make runs with a clean environment (env -i), so a shell with
# an Android build environment loaded (OUT, TARGET_*, ...) gives the same result.
# Output in $NURA_DIR/dist: Image.gz, dtb.img (DT table), the config, kernel.release,
# modules.builtin and the kernel commit.
set -euo pipefail

NDIR=${NURA_DIR:-$HOME/android/nura}
KSRC=${NURA_KERNEL:-$NDIR/kernel}
TC=${NURA_TOOLCHAIN:-$NDIR/toolchain}
KOBJ=$NDIR/KERNEL_OBJ
KDIST=$NDIR/dist
FRAGMENT=arch/arm64/configs/camellia_nura.config
DTB_CFG_LINE=mediatek/mt6833.dtb
# Same variant the Lineage build uses; several MTK connectivity Makefiles check it.
BUILD_VARIANT=userdebug

CLANG=$TC/clang-r383902/bin
GCC64=$TC/aarch64-linux-android-4.9/bin
GCC32=$TC/arm-linux-androideabi-4.9/bin
MKDTBOIMG=$TC/mkdtboimg.py

for f in "$CLANG/clang" "$GCC64/aarch64-linux-android-as" "$GCC32/arm-linux-androidkernel-as" \
         "$MKDTBOIMG" "$KSRC/$FRAGMENT"; do
    [ -e "$f" ] || { echo "missing: $f (run tools/setup-toolchain.sh / clone the kernel)" >&2; exit 1; }
done
for t in make flex bison perl python3; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; exit 1; }
done

branch=$(git -C "$KSRC" rev-parse --abbrev-ref HEAD)
[ "$branch" = nura ] || echo "warning: $KSRC is on '$branch', not 'nura'" >&2

CLEAN_ENV=(env -i
    HOME="$HOME" TERM="${TERM:-dumb}" LANG=C.UTF-8
    PATH="$CLANG:$GCC64:$GCC32:/usr/local/bin:/usr/bin:/bin"
    LD_LIBRARY_PATH="$CLANG/../lib64")

MAKE_ARGS=(-j"$(nproc)" -C "$KSRC" O="$KOBJ" ARCH=arm64
    HOSTCFLAGS="-I/usr/include -I/usr/include/x86_64-linux-gnu"
    HOSTLDFLAGS="-L/usr/lib/x86_64-linux-gnu -L/usr/lib64 -fuse-ld=lld"
    HOSTCC="$CLANG/clang" HOSTCXX="$CLANG/clang++"
    CC=clang LD="$CLANG/ld.lld" AR="$CLANG/llvm-ar" NM=llvm-nm OBJCOPY=llvm-objcopy
    LLVM=1 LLVM_IAS=1
    TARGET_PRODUCT=camellian TARGET_BUILD_VARIANT="$BUILD_VARIANT"
    CROSS_COMPILE="$GCC64/aarch64-linux-android-"
    CROSS_COMPILE_ARM32="$GCC32/arm-linux-androidkernel-"
    CROSS_COMPILE_COMPAT="$GCC32/arm-linux-androidkernel-"
    CLANG_TRIPLE=aarch64-linux-gnu-)

kmake() { "${CLEAN_ENV[@]}" make "${MAKE_ARGS[@]}" "$@"; }

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

kmake Image.gz dtbs modules.builtin

# dtb.img: DT table with one entry, the format the bootloader expects (androidboot.dtb_idx)
echo "$DTB_CFG_LINE" > "$KOBJ/dtb.cfg"
"${CLEAN_ENV[@]}" python3 "$MKDTBOIMG" cfg_create "$KDIST/dtb.img" "$KOBJ/dtb.cfg" --dtb-dir "$KOBJ/arch/arm64/boot/dts"

cp "$KOBJ/arch/arm64/boot/Image.gz" "$KDIST/Image.gz"
cp "$KOBJ/.config" "$KDIST/config-xiaomi-camellia.aarch64"
cp "$KOBJ/include/config/kernel.release" "$KDIST/kernel.release"
cp "$KOBJ/modules.builtin" "$KDIST/modules.builtin"
git -C "$KSRC" rev-parse HEAD > "$KDIST/kernel-commit"

cd "$KDIST"
sha256sum Image.gz dtb.img config-xiaomi-camellia.aarch64
echo "kernel commit: $(cat kernel-commit)"
echo "kernel release: $(cat kernel.release)"
echo "done: $KDIST"
