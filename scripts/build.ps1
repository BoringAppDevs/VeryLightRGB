param([switch]$Legacy)
$ErrorActionPreference='Stop'
if($Legacy){& (Join-Path $PSScriptRoot 'build-legacy.ps1');return}
$repo=Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $repo
try{
    & (Join-Path $PSScriptRoot 'setup-native-toolchain.ps1') | Out-Null
    & (Join-Path $repo 'device-db\tools\fetch.ps1') -Core | Out-Null
    node (Join-Path $repo 'device-db\tools\build-core.mjs')
    if($LASTEXITCODE -ne 0){throw 'Native driver core build failed.'}
    node (Join-Path $repo 'device-db/tools/index-core.mjs')
    if($LASTEXITCODE -ne 0){throw 'Native driver manifest failed.'}
    & (Join-Path $PSScriptRoot 'stage-native-drivers.ps1')
    & (Join-Path $PSScriptRoot 'build-native.ps1') -Tests
    & (Join-Path $repo 'artifacts\VeryLightRGB\tests.exe')
    if($LASTEXITCODE -ne 0){throw 'Native regression tests failed.'}
    & (Join-Path $PSScriptRoot 'build-native.ps1')
    $output=Join-Path $repo 'artifacts\VeryLightRGB'
    Copy-Item -LiteralPath (Join-Path $repo 'native\THIRD-PARTY-NOTICES.txt') -Destination (Join-Path $output 'THIRD-PARTY-NOTICES.txt') -Force
    Copy-Item -LiteralPath (Join-Path $repo 'native\vendor\json.LICENSE.MIT') -Destination (Join-Path $output 'json.LICENSE.MIT') -Force
    Copy-Item -LiteralPath (Join-Path $repo 'native\vendor\LLVM-RUNTIME-LICENSE.txt') -Destination (Join-Path $output 'LLVM-RUNTIME-LICENSE.txt') -Force
    foreach($name in @('LICENSE','COPYRIGHT.md')) {Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $output -Force}
}finally{Pop-Location}
