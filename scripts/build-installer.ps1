# SPDX-License-Identifier: GPL-3.0-or-later
param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if(-not $SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1')}
$payload=Join-Path $repo 'artifacts/VeryLightRGB'
Copy-Item -LiteralPath (Join-Path $repo 'native/PORTABLE.md') -Destination (Join-Path $payload 'README.md') -Force
foreach($name in @('LICENSE','COPYRIGHT.md')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $payload -Force}
& (Join-Path $PSScriptRoot 'package-source.ps1')
$compiler=& (Join-Path $PSScriptRoot 'setup-inno.ps1')
$version=(Get-Item (Join-Path $payload 'VeryLightRGB.exe')).VersionInfo.ProductVersion
& $compiler '/Qp' ('/DAppVersion='+$version) (Join-Path $repo 'installer/VeryLightRGB.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$installer=Join-Path $repo ("artifacts/VeryLightRGB-$version-Setup-win-x64.exe")
[IO.File]::WriteAllText(($installer+'.sha256'),(Get-FileHash $installer).Hash.ToLowerInvariant()+'  '+(Split-Path $installer -Leaf)+[Environment]::NewLine)
Get-Item $installer | Select-Object FullName,Length
