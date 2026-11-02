# SPDX-License-Identifier: GPL-3.0-or-later
# Fail before publishing if build metadata exposes a Windows user profile path.
param()
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$payload=Join-Path $repo 'artifacts/VeryLightRGB'
$version=(Get-Item (Join-Path $payload 'VeryLightRGB.exe')).VersionInfo.ProductVersion
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Unexpected application version'}
$binaries=@((Join-Path $payload 'VeryLightRGB.exe'))
$binaries+=Get-ChildItem -LiteralPath (Join-Path $payload 'native-drivers') -File | Where-Object {$_.Extension -in @('.exe','.dll')} | ForEach-Object FullName
foreach($binary in $binaries){
    $bytes=[IO.File]::ReadAllBytes($binary)
    foreach($text in @([Text.Encoding]::ASCII.GetString($bytes),[Text.Encoding]::Unicode.GetString($bytes))){
        if($text -match '(?i)[A-Z]:[\\/]+Users[\\/]+' -or $text -match 'S-1-5-21-\d+-\d+-\d+-\d+'){
            throw ('Private build metadata detected in '+[IO.Path]::GetFileName($binary))
        }
    }
}
$release=Join-Path $repo 'artifacts/release'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$downloads=[ordered]@{
    "VeryLightRGB-$version-Setup-win-x64.exe"='VeryLightRGB-Setup-win-x64.exe'
    "VeryLightRGB-$version-win-x64.zip"='VeryLightRGB-win-x64.zip'
    "VeryLightRGB-$version-source.zip"='VeryLightRGB-source.zip'
}
$checksums=@()
foreach($name in $downloads.Keys){
    $source=Join-Path $repo ('artifacts/'+$name)
    $target=Join-Path $release $downloads[$name]
    Copy-Item -LiteralPath $source -Destination $target -Force
    $checksums+=((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()+'  '+$downloads[$name])
}
[IO.File]::WriteAllText((Join-Path $release 'SHA256SUMS.txt'),($checksums -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
Write-Output ('Verified '+$binaries.Count+' binaries; prepared '+$downloads.Count+' downloads and SHA256SUMS.txt.')
