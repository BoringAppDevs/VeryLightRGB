$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$toolDirectory=Join-Path $repo 'local\toolchain'
$compiler=Join-Path $toolDirectory 'llvm-mingw-20261020-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
if(Test-Path -LiteralPath $compiler){Write-Output $compiler;return}
$name='llvm-mingw-20261020-ucrt-x86_64.zip'
$sha256='fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd'
$url='https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/'+$name
New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
$archive=Join-Path $toolDirectory $name
if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $url -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256){throw 'Toolchain SHA-256 mismatch.'}
tar -xf $archive -C $toolDirectory
if($LASTEXITCODE -ne 0 -or -not(Test-Path -LiteralPath $compiler)){throw 'Toolchain extraction failed.'}
Write-Output $compiler
