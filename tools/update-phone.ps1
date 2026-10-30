# Update the camellia Nura packages from Windows:
#   newest firmware-, device- and linux-xiaomi-camellia packages from the VM -> phone
#   (apk add; the kernel package rebuilds /boot/boot.img with the installed rootfs UUIDs).
#   If boot.img changed, it is fetched and flashed to boot_b with fastboot (boot_b is
#   write-protected from the running system), otherwise the phone is just rebooted.
param(
    [string]$Vm = 'bjozsef@192.168.20.201',
    [string]$Phone = 'bjozsef@172.16.42.1',
    [string]$Bind = '172.16.42.2',
    [string[]]$Packages = @('firmware-xiaomi-camellia', 'device-xiaomi-camellia', 'linux-xiaomi-camellia')
)
$ErrorActionPreference = 'Stop'
$work = Join-Path $PSScriptRoot 'phone-update'
New-Item -ItemType Directory -Force $work | Out-Null

# cmd keeps redirected binary data intact (Windows PowerShell 5 pipelines do not)
function Invoke-Cmd([string]$line) {
    cmd /c $line
    if ($LASTEXITCODE -ne 0) { throw "failed ($LASTEXITCODE): $line" }
}
function Get-PhoneHash([string]$path) {
    ((ssh -b $Bind $Phone "sha256sum $path") -split '\s+')[0]
}

$names = @()
foreach ($pkg in $Packages) {
    Write-Host "==> $pkg"
    $remote = ssh $Vm "ls -t ~/.local/var/pmbootstrap/packages/edge/aarch64/$pkg-[0-9]*.apk 2>/dev/null | head -1"
    if (-not $remote) { throw "no $pkg package found on the VM" }
    $remote = $remote.Trim()
    $name = Split-Path $remote -Leaf
    $local = Join-Path $work $name
    Invoke-Cmd "scp `"${Vm}:$remote`" `"$local`""
    Invoke-Cmd "ssh -b $Bind $Phone `"mkdir -p phone-update && cat > phone-update/$name`" < `"$local`""
    $h = (Get-FileHash $local -Algorithm SHA256).Hash.ToLower()
    if ($h -ne (Get-PhoneHash "phone-update/$name")) { throw "$name hash mismatch after copy" }
    $names += "./phone-update/$name"
}

$before = Get-PhoneHash '/boot/boot.img'
Write-Host '==> Installing on the phone'
ssh -t -b $Bind $Phone "sudo apk add --no-interactive --allow-untrusted $($names -join ' ') && rm -rf phone-update"
if ($LASTEXITCODE -ne 0) { throw 'apk add failed' }
$after = Get-PhoneHash '/boot/boot.img'

if ($before -eq $after) {
    Write-Host '==> boot.img unchanged, rebooting'
    ssh -t -b $Bind $Phone 'sudo reboot'
    Write-Host '==> Done.'
    exit 0
}

Write-Host '==> boot.img changed, fetching it'
$img = Join-Path $work 'boot.img'
Invoke-Cmd "ssh -b $Bind $Phone `"cat /boot/boot.img`" > `"$img`""
if ((Get-FileHash $img -Algorithm SHA256).Hash.ToLower() -ne $after) { throw 'boot.img hash mismatch after copy' }

Write-Host '==> Rebooting the phone: HOLD VOLUME DOWN until the fastboot screen appears'
ssh -t -b $Bind $Phone 'sudo reboot'
Write-Host '    waiting for fastboot...'
do { Start-Sleep 2; $dev = fastboot devices } while (-not $dev)
fastboot flash boot_b $img
if ($LASTEXITCODE -ne 0) { throw 'fastboot flash failed' }
fastboot reboot
Write-Host '==> Done. The phone boots the new boot.img.'
