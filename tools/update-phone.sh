#!/bin/sh
# Install the newest camellia packages on a running phone (Linux, macOS, Windows Git Bash/WSL).
# Needs ssh, scp and fastboot on this machine. The packages come from a local directory or are
# fetched from the build host first. If /boot/boot.img changed, it is flashed to boot_b with
# fastboot (boot_b is write-protected while the system runs); otherwise the phone just reboots.
#
# Usage:  update-phone.sh PHONE [BUILD_HOST]
#   PHONE       user@address of the phone, e.g. bjozsef@172.16.42.1 (USB) or its Wi-Fi address
#   BUILD_HOST  user@address of the build host; omit when running on the build host itself
# Environment: PACKAGES (default: firmware, device and linux packages), PKGDIR (where pmbootstrap
#   puts packages on the build host, default ~/.local/var/pmbootstrap/packages/edge/aarch64)
set -eu

PHONE=${1:?usage: update-phone.sh user@phone [user@buildhost]}
BUILD_HOST=${2:-}
PACKAGES=${PACKAGES:-"firmware-xiaomi-camellia device-xiaomi-camellia linux-xiaomi-camellia"}
PKGDIR=${PKGDIR:-'~/.local/var/pmbootstrap/packages/edge/aarch64'}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

newest() {
    # newest file of package $1 in PKGDIR, locally or on the build host
    if [ -n "$BUILD_HOST" ]; then
        ssh "$BUILD_HOST" "ls -t $PKGDIR/$1-[0-9]*.apk 2>/dev/null | head -1"
    else
        eval "ls -t $PKGDIR/$1-[0-9]*.apk 2>/dev/null | head -1"
    fi
}

files=""
for pkg in $PACKAGES; do
    src=$(newest "$pkg")
    [ -n "$src" ] || { echo "no $pkg package found" >&2; exit 1; }
    name=$(basename "$src")
    echo "==> $name"
    if [ -n "$BUILD_HOST" ]; then
        scp -q "$BUILD_HOST:$src" "$WORK/$name"
    else
        cp "$src" "$WORK/$name"
    fi
    files="$files $WORK/$name"
done

echo "==> copying to the phone"
ssh "$PHONE" 'rm -rf phone-update && mkdir phone-update'
# shellcheck disable=SC2086
scp -q $files "$PHONE:phone-update/"

before=$(ssh "$PHONE" 'sha256sum /boot/boot.img' | cut -d' ' -f1)
echo "==> installing (sudo password of the phone user)"
ssh -t "$PHONE" 'sudo apk add --no-interactive --allow-untrusted ./phone-update/*.apk && rm -rf phone-update'
after=$(ssh "$PHONE" 'sha256sum /boot/boot.img' | cut -d' ' -f1)

if [ "$before" = "$after" ]; then
    echo "==> boot.img unchanged, rebooting"
    ssh -t "$PHONE" 'sudo reboot' || true
    exit 0
fi

echo "==> boot.img changed, fetching it"
scp -q "$PHONE:/boot/boot.img" "$WORK/boot.img"
[ "$(sha256sum "$WORK/boot.img" 2>/dev/null | cut -d' ' -f1)" = "$after" ] || \
    [ "$(shasum -a 256 "$WORK/boot.img" 2>/dev/null | cut -d' ' -f1)" = "$after" ] || \
    { echo "boot.img hash mismatch after copy" >&2; exit 1; }

echo "==> rebooting the phone: HOLD VOLUME DOWN until the fastboot screen appears"
ssh -t "$PHONE" 'sudo reboot' || true
echo "    waiting for fastboot..."
until fastboot devices | grep -q fastboot; do sleep 2; done
fastboot flash boot_b "$WORK/boot.img"
fastboot reboot
echo "==> done"
