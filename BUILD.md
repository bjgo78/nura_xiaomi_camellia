# Build Nura (pmOS) for Camellia

From a clean Ubuntu VM and an unlocked phone with any ROM (stock MIUI included) to a running Nura
server, with the optional on-screen panel, boot splash and Docker. Technical details, background
and troubleshooting: [README.md](README.md).

## System Requirements

| Item | Requirement |
| --- | --- |
| Phone | Xiaomi Redmi Note 10 5G (camellia), bootloader unlocked |
| Build host | Ubuntu 24.04 VM: 4+ cores, 16 GB RAM, 80 GB disk, internet |
| Computer for flashing | Windows 10/11, Linux or macOS with a USB port (can be the build host) |
| On that computer | Android Platform-Tools (`fastboot`, `adb`) on the PATH; Windows also the Google USB Driver |
| Files | `vbmeta.img` and `dtbo.img` from the stock fastboot ROM of the phone's MIUI version (`images/` folder); a LineageOS 22.2 `boot.img` for camellia, renamed to `lineage-boot.img` |

Everything on the phone's internal storage is erased.

## Install Ubuntu

Install Ubuntu 24.04 in the VM, then enable SSH so the computer for flashing can reach it:

```bash
sudo apt update
sudo apt install -y openssh-server
ip -br addr
```

Note the VM's address; below it is `user@vm`.

## Install packages

```bash
sudo apt install -y git curl make flex bison bc perl python3 build-essential \
    libssl-dev libelf-dev cpio kmod rsync openssl
```

## Prepare for building

```bash
mkdir -p ~/android/nura/tools && cd ~/android/nura
git clone --single-branch -b nura https://github.com/bjgo78/kernel_xiaomi_camellia.git kernel
git clone https://github.com/bjgo78/nura_xiaomi_camellia.git
~/android/nura/nura_xiaomi_camellia/tools/setup-toolchain.sh

cd ~/android/nura/tools
git clone --depth=1 https://gitlab.postmarketos.org/postmarketOS/pmbootstrap.git
mkdir -p ~/.local/bin
ln -sf "$PWD/pmbootstrap/pmbootstrap.py" ~/.local/bin/pmbootstrap
export PATH="$HOME/.local/bin:$PATH"
```

## Build Nura

1. Build the kernel (15 to 40 minutes the first time):

```bash
~/android/nura/nura_xiaomi_camellia/build-nura-kernel.sh
```

2. First pmbootstrap init:

```bash
pmbootstrap init
```

| Question | Answer |
| --- | --- |
| Channel | `edge` |
| Vendor / device | `qemu` / `aarch64` |
| Kernel | default (`stable`) |
| Username | your login name for the phone |
| User interface | `console` |
| Hostname | `xiaomi-camellia` |
| Everything else | default |

3. Select OpenRC, copy the port into pmaports, and init again with the real device (vendor
`xiaomi`, device `camellia`, the other answers as before):

```bash
pmbootstrap config service_manager openrc
~/android/nura/nura_xiaomi_camellia/pmaports-sync.sh
pmbootstrap init
```

4. Build the packages (the panel is optional) and the images:

```bash
pmbootstrap build firmware-xiaomi-camellia device-xiaomi-camellia linux-xiaomi-camellia
pmbootstrap build camellia-panel
pmbootstrap install
pmbootstrap export
mkdir -p ~/android/nura/flash
cp -L /tmp/postmarketOS-export/boot.img /tmp/postmarketOS-export/xiaomi-camellia.img ~/android/nura/flash/
```

`pmbootstrap install` asks for the phone user's password.

## Prepare the phone

Do this on the computer for flashing, in a folder with `vbmeta.img`, `dtbo.img` and
`lineage-boot.img`. The commands work the same in PowerShell, bash and zsh.

1. Power the phone off, hold Volume Down + Power until fastboot, connect USB:

```bash
fastboot getvar unlocked
```

It must say `unlocked: yes`.

2. Put the recovery into slot a and start it:

```bash
fastboot --disable-verity --disable-verification flash vbmeta_a vbmeta.img
fastboot flash dtbo_a dtbo.img
fastboot flash boot_a lineage-boot.img
fastboot reboot recovery
```

3. Back up the calibration and identity partitions (IMEI, MAC addresses, Wi-Fi calibration).
Keep the `camellia-backup` folder private:

```bash
adb shell 'mkdir -p /tmp/camellia-backup && for p in nvram nvdata nvcfg persist protect1 protect2 proinfo; do [ -e /dev/block/by-name/$p ] && dd if=/dev/block/by-name/$p of=/tmp/camellia-backup/$p.img bs=4M; done; ls -l /tmp/camellia-backup'
adb pull /tmp/camellia-backup
```

The list must show seven files.

## Flash Nura

```bash
scp user@vm:android/nura/flash/*.img .
adb reboot bootloader
fastboot --disable-verity --disable-verification flash vbmeta_b vbmeta.img
fastboot flash dtbo_b dtbo.img
fastboot flash boot_b boot.img
fastboot flash userdata xiaomi-camellia.img
fastboot set_active b
fastboot reboot
```

The first boot takes 5 to 8 minutes (it grows the root filesystem). A USB network adapter
appears on the computer about 10 seconds after power-on.

## First login

```bash
ssh user@172.16.42.1
```

Connect Wi-Fi (always on `wlan0`):

```bash
sudo nmcli device wifi connect "SSID" password "PASSWORD" ifname wlan0
```

Check the root filesystem: `df -h /` should show about 47 GB; if not, `sudo resize2fs /dev/loop0p2`.

## On-screen panel and boot splash (optional)

The panel shows the server's status and sets Wi-Fi (including hidden and enterprise networks),
the charge limit and power by touch; the package also brings the Nura boot splash. It was built
in Build Nura step 4; install it by running the update script from Update the phone once,
which also flashes the changed boot image. The screen turns off after 60 s without touch
(`PANEL_TIMEOUT` in `/etc/conf.d/camellia-panel`); the power key turns it off and on.

## Docker (optional)

On the phone, after Wi-Fi is connected. crun replaces runc, which does not work on this kernel:

```bash
sudo apk add docker docker-cli-compose crun
sudo mkdir -p /etc/docker
printf '{\n  "runtimes": { "crun": { "path": "%s" } },\n  "default-runtime": "crun"\n}\n' "$(command -v crun)" | sudo tee /etc/docker/daemon.json
sudo rc-update add docker default
sudo rc-service docker start
sudo docker run --rm hello-world
```

## Update the phone

After changing something, rebuild on the build host:

| Changed | Rebuild |
| --- | --- |
| Kernel | `build-nura-kernel.sh`, `pmaports-sync.sh`, `pmbootstrap build linux-xiaomi-camellia` |
| A package | Bump its `pkgrel` (or `pkgver`), `pmaports-sync.sh`, `pmbootstrap build <package>` |

Then install on the phone from the computer for flashing (phone on USB). The script takes the
newest packages from the build host, installs them, and if the boot image changed asks you to
**hold Volume Down** while the phone restarts, then flashes it.

Windows (PowerShell):

```powershell
curl.exe -LO https://raw.githubusercontent.com/bjgo78/nura_xiaomi_camellia/main/tools/update-phone.ps1
powershell -ExecutionPolicy Bypass -File .\update-phone.ps1 user@172.16.42.1 user@vm
```

Linux and macOS:

```bash
curl -LO https://raw.githubusercontent.com/bjgo78/nura_xiaomi_camellia/main/tools/update-phone.sh
sh update-phone.sh user@172.16.42.1 user@vm
```

Distribution updates: run `sudo apk upgrade` on the phone. If it printed that it rebuilt the
boot image (mkinitfs), flash that image from the computer, holding Volume Down during the restart:

```bash
scp user@172.16.42.1:/boot/boot.img nura-boot.img
ssh -t user@172.16.42.1 sudo reboot
fastboot flash boot_b nura-boot.img
fastboot reboot
```
