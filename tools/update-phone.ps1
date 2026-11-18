# Install the newest camellia packages on a running phone, from Windows PowerShell.
# Needs ssh, scp (built into Windows 10/11) and fastboot (Android Platform-Tools) on the PATH.
# The packages are fetched from the build host. If /boot/boot.img changed, it is flashed to
# boot_b with fastboot (boot_b is write-protected while the system runs); otherwise the phone
# just reboots.
#
# Usage:  powershell -ExecutionPolicy Bypass -File update-phone.ps1 PHONE BUILD_HOST
#   PHONE       user@address of the phone, e.g. bjozsef@172.16.42.1 (USB)
#   BUILD_HOST  user@address of the build host
# camellia-panel is installed too when it was built on the build host.
param(
	[Parameter(Mandatory = $true, Position = 0)][string]$Phone,
	[Parameter(Mandatory = $true, Position = 1)][string]$BuildHost
)
$ErrorActionPreference = "Stop"
$pkgdir = ".local/var/pmbootstrap/packages/edge/aarch64"
$required = "firmware-xiaomi-camellia", "device-xiaomi-camellia", "linux-xiaomi-camellia"
$optional = "camellia-panel"
$work = Join-Path $env:TEMP ("nura-update-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null

function Get-PhoneBootHash {
	$line = ssh $Phone 'sha256sum /boot/boot.img'
	return ($line -split '\s+')[0]
}

try {
	$files = @()
	foreach ($pkg in $required + $optional) {
		$src = ssh $BuildHost "ls -t $pkgdir/$pkg-[0-9]*.apk 2>/dev/null | head -1"
		if (-not $src) {
			if ($optional -contains $pkg) { Write-Host "--- $pkg not built, skipped"; continue }
			throw "no $pkg package found on $BuildHost"
		}
		$name = $src.Split('/')[-1]
		Write-Host "==> $name"
		scp -q "${BuildHost}:$src" (Join-Path $work $name)
		if ($LASTEXITCODE -ne 0) { throw "copying $name from the build host failed" }
		$files += (Join-Path $work $name)
	}

	Write-Host "==> copying to the phone"
	ssh $Phone 'rm -rf phone-update && mkdir phone-update'
	scp -q $files "${Phone}:phone-update/"
	if ($LASTEXITCODE -ne 0) { throw "copying the packages to the phone failed" }

	$before = Get-PhoneBootHash
	Write-Host "==> installing (sudo password of the phone user)"
	ssh -t $Phone 'sudo apk add --no-interactive --allow-untrusted ./phone-update/*.apk && rm -rf phone-update'
	if ($LASTEXITCODE -ne 0) { throw "apk add failed on the phone" }
	$after = Get-PhoneBootHash

	if ($before -eq $after) {
		Write-Host "==> boot.img unchanged, rebooting"
		ssh -t $Phone 'sudo reboot'
		return
	}

	Write-Host "==> boot.img changed, fetching it"
	$boot = Join-Path $work "boot.img"
	scp -q "${Phone}:/boot/boot.img" $boot
	if ((Get-FileHash -Algorithm SHA256 $boot).Hash.ToLower() -ne $after) {
		throw "boot.img hash mismatch after copy"
	}

	Write-Host "==> rebooting the phone: HOLD VOLUME DOWN until the fastboot screen appears"
	ssh -t $Phone 'sudo reboot'
	Write-Host "    waiting for fastboot..."
	do { Start-Sleep -Seconds 2 } until ((fastboot devices) -match "fastboot")
	fastboot flash boot_b $boot
	fastboot reboot
	Write-Host "==> done"
}
finally {
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
