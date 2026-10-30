#!/bin/bash
# Copy the Nura packages from this repo into pmbootstrap's pmaports checkout
# (device/downstream), add the kernel build output from ~/android/nura/dist and
# update the checksums there. Binaries and checksums are not committed.
set -euo pipefail

REPO=$(cd "$(dirname "$0")" && pwd)
NDIR=${NURA_DIR:-$HOME/android/nura}
DIST=$NDIR/dist
APORTS=$(pmbootstrap config aports)
DEST=$APORTS/device/downstream
PKGS=(firmware-xiaomi-camellia device-xiaomi-camellia linux-xiaomi-camellia)

[ -d "$DEST" ] || { echo "missing $DEST (pmaports not on edge?)" >&2; exit 1; }
for f in Image.gz dtb.img config-xiaomi-camellia.aarch64 kernel.release modules.builtin; do
    [ -e "$DIST/$f" ] || { echo "missing $DIST/$f (run build-nura-kernel.sh)" >&2; exit 1; }
done
[ -e "$REPO/packages/device-xiaomi-camellia/APKBUILD" ] || { echo "missing device APKBUILD in repo" >&2; exit 1; }

for p in "${PKGS[@]}"; do
    rm -rf "${DEST:?}/$p"
    mkdir -p "$DEST/$p"
    cp -a "$REPO/packages/$p/." "$DEST/$p/"
done
cp "$DIST/Image.gz" "$DIST/dtb.img" "$DIST/config-xiaomi-camellia.aarch64" "$DIST/kernel.release" \
    "$DIST/modules.builtin" \
    "$DEST/linux-xiaomi-camellia/"

# Give every kernel build its own package version (4.14.186_p<build time>), so apk
# always sees a newer kernel package and upgrades it in the rootfs.
stamp=$(date -u -r "$DIST/Image.gz" +%Y%m%d%H%M%S)
sed -i "s/^pkgver=.*/pkgver=4.14.186_p$stamp/" "$DEST/linux-xiaomi-camellia/APKBUILD"

for p in "${PKGS[@]}"; do
    pmbootstrap checksum "$p"
done
echo "synced to $DEST"
echo "kernel: $(cat "$DIST/kernel.release"), commit $(cat "$DIST/kernel-commit" 2>/dev/null || echo unknown), package 4.14.186_p$stamp"
