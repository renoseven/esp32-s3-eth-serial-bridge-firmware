# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven
#
# Attach a USB device to WSL via usbipd-win.
#
#   attach_wsl_usb.ps1 [-HardwareId VID:PID] [-AutoAttach 0|1] [-Quiet]
#
# Finds a device by VID and PID, shares it if needed, attaches it to WSL, and
# optionally registers usbipd auto-attach for that hardware ID.

param(
    [string]$HardwareId = '303a:1001',
    [string]$AutoAttach = '1',
    [switch]$Quiet
)

$ErrorActionPreference = 'Continue'
$script:Quiet = [bool]$Quiet
$HardwareId = $HardwareId.Trim().ToLower()
if ($HardwareId -match '^(?<vid>[^:]+):(?<productId>.+)$') {
    $vendorId = $Matches.vid -replace '^0x', ''
    $productId = $Matches.productId -replace '^0x', ''
    $HardwareId = "$vendorId`:$productId"
} else {
    [Console]::Error.WriteLine('Error: HardwareId must be VID:PID (e.g. 303a:1001).')
    exit 1
}

# --- Constants ---

$AttachRetryAttempts = 5
$AttachRetryDelaySec = 1
$AttachSettleDelaySec = 0.5

$UsbipdCandidates = @(
    "${env:ProgramFiles}\usbipd-win\usbipd.exe",
    "${env:ProgramFiles(x86)}\usbipd-win\usbipd.exe"
)

# --- Logging ---

function Write-Err {
    param([string]$Message)
    if (-not $script:Quiet) {
        [Console]::Error.WriteLine($Message)
    }
}

function Write-Info {
    param([string]$Message)
    if ($script:Quiet) {
        return
    }
    [Console]::Out.WriteLine($Message)
}

# --- usbipd ---

function Get-UsbipdPath {
    $cmd = Get-Command usbipd -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    foreach ($path in $UsbipdCandidates) {
        if (Test-Path $path) {
            return $path
        }
    }
    return $null
}

function Invoke-UsbipdAttach {
    param(
        [string]$Usbipd,
        [string]$HardwareId,
        [string]$BusId
    )
    if ($BusId) {
        & $Usbipd attach --wsl --busid $BusId *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }
    }
    & $Usbipd attach --wsl --hardware-id $HardwareId *> $null
}

function Wait-AndAttach {
    param(
        [string]$Usbipd,
        [string]$HardwareId,
        [string]$BusId
    )

    Start-Sleep -Seconds $AttachSettleDelaySec

    for ($attempt = 1; $attempt -le $AttachRetryAttempts; $attempt++) {
        if (Test-DeviceAttached -Usbipd $Usbipd -HardwareId $HardwareId) {
            return $true
        }

        $label = if ($attempt -eq 1) { 'Attaching' } else { 'Retrying' }
        Write-Info "$label ($attempt/$AttachRetryAttempts) ..."

        $useBusId = $BusId -and ($attempt -eq $AttachRetryAttempts)
        Invoke-UsbipdAttach -Usbipd $Usbipd -HardwareId $HardwareId -BusId $(if ($useBusId) { $BusId } else { '' })
        if ($LASTEXITCODE -eq 0 -and (Test-DeviceAttached -Usbipd $Usbipd -HardwareId $HardwareId)) {
            return $true
        }

        if ($attempt -lt $AttachRetryAttempts) {
            Start-Sleep -Seconds $AttachRetryDelaySec
        }
    }

    return $false
}

# --- Device ---

function Get-Device {
    param(
        [string]$Usbipd,
        [string]$HardwareId
    )

    $line = @(& $Usbipd list) | Where-Object { $_ -like "*$HardwareId*" } | Select-Object -First 1
    if (-not $line) {
        return $null
    }

    $busId = $null
    if ($line -match '(?<busid>\d+-\d+)') {
        $busId = $Matches.busid
    }

    $state = 'Unknown'
    if ($line -match 'Attached') {
        $state = 'Attached'
    } elseif ($line -match 'Not shared') {
        $state = 'Not shared'
    } elseif ($line -match 'Shared') {
        $state = 'Shared'
    }

    return [pscustomobject]@{
        BusId = $busId
        State = $state
    }
}

function Test-DeviceAttached {
    param(
        [string]$Usbipd,
        [string]$HardwareId
    )
    $device = Get-Device -Usbipd $Usbipd -HardwareId $HardwareId
    return $device -and $device.State -eq 'Attached'
}

function Write-BindInstruction {
    param([string]$BusId)
    Write-Err 'Error: Device is not shared with WSL yet.'
    Write-Err '  Run once in Administrator PowerShell:'
    Write-Err "    usbipd bind --busid $BusId"
    Write-Err '  Then retry attaching the USB device from WSL.'
}

# --- Auto-attach ---

function Get-AutoAttachMarkerPath {
    param([string]$HardwareId)
    $safe = $HardwareId -replace ':', '-'
    return Join-Path $env:TEMP "serial-bridge-usbipd-auto-attach-$safe.pid"
}

function Test-AutoAttachDaemonRunning {
    param([string]$HardwareId)

    $marker = Get-AutoAttachMarkerPath -HardwareId $HardwareId
    if (Test-Path $marker) {
        $markerPid = Get-Content $marker -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($markerPid -match '^\d+$') {
            $markerProc = Get-Process -Id ([int]$markerPid) -ErrorAction SilentlyContinue
            if ($markerProc -and $markerProc.ProcessName -eq 'usbipd') {
                return $true
            }
        }
        Remove-Item $marker -Force -ErrorAction SilentlyContinue
    }

    $processes = @(Get-CimInstance Win32_Process -Filter "Name = 'usbipd.exe'" -ErrorAction SilentlyContinue)
    foreach ($proc in $processes) {
        $cmd = $proc.CommandLine
        if ($cmd -and $cmd -like '*--auto-attach*' -and $cmd -like "*$HardwareId*") {
            return $true
        }
    }

    return $false
}

function Start-AutoAttachDaemon {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [string]$Usbipd,
        [string]$HardwareId
    )

    if (Test-AutoAttachDaemonRunning -HardwareId $HardwareId) {
        return
    }

    $target = "usbipd auto-attach ($HardwareId)"
    if (-not $PSCmdlet.ShouldProcess($target, 'Start')) {
        return
    }

    Write-Info 'Starting auto-attach monitor ...'
    $daemon = Start-Process -FilePath $Usbipd -ArgumentList @(
        'attach', '--wsl', '--hardware-id', $HardwareId, '--auto-attach'
    ) -WindowStyle Minimized -PassThru
    if ($daemon) {
        $marker = Get-AutoAttachMarkerPath -HardwareId $HardwareId
        if ($PSCmdlet.ShouldProcess($marker, 'Write marker')) {
            Set-Content -Path $marker -Value $daemon.Id -NoNewline
        }
    }
}

function Enable-AutoAttachIfRequested {
    param(
        [string]$Usbipd,
        [string]$HardwareId,
        [string]$AutoAttach
    )
    if ($AutoAttach -ne '0') {
        Start-AutoAttachDaemon -Usbipd $Usbipd -HardwareId $HardwareId
    }
}

# --- Attach flow ---
#
# 1. Find usbipd
# 2. Look up device by hardware ID
# 3. Already attached  -> enable auto-attach, exit
# 4. Not shared        -> print bind instructions, exit
# 5. Attach via usbipd -> verify, enable auto-attach

$usbipd = Get-UsbipdPath
if (-not $usbipd) {
    Write-Err 'Error: Could not find usbipd on Windows.'
    Write-Err '  Install: winget install dorssel.usbipd-win'
    exit 1
}

$device = Get-Device -Usbipd $usbipd -HardwareId $HardwareId
if (-not $device) {
    Write-Err "Error: No device matching $HardwareId."
    Write-Err '  Check that the board is connected and powered on.'
    exit 1
}

Write-Info "Device $($device.BusId) ($HardwareId), state: $($device.State)"

if ($device.State -eq 'Attached') {
    Write-Info 'Already attached to WSL.'
    Enable-AutoAttachIfRequested -Usbipd $usbipd -HardwareId $HardwareId -AutoAttach $AutoAttach
    exit 0
}

if ($device.State -eq 'Not shared') {
    if (-not $device.BusId) {
        Write-Err 'Error: Could not parse BUSID from usbipd list output.'
        exit 1
    }
    Write-BindInstruction -BusId $device.BusId
    exit 2
}

if (-not (Wait-AndAttach -Usbipd $usbipd -HardwareId $HardwareId -BusId $device.BusId)) {
    Write-Err 'Error: The usbipd attach command failed.'
    Write-Err '  Keep WSL terminal open, wait a moment after replugging, and retry.'
    exit 1
}

Enable-AutoAttachIfRequested -Usbipd $usbipd -HardwareId $HardwareId -AutoAttach $AutoAttach
exit 0
