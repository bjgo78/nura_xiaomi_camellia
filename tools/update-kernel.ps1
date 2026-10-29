# Update the Nura kernel on camellia from Windows:
#   newest linux-xiaomi-camellia package on the VM -> phone (apk add, mkinitfs builds
#   /boot/boot.img with the installed rootfs UUIDs) -> boot.img back to Windows -> fastboot boot_b.
# boot_b cannot be written from the running system (UFS write protection), hence fastboot.
param(
    [string]$Vm = 'bjozsef@192.168.20.201',
    [string]$Phone = 'bjozsef@172.16.42.1',
    [string]$Bind = '172.16.42.2'
)
$ErrorActionPreference = 'Stop'
$work = Join-Path $PSScriptRoot 'kernel-update'
New-Item -ItemType Directory -Force $work | Out-Null

# cmd keeps redirected binary data intact (Windows PowerShell 5 pipelines do not)
function Invoke-Cmd([string]$line) {
    cmd /c $line
    if ($LASTEXITCODE -ne 0) { throw "failed ($LASTEXITCODE): $line" }
}
function Get-PhoneHash([string]$path) {
    ((ssh -b $Bind $Phone "sha256sum $path") -split '\s+')[0]
}

Write-Host '==> Newest kernel package on the VM'
$remote = (ssh $Vm 'ls -t ~/.local/var/pmbootstrap/packages/edge/aarch64/linux-xiaomi-camellia-*.apk | head -1')
if (-not $remote) { throw 'no kernel package found on the VM' }
$remote = $remote.Trim()
$apk = Join-Path $work (Split-Path $remote -Leaf)
Write-Host "    $remote"
Invoke-Cmd "scp `"${Vm}:$remote`" `"$apk`""

Write-Host '==> Copy to the phone and install'
Invoke-Cmd "ssh -b $Bind $Phone `"cat > linux.apk`" < `"$apk`""
$h = (Get-FileHash $apk -Algorithm SHA256).Hash.ToLower()
if ($h -ne (Get-PhoneHash 'linux.apk')) { throw 'kernel package hash mismatch after copy' }
ssh -t -b $Bind $Phone 'sudo apk add --no-interactive --allow-untrusted ./linux.apk'
if ($LASTEXITCODE -ne 0) { throw 'apk add failed' }

Write-Host '==> Fetch the new boot.img'
$img = Join-Path $work 'boot.img'
Invoke-Cmd "ssh -b $Bind $Phone `"cat /boot/boot.img`" > `"$img`""
$h = (Get-FileHash $img -Algorithm SHA256).Hash.ToLower()
if ($h -ne (Get-PhoneHash '/boot/boot.img')) { throw 'boot.img hash mismatch after copy' }

Write-Host '==> Rebooting the phone: HOLD VOLUME DOWN until the fastboot screen appears'
ssh -t -b $Bind $Phone 'sudo reboot'
Write-Host '    waiting for fastboot...'
do { Start-Sleep 2; $dev = fastboot devices } while (-not $dev)
fastboot flash boot_b $img
if ($LASTEXITCODE -ne 0) { throw 'fastboot flash failed' }
fastboot reboot
Write-Host '==> Done. The phone boots the new kernel.'
