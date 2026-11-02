param([switch]$SourceOnly)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$root=Join-Path $repo 'third_party\openrgb'
$version=Get-Content -LiteralPath (Join-Path $root 'version.json') -Raw | ConvertFrom-Json
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($asset in @($version.Binary,$version.Source)){
    $path=Join-Path $dist $asset.File
    if(-not(Test-Path -LiteralPath $path)){
        $temporary=$path+'.download'
        Invoke-WebRequest -Uri $asset.Url -OutFile $temporary
        if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.Sha256){throw "OpenRGB hash mismatch: $($asset.File)"}
        Move-Item -LiteralPath $temporary -Destination $path -Force
    }
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.Sha256){throw "OpenRGB hash mismatch: $($asset.File)"}
}
if($SourceOnly){Write-Output (Join-Path $dist $version.Source.File);return}
$runtime=Join-Path $root 'runtime'
$exe=Join-Path $runtime 'OpenRGB Windows 64-bit\OpenRGB.exe'
if(-not(Test-Path -LiteralPath $exe)){
    Expand-Archive -LiteralPath (Join-Path $dist $version.Binary.File) -DestinationPath $runtime -Force
}
$base=Split-Path $exe -Parent
$inventory=Get-Content -LiteralPath (Join-Path $root 'runtime-sha256.json') -Raw | ConvertFrom-Json
foreach($entry in $inventory){
    $file=Join-Path $base $entry.Path
    if(-not(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Sha256){throw "OpenRGB runtime changed or incomplete: $($entry.Path)"}
}
Write-Output $exe
