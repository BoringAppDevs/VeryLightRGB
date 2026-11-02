$ErrorActionPreference = 'Stop'
$nativeExe = Join-Path $env:LOCALAPPDATA 'Programs\VeryLightRGB\VeryLightRGB.exe'
if(Test-Path -LiteralPath $nativeExe){
    Start-Process -FilePath $nativeExe -ArgumentList '--exit' -WindowStyle Hidden -Wait
    Get-Process VeryLightRGB -ErrorAction SilentlyContinue | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue
    if(Get-Process VeryLightRGB -ErrorAction SilentlyContinue){throw 'Close the Very Light RGB menu first.'}
    $removal=Start-Process -FilePath $nativeExe -ArgumentList '--disable-startup' -WindowStyle Hidden -Wait -PassThru
    if($removal.ExitCode -ne 0){throw 'Could not disable Very Light RGB startup.'}
}
if (Get-Process RgbTray -ErrorAction SilentlyContinue) { throw 'Exit RGB Tray using its tray menu first.' }
$backupPath = Join-Path $env:LOCALAPPDATA 'RgbTray\integration-backup.json'
if (-not (Test-Path -LiteralPath $backupPath)) { throw 'Startup backup not found.' }
$backup = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
$rgbExe = Join-Path $env:LOCALAPPDATA 'Programs\RgbTray\RgbTray.exe'
if (Test-Path -LiteralPath $rgbExe) {
    $startupOutput = Join-Path $env:LOCALAPPDATA 'RgbTray\startup-removal.json'
    $startupProcess = Start-Process -FilePath $rgbExe -ArgumentList @('--disable-startup','--output',('"' + $startupOutput + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($startupProcess.ExitCode -ne 0) { throw (Get-Content -LiteralPath $startupOutput -Raw) }
}
$runPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
foreach ($name in @('SignalRgb','RgbTray')) {
    $value = $backup.$name
    if ($null -eq $value) { Remove-ItemProperty -LiteralPath $runPath -Name $name -ErrorAction SilentlyContinue }
    else { New-ItemProperty -LiteralPath $runPath -Name $name -Value $value -PropertyType String -Force | Out-Null }
}
$launcher = Join-Path $env:LOCALAPPDATA 'VortxEngine\SignalRgbLauncher.exe'
if (Test-Path -LiteralPath $launcher) { Start-Process -FilePath $launcher -ArgumentList '--silent' -WindowStyle Hidden }
Write-Output 'Previous startup entries restored. RGB app files and settings were kept.'
