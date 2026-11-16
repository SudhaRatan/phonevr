<#
 PhoneVR installer (run in PowerShell, after building, from the host folder):
     .\install.ps1 -Package .\build\phonevr
 What it does:
   1. copies the driver to %LOCALAPPDATA%\PhoneVR\phonevr
   2. downloads Google's platform-tools (adb) next to the driver (needed for USB mode)
   3. registers the driver with SteamVR (vrpathreg)
   4. lets PhoneVR be the only HMD, so SteamVR starts without a physical headset
 Undo with:  .\install.ps1 -Uninstall
#>
param(
    [string]$Package = ".\build\phonevr",
    [switch]$Uninstall,
    [switch]$SkipAdb,
    [switch]$SkipSteamVRSettings
)
$ErrorActionPreference = "Stop"

function Find-SteamVR {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { throw "Steam was not found in the registry. Install Steam + SteamVR first." }
    $steam = $steam -replace "/", "\"
    $libs = @($steam)
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $vdf) {
        Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object {
            $libs += ($_.Matches[0].Groups[1].Value -replace "\\\\", "\")
        }
    }
    foreach ($l in $libs) {
        $p = Join-Path $l "steamapps\common\SteamVR"
        if (Test-Path (Join-Path $p "bin\win64\vrpathreg.exe")) { return @{ Steam = $steam; VR = $p } }
    }
    throw "SteamVR is not installed (looked in: $($libs -join ', '))."
}

$sv = Find-SteamVR
$vrpathreg = Join-Path $sv.VR "bin\win64\vrpathreg.exe"
$dest = Join-Path $env:LOCALAPPDATA "PhoneVR\phonevr"

if ($Uninstall) {
    & $vrpathreg removedriver $dest | Out-Null
    $cfg = Join-Path $sv.Steam "config\steamvr.vrsettings"
    if (Test-Path "$cfg.phonevr-backup") { Copy-Item "$cfg.phonevr-backup" $cfg -Force }
    Remove-Item (Join-Path $env:LOCALAPPDATA "PhoneVR") -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "PhoneVR removed."
    return
}

if (-not (Test-Path (Join-Path $Package "bin\win64\driver_phonevr.dll"))) {
    throw "Driver not found in '$Package'. Build first (see README) or pass -Package."
}
Get-Process vrserver, vrmonitor, vrdashboard -ErrorAction SilentlyContinue | Stop-Process -Force

if (Test-Path $dest) {
    # keep the user's tweaks to settings across upgrades
    $keep = Join-Path $dest "resources\settings\default.vrsettings"
    if (Test-Path $keep) { Copy-Item $keep "$env:TEMP\phonevr.default.vrsettings" -Force }
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $Package "*") $dest -Recurse -Force
if (Test-Path "$env:TEMP\phonevr.default.vrsettings") {
    Copy-Item "$env:TEMP\phonevr.default.vrsettings" (Join-Path $dest "resources\settings\default.vrsettings") -Force
    Remove-Item "$env:TEMP\phonevr.default.vrsettings"
}

if (-not $SkipAdb) {
    $adbDir = Join-Path $dest "bin\win64\adb"
    if (-not (Test-Path (Join-Path $adbDir "adb.exe"))) {
        Write-Host "Downloading adb (Android platform-tools)..."
        $zip = Join-Path $env:TEMP "platform-tools.zip"
        Invoke-WebRequest "https://dl.google.com/android/repository/platform-tools-latest-windows.zip" -OutFile $zip
        $tmp = Join-Path $env:TEMP "phonevr-pt"
        Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Expand-Archive $zip $tmp -Force
        New-Item -ItemType Directory -Force -Path $adbDir | Out-Null
        Copy-Item (Join-Path $tmp "platform-tools\*") $adbDir -Recurse -Force
        Remove-Item $zip, $tmp -Recurse -Force
    }
}

& $vrpathreg adddriver $dest | Out-Null
Write-Host "Driver registered with SteamVR: $dest"

if (-not $SkipSteamVRSettings) {
    $cfg = Join-Path $sv.Steam "config\steamvr.vrsettings"
    if (Test-Path $cfg) {
        if (-not (Test-Path "$cfg.phonevr-backup")) { Copy-Item $cfg "$cfg.phonevr-backup" }
        $json = Get-Content $cfg -Raw | ConvertFrom-Json
    } else {
        $json = [pscustomobject]@{}
    }
    if (-not $json.PSObject.Properties["steamvr"]) {
        $json | Add-Member -NotePropertyName steamvr -NotePropertyValue ([pscustomobject]@{})
    }
    $s = $json.steamvr
    $wanted = [ordered]@{ forcedDriver = "phonevr"; activateMultipleDrivers = $true; enableHomeApp = $false }
    foreach ($name in $wanted.Keys) {
        $value = $wanted[$name]
        if ($s.PSObject.Properties[$name]) { $s.PSObject.Properties[$name].Value = $value }
        else { $s | Add-Member -NotePropertyName $name -NotePropertyValue $value }
    }
    $json | ConvertTo-Json -Depth 32 | Set-Content $cfg -Encoding UTF8
    Write-Host "SteamVR configured to use PhoneVR (backup: steamvr.vrsettings.phonevr-backup)"
}

Write-Host ""
Write-Host "Done. Start SteamVR, then open the PhoneVR app on the phone."
Write-Host "Tune lenses/bitrate in: $dest\resources\settings\default.vrsettings"
