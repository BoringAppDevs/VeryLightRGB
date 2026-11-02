param([switch]$Tests)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$compiler = Join-Path $repo 'local/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Run scripts/setup-native-toolchain.ps1 first.' }
$output = Join-Path $repo 'artifacts/device-db'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$flags = @('-std=c++20', '-O2', '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0A00', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-I', "$repo/native/vendor", '-I', "$repo/device-db/native", '-static', '-municode')
$flags += @("-ffile-prefix-map=$repo=.", ('-ffile-prefix-map=' + $repo.Replace('\', '/') + '=.'), '-fdebug-compilation-dir=.')
$source = Join-Path $repo 'device-db/native/main.cpp'
$name = 'native-device-db.exe'
if ($Tests) { $source = Join-Path $repo 'device-db/tests/native_tests.cpp'; $name = 'native-device-db-tests.exe' }
& $compiler @flags $source '-lsetupapi' '-lhid' '-ladvapi32' '-o' (Join-Path $output $name)
if ($LASTEXITCODE -ne 0) { throw 'Native device database compilation failed' }
foreach ($file in @('catalog.json', 'protocols.json', 'summary.json', 'sources.lock.json', 'providers.json')) {
    Copy-Item -LiteralPath (Join-Path $repo "device-db/$file") -Destination $output -Force
}
Get-Item -LiteralPath (Join-Path $output $name) | Select-Object FullName,Length
