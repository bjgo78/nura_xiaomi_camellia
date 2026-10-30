#!/bin/bash
# Download the kernel toolchain into $NURA_DIR/toolchain (default ~/android/nura/toolchain):
#   clang-r383902               AOSP clang 11.0.1 (android12-release), the compiler the MTK
#                               4.14 code with LTO + CFI was validated with
#   aarch64-linux-android-4.9   GNU binutils for arm64 (LineageOS prebuilt, lineage-19.1)
#   arm-linux-androideabi-4.9   GNU binutils for the 32-bit compat vDSO (incl. the
#                               arm-linux-androidkernel-* names the kernel build uses)
#   mkdtboimg.py                builds the DT table image (dtb.img)
# Idempotent: parts that are already present and valid are skipped.
# A clang archive downloaded elsewhere can be passed: setup-toolchain.sh /path/clang-r383902.tar.gz
set -euo pipefail

NDIR=${NURA_DIR:-$HOME/android/nura}
TC=$NDIR/toolchain
CLANG_URL=https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/android12-release/clang-r383902.tar.gz
GCC64_REPO=https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9.git
GCC32_REPO=https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9.git
GCC_BRANCH=lineage-19.1
MKDTBOIMG_URL=https://raw.githubusercontent.com/LineageOS/android_system_libufdt/lineage-22.2/utils/src/mkdtboimg.py

mkdir -p "$TC"

clang_ok() { "$TC/clang-r383902/bin/clang" --version 2>/dev/null | grep -q 'based on r383902'; }
if clang_ok; then
    echo "clang-r383902: present"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    if [ $# -ge 1 ]; then
        archive=$1
    else
        echo "clang-r383902: downloading (about 1 GB)"
        archive=$tmp/clang.tar.gz
        curl -fL --retry 3 -o "$archive" "$CLANG_URL"
    fi
    mkdir "$tmp/clang"
    tar -xzf "$archive" -C "$tmp/clang"
    if ! "$tmp/clang/bin/clang" --version | grep -q 'based on r383902'; then
        echo "error: the archive is not clang r383902" >&2
        exit 1
    fi
    rm -rf "$TC/clang-r383902"
    mv "$tmp/clang" "$TC/clang-r383902"
    echo "clang-r383902: installed"
fi

fetch_gcc() {
    # $1 repo, $2 directory, $3 binary that must exist
    if [ -x "$TC/$2/bin/$3" ]; then
        echo "$2: present"
        return
    fi
    rm -rf "$TC/$2"
    git clone -q --depth 1 -b "$GCC_BRANCH" "$1" "$TC/$2"
    [ -x "$TC/$2/bin/$3" ] || { echo "error: $2 has no bin/$3" >&2; exit 1; }
    echo "$2: installed"
}
fetch_gcc "$GCC64_REPO" aarch64-linux-android-4.9 aarch64-linux-android-as
fetch_gcc "$GCC32_REPO" arm-linux-androideabi-4.9 arm-linux-androidkernel-as

if [ -s "$TC/mkdtboimg.py" ]; then
    echo "mkdtboimg.py: present"
else
    curl -fsSL -o "$TC/mkdtboimg.py" "$MKDTBOIMG_URL"
    echo "mkdtboimg.py: installed"
fi

echo "toolchain ready in $TC"
