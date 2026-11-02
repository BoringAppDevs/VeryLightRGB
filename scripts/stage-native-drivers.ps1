param()
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$source=Join-Path $repo 'artifacts/device-db-core'
$destination=Join-Path $repo 'artifacts/VeryLightRGB/native-drivers'
if(-not(Test-Path -LiteralPath (Join-Path $source 'native-driver-core.exe'))){throw 'Build native driver core: node device-db/tools/build-core.mjs'}
$sources=Join-Path $destination 'sources'
New-Item -ItemType Directory -Path $destination,(Join-Path $sources 'device-db/core'),(Join-Path $sources 'device-db/tools'),(Join-Path $sources 'device-db/upstream'),(Join-Path $sources 'scripts') -Force | Out-Null
foreach($name in @('native-driver-core.exe','hidapi-hotplug.dll','libusb-1.0.dll','PawnIOLib.dll')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $source 'modules') -Destination $destination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo 'device-db/licenses') -Destination $destination -Recurse -Force
foreach($name in @('headless-cc71485c-source.tar.gz','mbedtls-3.2.1-source.tar.gz','hidapi-hotplug-source.tar.gz','libusb-source.tar.gz','pawnio-source.tar.gz','pawnio-modules-source.tar.gz')) {
    Copy-Item -LiteralPath (Join-Path $repo "device-db/upstream/$name") -Destination (Join-Path $sources 'device-db/upstream') -Force
}
foreach($name in @('direct_main.cpp','mingw_compat.hpp','sources.lock.json','module-provenance.json','README.md')) {
    Copy-Item -LiteralPath (Join-Path $repo "device-db/core/$name") -Destination (Join-Path $sources 'device-db/core') -Force
}
foreach($script in @('build-core.mjs','build-transports.mjs','fetch.ps1')) {Copy-Item -LiteralPath (Join-Path $repo "device-db/tools/$script") -Destination (Join-Path $sources 'device-db/tools') -Force}
Copy-Item -LiteralPath (Join-Path $repo 'scripts/setup-native-toolchain.ps1') -Destination (Join-Path $sources 'scripts') -Force
Copy-Item -LiteralPath (Join-Path $repo 'device-db/PROVENANCE.md') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $repo 'device-db/providers.json') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $repo 'device-db/catalog.json') -Destination $destination -Force
