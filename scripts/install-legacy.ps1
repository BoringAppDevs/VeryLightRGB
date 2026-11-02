param([switch]$NoLaunch)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'artifacts\RgbTray\RgbTray.exe'
if (-not (Test-Path -LiteralPath $source)) { throw 'Run scripts\build.ps1 first.' }
if (Get-Process RgbTray -ErrorAction SilentlyContinue) { throw 'Exit RGB Tray from its tray menu before installing.' }

$data = Join-Path $env:LOCALAPPDATA 'RgbTray'
$install = Join-Path $env:LOCALAPPDATA 'Programs\RgbTray'
$runPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -ItemType Directory -Path $data,$install -Force | Out-Null
$backupPath = Join-Path $data 'integration-backup.json'
if (-not (Test-Path -LiteralPath $backupPath)) {
    $run = Get-Item -LiteralPath $runPath
    @{
        SignalRgb = $run.GetValue('SignalRgb', $null)
        RgbTray = $run.GetValue('RgbTray', $null)
    } | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding utf8
}

# Stop the watchdog before its child, otherwise it immediately respawns SignalRGB.
Get-Process SignalRgbLauncher -ErrorAction SilentlyContinue | Stop-Process
Get-Process SignalRgb -ErrorAction SilentlyContinue | Stop-Process
Copy-Item -LiteralPath $source -Destination (Join-Path $install 'RgbTray.exe') -Force
$exe = Join-Path $install 'RgbTray.exe'
Remove-ItemProperty -LiteralPath $runPath -Name SignalRgb -ErrorAction SilentlyContinue
$startupOutput = Join-Path $data 'startup-registration.json'
$startupProcess = Start-Process -FilePath $exe -ArgumentList @('--enable-startup','--output',('"' + $startupOutput + '"')) -WindowStyle Hidden -Wait -PassThru
if ($startupProcess.ExitCode -ne 0) { throw (Get-Content -LiteralPath $startupOutput -Raw) }

$shell = New-Object -ComObject WScript.Shell
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Programs')) 'RGB Tray.lnk'
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $install
$shortcut.Description = 'Simple lighting control for MSI Mortar and Gainward Phoenix'
$shortcut.Save()
if (-not $NoLaunch) { Start-Process -FilePath $exe -WindowStyle Hidden }
Write-Output "Installed: $exe"
Write-Output "Startup backup: $backupPath"
