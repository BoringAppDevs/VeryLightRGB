# SPDX-License-Identifier: GPL-3.0-or-later
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$directory=Join-Path $repo 'local/inno-setup'
$compiler=Join-Path $directory 'compiler/ISCC.exe'
if(Test-Path -LiteralPath $compiler){Write-Output $compiler;return}
$url='https://github.com/jrsoftware/issrc/releases/download/is-7_1_1/innosetup-7.1.1-x64.exe'
$digest='8a5a6711c3766a6e5fb74f280ba0e3551e719ce94538d901fdbd1f8f9920b3f0'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$download=Join-Path $directory 'innosetup-7.1.1-x64.exe'
if(-not(Test-Path -LiteralPath $download)){Invoke-WebRequest $url -OutFile $download}
if((Get-FileHash -LiteralPath $download).Hash.ToLowerInvariant() -ne $digest){throw 'Inno Setup hash mismatch'}
$signature=Get-AuthenticodeSignature -LiteralPath $download
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*Pyrsys B.V.*'){throw 'Inno Setup signature mismatch'}
$process=Start-Process -FilePath $download -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOICONS',('/DIR="'+(Join-Path $directory 'compiler')+'"')) -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0 -or -not(Test-Path $compiler)){throw 'Could not install the verified Inno Setup compiler'}
Write-Output $compiler
