param([int]$StaticSeconds=22)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $env:LOCALAPPDATA 'Programs/VeryLightRGB/VeryLightRGB.exe'
$run=Join-Path $repo ('local/native-live-check-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
function Invoke-Agent([object]$command){
    $inputFile=Join-Path $run 'request.json';$outputFile=Join-Path $run 'response.json'
    $command | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $inputFile -Encoding utf8
    $p=Start-Process -FilePath $exe -ArgumentList @('--request',('"'+$inputFile+'"'),'--output',('"'+$outputFile+'"')) -WindowStyle Hidden -Wait -PassThru
    $result=Get-Content -LiteralPath $outputFile -Raw | ConvertFrom-Json
    if($p.ExitCode -ne 0){throw ($result | ConvertTo-Json -Depth 8 -Compress)}
    return $result
}
function Status { Invoke-Agent @{Command='status'} }
function Apply([object]$config) { $s=Status; Invoke-Agent @{Command='set-config';Revision=$s.Revision;Config=$config} | Out-Null }
function Ready([int]$DriverCount=-1) {
    $deadline=(Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 200
        $s=Status
        $driversReady=@($s.Status | Where-Object {$_.Id -like 'driver:*'}).Count
        if($s.Status.Count -gt 0 -and $s.AppliedRevision -eq $s.Revision -and ($DriverCount -lt 0 -or $driversReady -eq $DriverCount) -and @($s.Status | Where-Object {-not $_.Success}).Count -eq 0){return $s}
    }while((Get-Date)-lt$deadline)
    $s | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $run 'not-ready.json') -Encoding utf8
    throw ('Native outputs did not become ready: '+$s.Message+'; '+(($s.Status | Where-Object {-not $_.Success} | Select-Object -ExpandProperty Message) -join '; '))
}
function Core {
    $path=Join-Path (Split-Path $exe -Parent) 'native-drivers/native-driver-core.exe'
    @(Get-CimInstance Win32_Process -Filter "Name = 'native-driver-core.exe'" | Where-Object {$_.ExecutablePath -eq $path})
}
$initial=Status
$saved=$initial.Config
$saved | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $run 'config-before.json') -Encoding utf8
$drivers=@($saved.Catalog | Where-Object {$_.Id -like 'driver:*'})
if($drivers.Count -eq 0){throw 'No physical native-driver outputs selected for this opt-in test'}
$report=[ordered]@{Version=$initial.Version;DriverOutputs=@($drivers | Select-Object Name,LedCount);SoftwareReadbackOnly=$true}
try {
    $test=$saved | ConvertTo-Json -Depth 40 | ConvertFrom-Json
    $test.Enabled=$true;$test.Effect=0
    $palette=@('#B00000','#00B000','#0000B0');$index=0
    foreach($d in $test.Devices){
        if(-not $d.Managed -and $d.Id -notlike 'driver:*'){continue}
        if($d.Id -like 'driver:*'){$d.Managed=$true}
        $d.UseGlobal=$false;$d.HasCustomStyle=$true;$d.Style.Effect=0;$d.Style.Enabled=$true
        if($d.Id -like 'driver:*'){$d.Style.Color=$palette[$index%3];$d.Style.Brightness=15;$index++}
    }
    Apply $test;Start-Sleep -Milliseconds 700
    $state=Ready $drivers.Count
    if($state.Animating){throw 'Static mode kept the animation loop active'}
    $before=@(Core);if($before.Count -ne 1){throw 'Expected one native core worker'}
    Start-Sleep -Seconds $StaticSeconds
    $state=Ready $drivers.Count;$after=@(Core)
    if($after.Count -ne 1 -or $after[0].ProcessId -ne $before[0].ProcessId){throw 'Static lighting dropped its native worker'}
    $report.StaticSessionSeconds=$StaticSeconds;$report.StaticSessionStable=$true
    $test.Enabled=$false;Apply $test;Start-Sleep -Milliseconds 500;$null=Ready $drivers.Count
    $test.Enabled=$true;Apply $test;Start-Sleep -Milliseconds 500;$null=Ready $drivers.Count
    $report.OffOn=$true
    $victim=@(Core);if($victim.Count -ne 1){throw 'Cannot identify the owned driver worker'}
    Stop-Process -Id $victim[0].ProcessId
    $deadline=(Get-Date).AddSeconds(25);$recovered=$false
    do {
        Start-Sleep -Milliseconds 250
        $current=@(Core);$state=Status
        if($current.Count -eq 1 -and $current[0].ProcessId -ne $victim[0].ProcessId -and @($state.Status | Where-Object {-not $_.Success}).Count -eq 0){$recovered=$true;break}
    }while((Get-Date)-lt$deadline)
    if(-not $recovered){throw 'Native worker did not reconnect after termination'}
    $report.WorkerRecovery=$true
    $connections=@(Get-NetTCPConnection -OwningProcess $current[0].ProcessId -ErrorAction SilentlyContinue)
    if($connections.Count){throw 'Native driver worker opened TCP connections'}
    $report.NoTcpServer=$true
} finally {
    Apply $saved;Start-Sleep -Milliseconds 700
    $wanted=@($saved.Devices | Where-Object {$_.Managed -and $_.Id -like 'driver:*'}).Count
    $restored=Ready $wanted
    if(($restored.Config | ConvertTo-Json -Depth 40 -Compress) -ne ($saved | ConvertTo-Json -Depth 40 -Compress)){throw 'The original lighting configuration was not restored exactly'}
    $report.OriginalConfigurationRestored=$true
    $report | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $run 'report.json') -Encoding utf8
}
$report | ConvertTo-Json -Depth 10
Write-Output "Report: $run"
