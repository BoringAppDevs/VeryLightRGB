param([string]$Executable = (Join-Path $env:LOCALAPPDATA 'Programs\RgbTray\RgbTray.exe'))
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$settings = Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'RgbTray\settings.json') -Raw | ConvertFrom-Json
if ($settings.Backend -eq 1) { throw 'This script verifies the native MSI/Gainward backend. Use --diagnose for OpenRGB.' }
function Get-TargetConfig([string]$id) { @($settings.Devices | Where-Object Id -eq $id) | Select-Object -First 1 }
function Get-Style([string]$id) { $device = Get-TargetConfig $id; if ($null -eq $device -or $device.UseGlobal) { return $settings }; return $device.Style }
function Get-Expected([string]$id) {
    $style = Get-Style $id
    @(1,3,5 | ForEach-Object { if ($settings.Enabled -and $style.Enabled) { [int][Math]::Floor([Convert]::ToInt32($style.Color.Substring($_,2),16) * $style.Brightness / 100) } else { 0 } })
}
$ids = @('native:msi:argb:0','native:msi:argb:1','native:msi:argb:2','native:gainward:5080:phoenix')
if ((Get-Process RgbTray -ErrorAction SilentlyContinue) -and $settings.Enabled) {
    foreach ($id in $ids) {
        $device=Get-TargetConfig $id; $style=Get-Style $id
        if (($null -eq $device -or $device.Managed) -and $style.Enabled -and $style.Brightness -gt 0 -and $style.Effect -ne 0 -and -not ($id -eq $ids[3] -and $style.Effect -eq 2)) {
            throw 'An active effect owns the hardware. Switch all managed devices to Solid or exit RGB Tray before readback.'
        }
    }
}
$local = Join-Path $repo 'local'; New-Item -ItemType Directory -Path $local -Force | Out-Null
$output = Join-Path $local 'hardware-readback.json'
$p = Start-Process -FilePath $Executable -ArgumentList @('--diagnose','--output',('"' + $output + '"')) -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0) { throw (Get-Content -LiteralPath $output -Raw) }
$report = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
$config = @(); for ($i=0; $i -lt $report.Msi.Config.Length; $i+=2) { $config += [Convert]::ToByte($report.Msi.Config.Substring($i,2),16) }
for ($h=0; $h -lt 3; $h++) {
    $device=Get-TargetConfig $ids[$h]; if ($null -ne $device -and -not $device.Managed) { continue }
    $expected=Get-Expected $ids[$h]; $offset=1+$h*16
    if ($config[$offset] -ne 2) { throw "Header $($h+1) is not in steady mode." }
    for ($c=0; $c -lt 4; $c++) { for ($channel=0; $channel -lt 3; $channel++) { if ($config[$offset+1+$c*3+$channel] -ne $expected[$channel]) { throw "MSI Header $($h+1) color mismatch." } } }
}
if ($config[-1] -ne 0) { throw 'Flash-save flag is enabled.' }
$gpu=Get-TargetConfig $ids[3]
if ($null -eq $gpu -or $gpu.Managed) {
    $expected=Get-Expected $ids[3]
    foreach ($item in @(@('0x6C',0),@('0x6D',1),@('0x6E',2))) { if ($report.Gainward.Registers.($item[0]) -ne $expected[$item[1]]) { throw "GPU register $($item[0]) mismatch." } }
    if ($report.Gainward.Registers.'0x60' -ne 1) { throw 'GPU is not in static mode.' }
}
Write-Output 'PASS: All managed native zones match their own saved colors; flash-save is disabled.'
Write-Output "Read-only report: $output"
