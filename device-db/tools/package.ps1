param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts = Join-Path $repo 'artifacts'
foreach ($required in @('device-db/native-device-db.exe','device-db-core/native-driver-core.exe','device-db-core/build-manifest.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $artifacts $required))) { throw "Missing build: $required" }
}
$stage = Join-Path $artifacts ('device-db-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage,(Join-Path $stage 'light'),(Join-Path $stage 'core'),(Join-Path $stage 'database'),(Join-Path $stage 'source/native/vendor') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $artifacts 'device-db/native-device-db.exe') -Destination (Join-Path $stage 'light')
foreach ($name in @('native-driver-core.exe','hidapi-hotplug.dll','libusb-1.0.dll','PawnIOLib.dll','build-manifest.json')) {
    Copy-Item -LiteralPath (Join-Path $artifacts "device-db-core/$name") -Destination (Join-Path $stage 'core')
}
Copy-Item -LiteralPath (Join-Path $artifacts 'device-db-core/modules') -Destination (Join-Path $stage 'core/modules') -Recurse
Get-ChildItem -LiteralPath (Join-Path $repo 'device-db') -Filter '*.json' -File | Copy-Item -Destination (Join-Path $stage 'database')
Copy-Item -LiteralPath (Join-Path $repo 'device-db/core/compiled-detectors.json') -Destination (Join-Path $stage 'database/core-detectors.json')
Copy-Item -LiteralPath (Join-Path $repo 'device-db/examples') -Destination (Join-Path $stage 'examples') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'device-db/licenses') -Destination (Join-Path $stage 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'native/vendor/json.LICENSE.MIT') -Destination (Join-Path $stage 'licenses/nlohmann-json-MIT.txt')
Copy-Item -LiteralPath (Join-Path $repo 'native/vendor/LLVM-RUNTIME-LICENSE.txt') -Destination (Join-Path $stage 'licenses/LLVM-RUNTIME-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repo 'device-db') -Destination (Join-Path $stage 'source/device-db') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'native/vendor/json.hpp') -Destination (Join-Path $stage 'source/native/vendor/json.hpp')
Copy-Item -LiteralPath (Join-Path $repo 'native/vendor/json.LICENSE.MIT') -Destination (Join-Path $stage 'source/native/vendor/json.LICENSE.MIT')
Copy-Item -LiteralPath (Join-Path $repo 'device-db/PORTABLE.md') -Destination (Join-Path $stage 'README.md')
$fingerprint = (Get-FileHash -LiteralPath (Join-Path $stage 'core/native-driver-core.exe')).Hash.Substring(0,8).ToLowerInvariant()
$zip = Join-Path $artifacts ("device-db-serverless-20261101-$fingerprint.zip")
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force -CompressionLevel Optimal
$manifest = Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    [ordered]@{File=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/'); Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($zip + '.manifest.json') -Encoding utf8
Get-Item -LiteralPath $zip | Select-Object FullName,Length
