# SPDX-License-Identifier: GPL-3.0-or-later
param([switch]$NoLaunch)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $repo 'artifacts/VeryLightRGB/VeryLightRGB.exe'
if(-not(Test-Path $exe)){throw 'Run scripts/build.ps1 first.'}
$version=(Get-Item $exe).VersionInfo.ProductVersion
$installer=Join-Path $repo ("artifacts/VeryLightRGB-$version-Setup-win-x64.exe")
& (Join-Path $PSScriptRoot 'build-installer.ps1') -SkipBuild
$process=Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOLAUNCH=1') -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0){throw ("Installer failed with code "+$process.ExitCode)}
$registration=Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A38BD459-F630-4A2D-94F0-A2CA40ED3271}_is1'
$installed=Join-Path $registration.InstallLocation 'VeryLightRGB.exe'
if(-not $NoLaunch){Start-Process -FilePath $installed -ArgumentList '--background' -WindowStyle Hidden | Out-Null}
Write-Output "Installed: $installed"
Write-Output ("Settings: "+(Join-Path $env:LOCALAPPDATA 'VeryLightRGB'))
