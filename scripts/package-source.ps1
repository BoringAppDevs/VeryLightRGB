# SPDX-License-Identifier: GPL-3.0-or-later
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$payload=Join-Path $repo 'artifacts/VeryLightRGB'
$version=(Get-Item (Join-Path $payload 'VeryLightRGB.exe')).VersionInfo.ProductVersion
$zip=Join-Path $repo ("artifacts/VeryLightRGB-$version-source.zip")
Add-Type -AssemblyName System.IO.Compression
$stream=[IO.File]::Open($zip,[IO.FileMode]::Create)
$archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
try {
    $roots=@('native','scripts','installer','device-db','docs','src','tests','.github')
    $files=@('README.md','LICENSE','COPYRIGHT.md','.gitignore','.gitattributes','Directory.Build.props','RGB.sln') | ForEach-Object {Get-Item -LiteralPath (Join-Path $repo $_)}
    foreach($root in $roots){$files+=Get-ChildItem -LiteralPath (Join-Path $repo $root) -File -Recurse | Where-Object {$_.FullName -notmatch '[\\/](?:bin|obj)[\\/]'}}
    # SDK integration manifests are source; large optional GUI binaries are fetched separately.
    $files+=Get-ChildItem -LiteralPath (Join-Path $repo 'third_party/openrgb') -File
    $files+=Get-ChildItem -LiteralPath (Join-Path $repo 'third_party/openrgb/licenses') -File -Recurse
    foreach($file in ($files | Sort-Object FullName)){
        $relative=$file.FullName.Substring($repo.Length+1).Replace('\','/')
        $entry=$archive.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime=[DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero)
        $input=[IO.File]::OpenRead($file.FullName);$output=$entry.Open()
        try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}
    }
}finally{$archive.Dispose();$stream.Dispose()}
Copy-Item -LiteralPath $zip -Destination (Join-Path $payload 'SOURCE.zip') -Force
$hash=(Get-FileHash $zip).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip+'.sha256'),$hash+'  '+(Split-Path $zip -Leaf)+[Environment]::NewLine)
Get-Item $zip | Select-Object FullName,Length
