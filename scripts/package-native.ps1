param([switch]$WithOpenRgb)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$artifact=Join-Path $repo 'artifacts\VeryLightRGB'
$exe=Join-Path $artifact 'VeryLightRGB.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Run scripts\build.ps1 first.'}
Copy-Item -LiteralPath (Join-Path $repo 'native\PORTABLE.md') -Destination (Join-Path $artifact 'README.md') -Force
foreach($name in @('LICENSE','COPYRIGHT.md')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $artifact -Force}
& (Join-Path $PSScriptRoot 'package-source.ps1')
$files=@('VeryLightRGB.exe','README.md','LICENSE','COPYRIGHT.md','SOURCE.zip','THIRD-PARTY-NOTICES.txt','json.LICENSE.MIT','LLVM-RUNTIME-LICENSE.txt','native-drivers') | ForEach-Object {Join-Path $artifact $_}
$version=(Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if($WithOpenRgb){
    & (Join-Path $PSScriptRoot 'setup-openrgb.ps1') -SourceOnly | Out-Null
    $stage=Join-Path $repo ('local\package-openrgb-'+[guid]::NewGuid().ToString('N'))
    $thirdParty=Join-Path $stage 'third_party\openrgb'
    New-Item -ItemType Directory -Path $stage,(Join-Path $stage 'scripts'),(Join-Path $thirdParty 'dist'),(Join-Path $stage 'docs') -Force | Out-Null
    foreach($file in $files){Copy-Item -LiteralPath $file -Destination $stage -Recurse}
    foreach($script in @('setup-openrgb.ps1','start-openrgb.ps1','stop-openrgb.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $stage 'scripts')}
    foreach($name in @('README.md','LICENSE','version.json','runtime-sha256.json','detector-names-1.1.json')){Copy-Item -LiteralPath (Join-Path $repo ('third_party\openrgb\'+$name)) -Destination $thirdParty}
    Copy-Item -LiteralPath (Join-Path $repo 'third_party\openrgb\licenses') -Destination $thirdParty -Recurse
    $manifest=Get-Content -LiteralPath (Join-Path $thirdParty 'version.json') -Raw | ConvertFrom-Json
    foreach($asset in @($manifest.Binary,$manifest.Source)){Copy-Item -LiteralPath (Join-Path $repo ('third_party\openrgb\dist\'+$asset.File)) -Destination (Join-Path $thirdParty 'dist')}
    Copy-Item -LiteralPath (Join-Path $repo 'docs\openrgb-integration.md') -Destination (Join-Path $stage 'docs')
    $zip=Join-Path $repo ("artifacts\VeryLightRGB-$version-with-OpenRGB-1.1-win-x64.zip")
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal -Force
}else{
    $zip=Join-Path $repo ("artifacts\VeryLightRGB-$version-win-x64.zip")
    Compress-Archive -LiteralPath $files -DestinationPath $zip -CompressionLevel Optimal -Force
}
$digest=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip+'.sha256'),($digest+'  '+(Split-Path $zip -Leaf)+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $zip | Select-Object FullName,Length
