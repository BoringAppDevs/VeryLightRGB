param([switch]$Offline,[switch]$Core,[string[]]$SourceId=@())
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$lockFile = if ($Core) { 'device-db/core/sources.lock.json' } else { 'device-db/sources.lock.json' }
$lock = Get-Content (Join-Path $repo $lockFile) -Raw | ConvertFrom-Json
$sources=@($lock.sources)
if($SourceId.Count){
    foreach($id in $SourceId){if($id -notin $sources.id){throw "Unknown pinned source: $id"}}
    $sources=@($sources | Where-Object {$_.id -in $SourceId})
}
foreach ($source in $sources) {
    $directory = Join-Path $repo $source.cacheDirectory
    $archive = Join-Path (Split-Path $directory -Parent) ($source.id + '.tar.gz')
    if ($source.vendoredArchive) {
        $archive = Join-Path $repo $source.vendoredArchive
    }
    New-Item -ItemType Directory -Path (Split-Path $directory -Parent) -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path $archive -Parent) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Offline source archive is missing: $archive" }
        $partial = $archive + '.partial'
        Invoke-WebRequest -Uri $source.archiveUrl -OutFile $partial
        if ((Get-FileHash -LiteralPath $partial).Hash -ne $source.archiveSha256) { throw "Source hash mismatch: $($source.id)" }
        Move-Item -LiteralPath $partial -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $source.archiveSha256) { throw "Source hash mismatch: $($source.id)" }
    $stamp = Join-Path $directory '.device-db-sha256'
    if (Test-Path -LiteralPath $stamp) {
        if ((Get-Content -LiteralPath $stamp -Raw).Trim() -ne $source.archiveSha256) { throw "Different snapshot already exists at $directory" }
    } else {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        # Refuse absolute paths and parent traversal before extracting the pinned archive.
        $entries = & tar -tzf $archive
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source archive' }
        foreach ($entry in $entries) {
            if ($entry -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))') { throw "Unsafe archive path: $entry" }
        }
        $exclude=@($source.excludeArchiveEntries | Where-Object {$_} | ForEach-Object {'--exclude='+$_})
        & tar -xzf $archive -C $directory --strip-components=1 @exclude
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($source.id)" }
        [IO.File]::WriteAllText($stamp, $source.archiveSha256)
    }
    Write-Output "$($source.id) $($source.revision) verified"
}
