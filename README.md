# Nura on Xiaomi Redmi Note 10 5G (camellia)

Downstream Nura (postmarketOS) port for camellia, used as a headless Linux server: Wi-Fi, USB
networking, wired Ethernet through a USB-C dock (OTG), charging with a charge limit, thermal protection, zram swap, Docker, and an optional
on-screen panel with a boot splash. Kept fully separate from the LineageOS branches.

The step-by-step build and flash guide is [BUILD.md](BUILD.md). This README holds the technical
details: what every part does, why, and how the problems were found.

Never commit partition backups (nvram, nvdata, nvcfg, persist, protect1/2, proinfo): they contain
the IMEI, MAC addresses and calibration data.

## Contents

1. [Repositories and layout](#repositories-and-layout)
2. [Build pipeline](#build-pipeline)
3. [Phone layout and boot](#phone-layout-and-boot)
4. [Kernel branch `nura`](#kernel-branch-nura)
5. [Packages](#packages)
6. [Hardware notes](#hardware-notes)
7. [Containers (Docker)](#containers-docker)
8. [On-screen panel](#on-screen-panel)
9. [Boot splash](#boot-splash)
10. [Troubleshooting and debugging](#troubleshooting-and-debugging)
11. [Known limitations](#known-limitations)
12. [Reference](#reference)
13. [History](#history)

## Repositories and layout

| Repo | Branch | Content |
| --- | --- | --- |
| `bjgo78/kernel_xiaomi_camellia` | `nura` | Kernel, from `lineage-22.2` at `1576725` (Xiaomi's 4.14.186 source as built for LineageOS 22.2) |
| `bjgo78/nura_xiaomi_camellia` | `main` | This repo: build scripts, packages, tools, guide |
| `bjgo78/vendor_xiaomi_camellia` | `lineage-22.2` | Source of the firmware files (pinned by commit in the firmware package) |

This repo:

| Path | Purpose |
| --- | --- |
| `BUILD.md` | Build and flash guide |
| `build-nura-kernel.sh` | Kernel build into `~/android/nura/dist` |
| `pmaports-sync.sh` | Copies the packages and the kernel output into pmbootstrap's pmaports, updates checksums |
| `tools/setup-toolchain.sh` | Downloads clang r383902, GCC 4.9 binutils and `mkdtboimg.py` |
| `tools/update-phone.sh` | Installs new packages on the phone (Linux, macOS, Git Bash/WSL) |
| `tools/update-phone.ps1` | The same for Windows PowerShell |
| `packages/linux-xiaomi-camellia` | Prebuilt kernel package |
| `packages/device-xiaomi-camellia` | deviceinfo, services, scripts, configs |
| `packages/firmware-xiaomi-camellia` | Connectivity and touchscreen firmware |
| `packages/camellia-panel` | Optional on-screen UI and the Plymouth splash theme |

Build host layout:

| Path | Content |
| --- | --- |
| `~/android/nura/kernel` | Kernel source, branch `nura` |
| `~/android/nura/nura_xiaomi_camellia` | This repo |
| `~/android/nura/toolchain` | clang r383902, binutils, `mkdtboimg.py` |
| `~/android/nura/KERNEL_OBJ`, `dist` | Kernel build output (not tracked) |
| `~/android/nura/tools/pmbootstrap` | pmbootstrap from git |
| `~/.local/var/pmbootstrap` | pmbootstrap work directory (chroots, pmaports, built packages in `packages/edge/aarch64`) |

## Build pipeline

### Toolchain

`tools/setup-toolchain.sh` downloads clang r383902 (the kernel's own compiler version), GCC 4.9
binutils and `mkdtboimg.py` into `~/android/nura/toolchain`, checks that clang reports
`based on r383902` and skips parts already there. A local `clang-r383902.tar.gz` can be passed as
the first argument when android.googlesource.com is unreachable. No Android tree is needed.

### Kernel: `build-nura-kernel.sh`

1. Generates `camellia_stock_defconfig`, merges `arch/arm64/configs/camellia_nura.config` and runs
   `olddefconfig`.
2. Checks that every line of the fragment is in `.config`. Comments are skipped except
   `# CONFIG_... is not set`, which is a real setting. A `not applied:` line means an unmet
   dependency (for example `IPVLAN` needs `NET_L3_MASTER_DEV`); the build stops.
3. Builds `Image.gz`, the dtbs and `modules.builtin` (LTO + CFI) under `env -i`, with
   `TARGET_BUILD_VARIANT=userdebug` passed explicitly. Only `NURA_DIR`, `NURA_KERNEL` and
   `NURA_TOOLCHAIN` are read from the environment, so a shell with an Android build environment
   does not leak into the build.
4. Packs `mediatek/mt6833.dtb` into a one-entry DT table (`dtb.img`, `androidboot.dtb_idx=0`).

Output in `~/android/nura/dist`: `Image.gz`, `dtb.img`, `config-xiaomi-camellia.aarch64`,
`kernel.release`, `modules.builtin`, `kernel-commit`. Uncommitted changes show as `-dirty` in
`uname -r`.

### pmbootstrap

- Installed from git (the PyPI releases are withdrawn), channel **edge** (downstream ports exist
  only there).
- OpenRC: `pmbootstrap config service_manager openrc` (pmbootstrap 3.11 renamed the old
  `systemd never` setting). The 4.14 kernel is too old for current systemd.
- Initialized twice: first with the temporary device `qemu`/`aarch64` to get a pmaports checkout,
  then, after `pmaports-sync.sh`, with `xiaomi`/`camellia`.
- `pmaports-sync.sh` copies the four packages into `pmaports/device/downstream`, adds `Image.gz`,
  `dtb.img`, the config and `modules.builtin` to the kernel package with the version
  `4.14.186_p<build time>` (every build is an upgrade for apk) and runs `pmbootstrap checksum`.
- `pmbootstrap kconfig check linux-xiaomi-camellia` passes; the `INPUT_EVDEV ... preferably m`
  line is informational.
- Every `pmbootstrap install` creates new partition UUIDs, which `boot.img` carries on its command
  line, so `boot.img` and `xiaomi-camellia.img` are flashed as a pair. `pmbootstrap export` can
  print `chroot not initialized! This is a bug!` and run mkinitfs again; the pair still matches if
  `pmos_boot_uuid`/`pmos_root_uuid` in the boot image's command line (512 bytes at offset 64)
  equal the partition UUIDs in the image (`losetup -fP --sector-size 4096`, `blkid`).
- The rootfs image uses 4096-byte sectors (UFS).

### Updating a running phone

`tools/update-phone.sh` (Linux, macOS, Git Bash/WSL) and `tools/update-phone.ps1` (Windows
PowerShell) do the same: fetch the newest package of each name from the build host
(`camellia-panel` only if it was built), copy them to the phone, `apk add` them, and compare
`/boot/boot.img` before and after. Unchanged: plain reboot. Changed: fetch the new image, verify
its hash, reboot (Volume Down held for fastboot), `fastboot flash boot_b`. `boot_b` is
write-protected while the system runs, so the boot image is always flashed with fastboot.

What changes the boot image: the kernel package, `deviceinfo` (command line), anything listed for
the initramfs (touch firmware, splash theme), and distribution upgrades that rebuild the
initramfs.

## Phone layout and boot

| Partition | Content |
| --- | --- |
| `boot_b`, `dtbo_b`, `vbmeta_b` | Nura boot image, the ROM's dtbo, vbmeta with verity and verification disabled |
| `userdata` | Nura rootfs image (subpartitions `pmOS_boot` and `pmOS_root`, 4096-byte sectors) |
| `boot_a`, `dtbo_a`, `vbmeta_a` | LineageOS 22.2 boot image and the ROM's dtbo/vbmeta: a recovery for backups and crash logs |
| `misc` (`/dev/sdc1`, not in by-partlabel) | A/B slot state: AOSP `bootloader_control` ("BCAB") at offset 2048, CRC32 over the first 28 bytes |

- Boot image header v2; `dtb.img` is a DT table. Keep `bootopt=64S3,32N2,64N2` in the command
  line; `initcall_debug=0` overrides `initcall_debug=1` from the dts bootargs.
- `fastboot boot` is not implemented by this LK; it falls back to recovery.
- Any ROM works as the starting point (stock MIUI included): slot a gets the LineageOS 22.2 boot
  image (same kernel base and device tree as the Nura kernel; the 23.2/24.0 kernels come from a
  different tree), with the stock `vbmeta.img` (verification disabled) and the stock `dtbo.img`
  (byte-identical to LineageOS 22.2's). Stock MIUI has no `adb root`, so the partition backup runs
  from that recovery and writes to `/tmp`.
- `mark-boot-successful` sets successful/tries on every boot like Android's boot HAL; without it
  the bootloader counts down the tries and falls back to slot a.
- USB networking is NCM with the IDs 1d6b:0104: Windows ADB driver packs claim 18d1:d001 (Nura's
  default) as an ADB recovery interface. Phone 172.16.42.1, host 172.16.42.2 by DHCP.
- SSH server: dropbear (keys in `~/.ssh/authorized_keys`). Firewall: pmOS's nftables default
  (SSH on every interface, DHCP on USB); more ports with a file in `/etc/nftables.d/`.
- First boot grows the root partition to fill `userdata` (about 47 GB); if only the partition
  grew, `resize2fs /dev/loop0p2`.

## Kernel branch `nura`

On top of `lineage-22.2`:

| Change | Why |
| --- | --- |
| `camellia_nura.config` (base) | devtmpfs, VT, SysV IPC, USB NCM gadget, USB Ethernet for OTG (CDC ECM/NCM, ASIX AX88179, Realtek RTL8152/8153 with `USB_RTL8152`), in-kernel fuel gauge, user namespaces, uevent helper, nftables (with 4.14's separate meta and set modules), zstd; fbcon off |
| `camellia_nura.config` (containers) | All that moby's `contrib/check-config.sh` lists as necessary (device cgroup, POSIX message queues, addrtype and ipvs matches), plus CPU bandwidth, block throttling, perf/net_prio/net_cls cgroups, IPVS, VXLAN, bridge VLAN filtering, macvlan, ipvlan with `NET_L3_MASTER_DEV`, `nft_fwd_netdev` |
| `USB_POWER_DELIVERY` on | Xiaomi turned it off in every camellia defconfig, so the MT6360 TCPC only did plain Type-C: a dock with a charger on its PD port is the power source, the phone attached as sink and stayed in device mode. With PD the phone negotiates a contract and does data and power role swaps (OTG Ethernet) |
| `MTK_BATTERY_KERNEL_GAUGE` | Runs the in-kernel (recovery) gauge algorithm in normal boot; Android's `fuelgauged` does not exist here. Default `n` |
| `ANDROID_DEFAULT_SETTING` no longer selects `STATIC_USERMODEHELPER` | Every usermode helper (uevent helper, poweroff/reboot) went to the Android-only `/system_ext/bin/aee_core_forwarder` |
| `FW_LOADER_USER_HELPER_FALLBACK` stays on | Selected by `MEDIATEK_SOLUTION`; a missing file waits for a userspace helper (the device package sets the timeout to 1 s early in boot) |
| `vt: selection`: duplicate `set_selection()` removed | 4.14.y merge leftover; the file was never built with VT off |
| `loop`: rescan partitions after a block size change | `losetup -P --sector-size 4096` scanned before the size was applied, so `pmOS_boot`/`pmOS_root` never appeared |
| `u_serial`: no NULL port in `gs_close()` | Oops when the initramfs removed its ACM function while getty reopened ttyGS0 |
| `f_ncm`: NDP signature follows the NTB format | Windows 11 selects 32-bit NTBs without SET_CRC_MODE; every NTB was dropped ("Wrong NDP SIGN") |
| Regulatory db from wireless-regdb `db.txt` | The built-in database was an empty placeholder |
| Wi-Fi driver: HU in its country table (CZ/NL rules) | The driver keeps its own table (US, CN, NL, CZ, JP, TR) and skips the regulatory notifier while halted |
| `vm.swappiness` up to 200 | zramstart sets 180; 4.14 capped it at 100 outside MTK's low-RAM profile |
| `msleep_idle()`, `usleep_idle_range()`, `wait_event_idle()`, `wait_event_idle_timeout()` | Sleep in `TASK_IDLE`, which neither counts as load nor trips the hung task detector |
| MediaTek threads use the idle sleeps | Watchdog kickers `wdtk-N`, `hang_detect` and its dump thread, `ccci_poll1`, `mddp_rx`, MT6360 `mivr_thread`, `gauge_timer_thr`, `mtk_drm_disp_id` (via `mtk_crtc_wait_status()`), `disp_echk`, USB boost; `mdrt_thread` is started with `kthread_run()` and waits interruptibly instead of sitting in kthread()'s uninterruptible start state |
| Writeback connectors hidden from `DRM_IOCTL_MODE_GETRESOURCES` | MTK registers two writeback connectors; this kernel has no `DRM_CLIENT_CAP_WRITEBACK_CONNECTORS`, so every client saw them. Plymouth laid its theme out on a 4096×2160 writeback "screen" |

## Packages

### `device-xiaomi-camellia`

- `deviceinfo`: header v2, `deviceinfo_dtb` names the DT table (installed as
  `/boot/dtbs/mediatek/mt6833-camellia-dtbtable.dtb`; boot-deploy requires it for header v2),
  rootfs sector size 4096, USB networking NCM. Kernel command line:
  `bootopt=64S3,32N2,64N2 initcall_debug=0 splash plymouth.ignore-serial-consoles cgroup_disable=schedtune`.
- OpenRC services: `wmt-launcher`, `camellia-wifi`, `camellia-battery`, `mark-boot-successful`
  (see Reference). Bump `pkgrel` on every change.
- Depends on `openssh-sftp-server`: the SSH server is dropbear, which has no SFTP, and current
  OpenSSH `scp` clients (Windows included) use the SFTP protocol.
- `/etc/NetworkManager/conf.d/50-camellia-unmanaged.conf`: NetworkManager must not manage `ap0`
  and `p2p0` (it picked `ap0` for a client connection).
- `/etc/rc.conf.d/50-camellia-cgroups.conf`: `rc_cgroup_mode="hybrid"` (Containers).
- `/usr/share/mkinitfs/files/10-camellia-touch.files`: touch firmware into the initramfs (Touch).

### `firmware-xiaomi-camellia`

From `vendor_xiaomi_camellia` at a pinned commit (`5416db4`): `WMT_SOC.cfg`,
`soc2_2_ram_*_1_1_hdr.bin` / `_1a_1_` (A-die MT6631 / MT6635), `WIFI_RAM_CODE_soc2_2_*.bin`,
`wifi.cfg`, `txpowerctrl.cfg`, `BT_FW.cfg`, `focaltech_ts_fw_tianma.bin`, into `/usr/lib/firmware`.

### `linux-xiaomi-camellia`

Packages the prebuilt kernel from `dist/` (pmbootstrap does not build it). `modules.builtin` lets
`modprobe` accept built-in modules (zram).

### `camellia-panel` (optional)

On-screen UI and the splash theme; see [On-screen panel](#on-screen-panel) and
[Boot splash](#boot-splash). The device package does not depend on it. Depends on `font-dejavu`
for the accented letters of the keyboard.

## Hardware notes

### Wi-Fi

Chain, done by `wmt-launcher` and `camellia-wifi` at boot:

1. Firmware fallback timeout to 1 s (`/sys/class/firmware/timeout`).
2. `wmt-loader`: ioctls on `/dev/wmtdetect` (`SOC_HW_INIT` returns EBUSY when built in; ignored),
   sets the STP mode synchronously (a power-on before the launcher's ioctl failed with
   "no hif info").
3. `wmt-launcher` (resident): answers `srh_rom_patch` with `SET_ROM_PATCH_INFO` (type = header
   byte 31, EMI offset = header bytes 25-27, files `soc2_2_ram_*_1_1_hdr.bin` for MT6631) and
   writes `/run/wmt-launcher.ready`.
4. NVRAM push: one `write()` of `WR-BUF:NVRAM` + the `WIFI` file (nvdata `APCFG/APRDEB/WIFI`,
   2050 bytes, nvdata mounted read-only) to `/dev/wmtWifi` (Android's `wlan_assistant` does this).
5. `echo 1 > /dev/wmtWifi` gives `wlan0` (with retries); the country is set while Wi-Fi is on
   (`WIFI_COUNTRY` in `/etc/conf.d/camellia-wifi`, default HU).

Always use `ifname wlan0` with nmcli; `ap0` and `p2p0` cannot be clients. WPA2 and WPA2/WPA3
transition work; WPA3-only networks did not connect in testing. 5 GHz works once the country is
known.

### Charging and thermal

- `camellia-battery`: charge limiter (`CHARGE_START`/`CHARGE_STOP`, default 75/80 %,
  `CHARGE_INTERVAL`) and the battery thermal zone.
- `echo "0 1" > /proc/mtk_battery_cmd/current_cmd` stops charging but keeps the power path (the
  phone runs from the charger, `Not charging`); `"0 0"` resumes. Not persistent.
  `battery/input_suspend` cuts input completely. `CURRENT_NOW` negative = charging.
- Thermal: Android's policy equals the kernel defaults except `tzbattery` (Android: 60 °C ->
  `mtktsbattery-sysrst`, 1 s polling; kernel: no trips). Restored with
  `echo "1 60000 0 mtktsbattery-sysrst 48000 0 no-cooler 46000 0 no-cooler 45000 0 no-cooler 44000 0 no-cooler 43000 0 no-cooler 42000 0 no-cooler 41000 0 no-cooler 40000 0 no-cooler 39000 0 no-cooler 1000" > /proc/driver/thermal/tzbattery`.
- `charger_thread` waits uninterruptibly for MT6360 ADC conversions (a few % of the time): real
  hardware waits, left as they are.

### Display

- MTK DRM, `/dev/dri/card0`, connector 28 `DSI-1` on CRTC 63, 1080×2400, modes 61.15 (preferred),
  91.73, 50.96 and 30.58 Hz (the last one flagged `userdef`). DRM dumb buffers with the legacy
  `drmModeSetCrtc` work; writes to a scanned-out buffer show without `drmModeDirtyFB`.
- fbdev exists (`/dev/fb0`, 1088×7200, 32 bpp) but `mtk_fbdev_probe` sets
  `info->screen_base = NULL`: fbcon crashed in `cfb_imageblit`, so fbcon is off. Do not write to
  `/dev/fb0`.
- Backlight: `/sys/class/leds/lcd-backlight` (0-2047). The panel turns off when the DRM master
  exits; the last picture stays when Plymouth quits.
- Geometry (LineageOS overlay): punch-hole camera at the top centre (x 503-577, y 0-103), status
  bar 103 px, rounded corners with a 104 px radius.

### Touch

- FocalTech FT8722 (in-cell, Tianma panel), `fts_ts`, `/dev/input/event2`, multitouch 10 slots,
  0-1079 × 0-2399, reports only while the panel is on.
- The controller loses its firmware whenever the panel powers up; the driver downloads
  `focaltech_ts_fw_tianma.bin` at probe (1.1 s) and on every panel resume. The probe request comes
  before the rootfs is mounted, so the file is also in the initramfs; without it the request
  waited for the 60 s fallback and touch stayed dead after boot until the first panel off/on
  (the bootloader leaves the panel on, so the first modeset is no power-up).
- Xiaomi's driver can start the resume download twice at once (IRQ thread and resume work); one
  fails, the other succeeds. Seen once, harmless so far.

### USB OTG Ethernet

- Tested with a Vention 8-in-1 USB-C dock (TGUHB): its Ethernet is a Realtek RTL8153
  (`0bda:8153`, driver `r8152`, `eth0`). The phone's port is USB 2.0 (MUSB), so the link runs at
  about 300 Mbit/s even on gigabit.
- Without a charger on the dock the phone is the source and powers it (`Attached.SRC`, MUSB host).
- With a charger on the dock's PD port the dock is the source: the phone attaches as sink, PD
  negotiates 5 V (the DT's only sink PDO), and the dock's data role swap makes the phone the host
  (`TCPC-PE-EVT:dr_swap`, `TCP_NOTIFY_DR_SWAP`). Without `USB_POWER_DELIVERY` no PD contract came
  up (`pd:0:0`, 5 V 1.5 A Type-C only) and the dock never enumerated.
- Charger plugged in while the phone powers the dock: PD power role swap (`pr_swap`), no detach,
  `eth0` stays up. Charger removed: the dock loses power, the phone sees a detach and attaches as
  source; `eth0` is back after about 6 s.
- Charges through the dock, and the charge limiter works the same way (`Not charging`, the phone
  runs from the charger).
- The USB network to the computer (`usb0`, 172.16.42.1) is gone while the dock is attached; SSH
  over Wi-Fi or Ethernet.
- NetworkManager runs `eth0` with DHCP and IPv6 by default; Ethernet has route metric 100 and
  Wi-Fi 600, so Ethernet is preferred while both stay up. Static settings: the panel's LAN tab
  (`camellia-eth` profile, below).
- `pe_ready` in `/sys/class/tcpc/type_c_port0` stays `no` with the dock; contract and swaps work
  anyway.

Checks: `cat /sys/class/typec/port0/data_role /sys/class/typec/port0/power_role` (host, and sink
with the charger or source without), `cat /sys/bus/usb/devices/*/product`,
`sudo dmesg | grep -iE 'TYPEC:|PE-EVT|otg_tcp_notifier|r8152'`.

### Keys

`mtk-kpd`, `/dev/input/event1`: `KEY_POWER`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`.

### Load average

About 1 on an idle phone after the idle-sleep patches (was about 17). The sum of
`nr_uninterruptible` over all CPUs (`/proc/sched_debug`) is +1 with no task in `D`: an
off-by-one in the scheduler's counter (a hidden exiting task or a lost decrement in the
MediaTek-modified scheduler). Left as is.

## Containers (Docker)

- Docker is installed on the phone by hand (BUILD.md), not by a package.
- runc aborts on 4.14 ("runc did not terminate successfully: exit status 2"): its libpathrs finds
  no `/proc/thread-self` candidate that passes its checks (it prefers `openat2` and the new mount
  API, which this kernel does not have). crun works and is the default runtime
  (`/etc/docker/daemon.json`).
- The cpu and cpuset controllers are cgroup v1 only on 4.14 (cpu came to v2 in 4.15, cpuset in
  5.0), so OpenRC mounts cgroups in hybrid mode. Docker then uses cgroup v1 and warns that its v1
  support is deprecated (planned for removal no later than May 2029).
- Hybrid mode mounts every v1 controller, including Android's EAS `schedtune`, which allows only a
  few groups and fails with ENOMEM beyond them ("creating cgroup directory
  /sys/fs/cgroup/schedtune/...: Out of memory"). `cgroup_disable=schedtune` on the command line
  disables it before anything is mounted.

## On-screen panel

`packages/camellia-panel`, C with LVGL 9.5.0 (fetched by the APKBUILD), runs as the OpenRC
service `camellia-panel` in the default runlevel, enabled by the package's post-install.

| File | Content |
| --- | --- |
| `drm_out.c` | First connected non-writeback connector, its preferred mode, one XRGB8888 dumb buffer, legacy `drmModeSetCrtc`, DPMS through the connector property |
| `main.c` | LVGL setup (partial rendering, 200-line buffers copied into the dumb buffer), evdev touch, power key (grabbed with `EVIOCGRAB`), screen-off timeout, tab layout, waits for Plymouth |
| `status.c` | Status page, refreshed every 2 s while the screen is on (nmcli every 10 s) |
| `wifi.c` | Wi-Fi list and the network form |
| `lan.c` | Ethernet (`eth0`) status and the IPv4/IPv6 form |
| `keyboard.c` | On-screen keyboard of the network form |
| `battery.c` | Charge limit |
| `power.c` | Reboot, power off |
| `exec.c` | fork/execvp with a pipe read from an LVGL timer: commands never block the UI and never pass through a shell |
| `dialog.c`, `ui.h` | Shared widgets and the screen geometry |

- Screen off: backlight 0 and DPMS off; on: DPMS on, mode set again, full redraw, backlight
  restored. `PANEL_TIMEOUT` in `/etc/conf.d/camellia-panel` (seconds, 0 = never, default 60).
- Layout: the top 103 px only hold the clock (left of the camera) and the battery level (right of
  it); the tab bar sits at the bottom, padded away from the rounded corners; DPI 400.
- Wi-Fi: `nmcli -t` terse output with escaped colons, one entry per SSID with its best signal,
  hidden ones skipped. Visible personal networks: `nmcli device wifi connect`. Hidden and
  enterprise networks: an existing profile named after the SSID is deleted, a new one added with
  `nmcli connection add` (key management `none`/WEP key, `wpa-psk`, `sae`, or `wpa-eap` with
  `802-1x.*`: EAP PEAP/TTLS/TLS/PWD, phase 2 MSCHAPv2/PAP/MSCHAP/GTC, identity, anonymous
  identity, password or client certificate, private key and key password, system CA store with
  `domain-suffix-match` or no validation), then `nmcli connection up`.
- LAN: link state, speed and duplex from `/sys/class/net/eth0`, addresses with prefix from
  `getifaddrs()` (IPv6 without link-local), gateways and DNS from `nmcli device show eth0`, every
  10 s. Settings live in one profile, `camellia-eth`, bound to `eth0`. The form has a method per
  family (Automatic / Manual / Disabled), address/prefix, gateway, DNS 1 and DNS 2; DNS servers
  with Automatic replace the ones from DHCP or router advertisements (`ignore-auto-dns`). Every
  address is checked with `inet_pton()` before anything changes. Save deletes `camellia-eth`,
  any profile on `eth0` and NetworkManager's automatic `Wired connection N` (NetworkManager then
  does not create them again for that adapter), adds `camellia-eth` and brings it up when a cable
  is in; otherwise it applies on the next plug-in. The address fields open the keyboard on its
  `?123` page.
- Keyboard (`keyboard.c`, replaces LVGL's `lv_keyboard`): Gboard-style, full width, 4 rows of
  150 px with the bottom row 90 px above the panel edge (rounded corners); letter keys on an even
  grid of 10 columns. Layouts English (US) QWERTY and Hungarian QWERTZ (plain QWERTZ like Gboard:
  every accent on long press, á é í ó ö ő ú ü ű first), picked from a list behind the globe key (or
  a long press on space) and saved as `KEYBOARD_LAYOUT` (`en`/`hu`) in
  `/etc/conf.d/camellia-panel`, again only that line. Pages `?123` and `=\<` hold every printable
  ASCII character. Shift is one-shot (off after the next character); a double tap within 400 ms
  locks caps. Long press opens the alternates like Gboard: the default option sits right above
  the finger (typed when released without sliding), the others spread away from the nearer screen
  edge; slide to pick, release to type. The default is the digit on the top letter row (shown in
  the key's corner), otherwise the first accent; in Hungarian e u i o open on é ú í ó with the
  digit next to it on the other side. Backspace deletes on press and repeats while held; the check key
  hides the keyboard. Keys never take the focus from the text field and never start a scroll
  (the slide over the alternates must stay a press).
- Fonts: the built-in Montserrat has only ASCII. DejaVu Sans
  (`/usr/share/fonts/dejavu/DejaVuSans.ttf`) is loaded with LVGL's Tiny TTF (`LV_USE_TINY_TTF`)
  as the fallback font of the keys and the form's text fields; without the file the keyboard
  works but accented letters show as nothing.
- Battery: rewrites only `CHARGE_START`/`CHARGE_STOP` in `/etc/conf.d/camellia-battery`
  (atomically) and restarts `camellia-battery`.
- Startup: waits while `plymouth --ping` answers (at most 3 minutes), then retries opening the
  display for up to 10 s. Before this, the panel started while plymouthd was still DRM master,
  `drmModeSetCrtc` failed with EACCES and the panel exited.
- Log: `/var/log/camellia-panel.log`. A hand-started copy (`camellia-panel <timeout>`) needs the
  service stopped first.
- Bug fixed on the way: `lv_msgbox` creates its footer only with the first button; sizing it
  earlier hit LVGL's NULL assert (frozen screen).

## Boot splash

- pmOS's initramfs ships Plymouth (`plymouthd`, the DRM renderer, the script plugin) and starts it
  in its second stage only with `splash` on the command line (`PMOS_NOSPLASH` was the old splash's
  flag and is no longer read). `deviceinfo_no_framebuffer=true` would also disable it.
- The bootloader adds `console=ttyS0`; with a serial console Plymouth forces its text-only
  "details" mode and never opens the display. `plymouth.ignore-serial-consoles` stops that.
- With the writeback connectors visible, Plymouth created two 4096×2160 heads and laid the theme
  out on them: black panel. Fixed in the kernel (hidden connectors).
- `plymouth-openrc-plugin` starts Plymouth for shutdown, sends `Starting service <name>` /
  `Stopping service <name>` per service (`plymouth message`) and `plymouth update --status`, and
  quits Plymouth when the default runlevel is up (on shutdown with `--retain-splash` before
  `mount-ro`).
- Theme `nura-camellia` (in `camellia-panel`): wordmark and device name (images rendered from
  DejaVu Sans), progress bar from Plymouth's boot progress estimate
  (`/var/lib/plymouth/boot-duration` from the previous boot), the current step below it, the
  panel's background colour. Installed to `/usr/share/plymouth/themes/nura-camellia`, listed for
  the initramfs in `/usr/share/mkinitfs/files/50-camellia-splash.files`, selected with
  `Theme=nura-camellia` in `/etc/plymouth/plymouthd.conf` by the post-install (removed again on
  deinstall). Without the panel package, pmOS's `pmos-spin` theme shows.
- Testing Plymouth by hand on a running system (panel stopped):
  `plymouthd --mode=boot --debug --debug-file=/tmp/ply.log --kernel-command-line="splash plymouth.ignore-serial-consoles"`,
  `plymouth show-splash`, `plymouth display-message --text=...`, `plymouth quit`.
- Once, a shutdown hung at the cgroups cleanup with the splash showing (right after `apk add` in
  the same SSH command, with the panel service crashed). Not reproduced since.

## Troubleshooting and debugging

| Symptom | Likely cause | What to do |
| --- | --- | --- |
| Redmi logo with an on-screen keyboard, no SSH | The initramfs could not mount the rootfs and opened its debug shell | Debug shell, below |
| Phone restarts by itself, or never gets USB network | Kernel crash | Crash log via the recovery slot, below |
| Phone boots LineageOS recovery instead of Nura | The bootloader fell back to slot a (tries used up) | `fastboot set_active b`, `fastboot reboot` |
| USB network appears but nothing answers (Windows) | A third-party ADB driver claimed the device, or the LAN uses 172.16.x | Uninstall that driver (Device Manager, "Attempt to remove the driver"); `ssh -b 172.16.42.2 user@172.16.42.1` |
| No `wlan0` | Wi-Fi chain failed at boot | `rc-status`, `sudo cat /var/log/wmt-launcher.log`, `dmesg \| grep -iE 'wmt\|wlan'` |
| pmbootstrap stops with HTTP 502 | A package mirror is down | Retry later or `--offline` |
| `pmbootstrap config: ... invalid choice: 'systemd'` | pmbootstrap 3.11 renamed the setting | `pmbootstrap config service_manager openrc` |
| `build-nura-kernel.sh`: `not applied: CONFIG_...` | Unmet dependency in the fragment | Add the dependency to `camellia_nura.config` |
| No touch events, dmesg `fts_fw_resume: ... request fail` / `TP Not Ready` | Touch firmware missing | `apk info -W /usr/lib/firmware/focaltech_ts_fw_tianma.bin` must name `firmware-xiaomi-camellia` |
| Touch dead after boot until the screen goes off and on, dmesg `fts_fwupg_get_fw_file: fw file len(0) fail` at about 61 s | The running boot image has no touch firmware in its initramfs | `zcat /boot/initramfs \| cpio -t \| grep focal`; flash the current `/boot/boot.img` |
| Screen stays on the Redmi logo | Panel not installed or not running | `rc-status \| grep camellia-panel`, `sudo tail /var/log/camellia-panel.log` |
| Panel never appears after the splash, log `drmModeSetCrtc: Permission denied`, service `crashed` | Panel older than 0.6 | Update `camellia-panel` |
| Accented letters missing on the keyboard, log `keyboard: DejaVu Sans not found` | `font-dejavu` not installed | `sudo apk add font-dejavu`, `sudo rc-service camellia-panel restart` |
| Splash black or text only | Command line or kernel | `cat /proc/cmdline` must show `splash plymouth.ignore-serial-consoles`; `sudo modetest -M mediatek -c \| grep -c Writeback` must print 0 |
| Dock with a charger on its PD port: nothing on USB, `data_role` shows `[device]` | Kernel without `USB_POWER_DELIVERY` (no PD contract, no data role swap) | `grep USB_POWER_DELIVERY ~/android/nura/dist/config-xiaomi-camellia.aarch64`; flash the current boot image |
| Dock on USB but no `eth0` | Adapter chip without a driver in the kernel | `cat /sys/bus/usb/devices/*/idVendor` / `idProduct`; add its `USB_NET_*` / `USB_RTL8152` option to `camellia_nura.config` |
| `eth0` up but the panel's static settings are not used | Another profile took `eth0` | `nmcli connection show`; save once on the LAN tab (removes the others) |
| Docker: `runc did not terminate successfully: exit status 2` | runc on 4.14 | crun as default runtime |
| Docker: `.../schedtune/...: Out of memory` | Boot image without `cgroup_disable=schedtune` | Flash the current boot image |
| `docker info`: `No cpu cfs quota support`, `No cpuset support` | cgroups in unified mode | `/etc/rc.conf.d/50-camellia-cgroups.conf` must set `rc_cgroup_mode="hybrid"`; reboot |

### Debug shell

The initramfs opens a shell on TCP port 23 when it cannot continue: `nc 172.16.42.1 23` (Linux,
macOS) or a telnet client (Windows: Telnet Client feature or PuTTY in Telnet mode). There:
`cat /pmOS_init.log`, `blkid`; `pmos_continue_boot` retries after a fix.

### Crash log via the recovery slot

pstore keeps the kernel log across a warm restart (a full power-off clears it):

1. Volume Down + Power to fastboot, `fastboot set_active a`, `fastboot reboot recovery`.
2. `adb pull /sys/fs/pstore/console-ramoops nura-crash.txt` (sometimes `console-ramoops-0`).
3. `adb shell 'rm -f /sys/fs/pstore/*'` (pstore can keep a stale record).
4. `adb reboot bootloader`, `fastboot set_active b`, `fastboot reboot`.

### Logs on the root filesystem

`/var/log/dmesg` (kernel log of the current boot), `/var/log/rc.log` (after
`rc_logger="YES"` in `/etc/rc.conf`), `/var/log/wmt-launcher.log`, `/var/log/camellia-panel.log`.

Reading them from the LineageOS recovery when Nura does not boot far enough for SSH:

```sh
adb shell
setenforce 0
set -- $(dd if=/dev/block/by-name/userdata bs=8 skip=1044 count=1 2>/dev/null | od -An -tu8)
L=$(losetup -f)
losetup -r -o $(($1*4096)) $L /dev/block/by-name/userdata
mkdir -p /tmp/nura && mount -t ext4 -o ro,noload $L /tmp/nura
```

`setenforce 0`: recovery's SELinux policy blocks the loop driver on `userdata`; `noload` skips the
journal of an uncleanly unmounted filesystem.

### Methods that found the problems

- Hung or busy threads: `/proc/<pid>/stack` and `wchan`; sample states over all threads with
  `cat /proc/[0-9]*/task/[0-9]*/stat | sed -n 's/^[0-9]* \((.*)\) \([RD]\) .*/\2 \1/p'` (BusyBox
  `ps` and a naive `awk '$3'` miss threads and names with spaces).
- Scheduler counters: `nr_running`/`nr_uninterruptible` per CPU in `/proc/sched_debug`.
- Display: `modetest -M mediatek` (connectors, `-s 28:1080x2400[-rate]` to set a mode), small
  libdrm test programs compiled on the phone (`apk add gcc musl-dev libdrm-dev`).
- Input: `evtest --grab /dev/input/eventN` (grab keeps the power key away from everything else).
- Initramfs: unpack `/boot/initramfs` with `zcat | cpio -id` and read `init`, `init_2nd.sh`,
  `init_functions*.sh` (BusyBox `grep` has no `--include`).
- Docker: `contrib/check-config.sh` from moby against `dist/config-xiaomi-camellia.aarch64`
  (its cgroup/apparmor/sysctl lines describe the host it runs on); `runc --debug run` with a
  bundle from `docker export` shows runc's own error.

## Known limitations

| Area | State |
| --- | --- |
| GPU, modem, camera, audio | Not supported (downstream kernel without the Android userspace they need) |
| Bluetooth, GPS, FM, sensors, IR, vibration | Drivers load; not brought up yet |
| USB OTG Ethernet | Works with a USB-C dock (RTL8153), with or without a charger on its PD port; USB 2.0 port (about 300 Mbit/s); the USB network to the computer is gone while the dock is attached |
| Kernel console | fbcon off (fbdev has no kernel mapping) |
| Wi-Fi | WPA3-only did not connect in testing; driver country table limited (others fall back to its worldwide list) |
| Battery percentage | Kernel's simplified gauge algorithm; less accurate than Android's |
| Kernel updates | Always flashed with fastboot (`boot_b` write-protected while running); `fastboot boot` not implemented |
| USB network address | Fixed at 172.16.42.1 |
| Containers | Docker with crun and cgroup v1; Podman not tested |
| Load average | About 1 when idle (counter off by one) |
| Distribution | Nura edge (rolling) |

## Reference

### Boot services (runlevel default)

| Service | Does |
| --- | --- |
| `wmt-launcher` | Firmware fallback timeout 1 s, `wmt-loader`, then the ROM patch launcher daemon |
| `camellia-wifi` | Mounts `nvdata` read-only, pushes the Wi-Fi calibration, powers Wi-Fi on, sets the country |
| `camellia-battery` | Battery thermal zone and charge limiter |
| `mark-boot-successful` | Marks the A/B slot successful |
| `camellia-panel` (optional package) | On-screen UI |

### Files on the phone

| Path | What |
| --- | --- |
| `/etc/conf.d/camellia-wifi` | `WIFI_COUNTRY` |
| `/etc/conf.d/camellia-battery` | `CHARGE_START`, `CHARGE_STOP`, `CHARGE_INTERVAL` |
| `/etc/conf.d/camellia-panel` | `PANEL_TIMEOUT`, `KEYBOARD_LAYOUT` |
| `/usr/libexec/camellia/` | `wmt-loader`, `wmt-launcher`, `mark-boot-successful`, `charge-limit` |
| `/etc/NetworkManager/conf.d/50-camellia-unmanaged.conf` | NetworkManager off `ap0`, `p2p0` |
| `/etc/NetworkManager/system-connections/camellia-eth.nmconnection` | Ethernet settings of the panel's LAN tab |
| `/etc/rc.conf.d/50-camellia-cgroups.conf` | `rc_cgroup_mode="hybrid"` |
| `/usr/share/mkinitfs/files/10-camellia-touch.files` | Touch firmware into the initramfs |
| `/usr/share/mkinitfs/files/50-camellia-splash.files` | Splash theme into the initramfs |
| `/usr/share/plymouth/themes/nura-camellia/` | Splash theme |
| `/etc/plymouth/plymouthd.conf` | `Theme=nura-camellia` |
| `/etc/docker/daemon.json` | crun as Docker's default runtime (by hand) |
| `/mnt/vendor/nvdata` | Calibration partition, read-only |
| `/proc/mtk_battery_cmd/current_cmd` | `0 1` stop charging (power path kept), `0 0` resume |

## History

| Date | Step |
| --- | --- |
| 2026-10-28 | Notes from LineageOS (boot, Wi-Fi chain, charging, thermal, display) |
| Boot 1-3 | `fastboot boot` unsupported; fbcon crash (fbcon off); loop partitions not found after the sector size change (kernel fix) |
| Boot 4-5 | u_serial Oops at the initramfs hand-over; USB IDs changed for Windows; f_ncm NDP signature fix |
| Boot 8-11 | zram, A/B slot marking, Wi-Fi chain as services, 5 GHz with HU, unattended boot |
| 2026-11 | Standalone toolchain, update script, sftp, firewall |
| 2026-11-18 | Container kernel options, hybrid cgroups, schedtune off, crun; touch firmware (package and initramfs); `camellia-panel` (status, Wi-Fi incl. hidden and enterprise, battery, power); idle-sleep kernel patches (load 17 -> 1); writeback connectors hidden; Plymouth splash with the `nura-camellia` theme; update script for Windows |
| 2026-11-19 | `camellia-panel` 0.8: Gboard-style keyboard (English US / Hungarian, symbol pages, long-press accents and digits, one-shot shift, caps lock) |
| 2026-11-19 | `camellia-panel` 0.10: long-press pop-up opens with its default above the finger; Hungarian e u i o default to é ú í ó |
| 2026-11-20 | USB OTG Ethernet: `USB_POWER_DELIVERY` (PD contract, data and power role swaps with a powered dock), `r8152` for RTL8153; `camellia-panel` 0.11: LAN tab (Ethernet status, IPv4/IPv6 automatic, manual or disabled, gateway, two DNS servers) |
