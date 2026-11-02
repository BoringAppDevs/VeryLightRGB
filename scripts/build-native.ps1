param([switch]$Prototype,[switch]$Tests)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $repo 'local\toolchain\llvm-mingw-20261020-ucrt-x86_64\bin\x86_64-w64-mingw32-clang++.exe'
if(-not(Test-Path -LiteralPath $compiler)){throw 'Portable LLVM-MinGW toolchain is missing; see native/README.md.'}
$outputDirectory=Join-Path $repo 'artifacts\VeryLightRGB'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$flags=@('-std=c++20','-O2','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-I',"$repo\native\include",'-I',"$repo\native\vendor")
$flags+=@("-ffile-prefix-map=$repo=.",('-ffile-prefix-map='+$repo.Replace('\','/')+'=.'),'-fdebug-compilation-dir=.')
$sources=@("$repo\native\src\core.cpp","$repo\native\src\hardware.cpp","$repo\native\src\driver.cpp")
$libraries=@('-lsetupapi','-lhid','-ladvapi32','-lshell32','-lole32','-loleaut32','-lbcrypt','-lpsapi','-lws2_32','-luuid','-luser32','-lgdi32','-ltaskschd')
$linkFlags=@('-static','-Wl,--gc-sections')
if($Prototype){$sources+=Join-Path $repo 'native\tests\prototype.cpp';$output=Join-Path $outputDirectory 'prototype.exe';$linkFlags+='-municode'}
else{
    $sources+=@("$repo\native\src\sdk.cpp","$repo\native\src\engine.cpp","$repo\native\src\ipc.cpp","$repo\native\src\startup.cpp","$repo\native\src\tui.cpp","$repo\native\src\localization.cpp")
    if($Tests){$sources+=@("$repo\native\tests\tests.cpp","$repo\native\tests\hardware_tests.cpp","$repo\native\tests\openrgb_tests.cpp","$repo\native\tests\driver_tests.cpp");$output=Join-Path $outputDirectory 'tests.exe'}
    else{$sources+=Join-Path $repo 'native\src\main.cpp';$output=Join-Path $outputDirectory 'VeryLightRGB.exe';$linkFlags+=@('-municode','-mwindows')}
}
$objectDirectory=Join-Path $repo 'local\native-objects'
New-Item -ItemType Directory -Path $objectDirectory -Force | Out-Null
$headerDigest=((Get-ChildItem "$repo\native\include\*.hpp","$repo\native\vendor\json.hpp" | Get-FileHash).Hash -join '')+($flags -join '|')
$objects=@()
foreach($source in $sources){
    $object=Join-Path $objectDirectory ((Split-Path $source -Leaf)+'.o')
    $digest=(Get-FileHash -LiteralPath $source).Hash+$headerDigest
    $stamp=$object+'.hash'
    if(-not(Test-Path -LiteralPath $object) -or -not(Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw) -ne $digest){
        & $compiler @flags '-c' $source '-o' $object
        if($LASTEXITCODE -ne 0){throw "Compilation failed: $source"}
        [IO.File]::WriteAllText($stamp,$digest)
    }
    $objects+=$object
}
if(-not $Prototype -and -not $Tests){
    $windres=Join-Path (Split-Path $compiler -Parent) 'x86_64-w64-mingw32-windres.exe'
    $resource=Join-Path $objectDirectory 'app-resource.o'
    & $windres '-I' "$repo\native\assets" '-i' "$repo\native\assets\app.rc" '-o' $resource '-O' 'coff'
    if($LASTEXITCODE -ne 0){throw 'Resource compilation failed.'}
    $objects+=$resource
}
& $compiler @linkFlags @objects @libraries '-o' $output
if($LASTEXITCODE -ne 0){throw 'Native build failed.'}
Get-Item -LiteralPath $output | Select-Object FullName,Length
