param(
    [ValidateRange(1024,65535)][int]$Port=6742,
    [string]$ConfigDirectory=(Join-Path $env:LOCALAPPDATA 'VeryLightRGB\OpenRGB'),
    [ValidateSet('Configured','All','ThisPc','None')][string]$Detection='Configured',
    [switch]$Gui
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$exe=& (Join-Path $PSScriptRoot 'setup-openrgb.ps1')
$config=[IO.Path]::GetFullPath($ConfigDirectory)
New-Item -ItemType Directory -Path $config -Force | Out-Null
$settingsPath=Join-Path $config 'OpenRGB.json'
$statePath=Join-Path $config 'very-light-server.json'
if(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue){throw "Port $Port is already in use; no process was replaced."}
if(Get-Process OpenRGB -ErrorAction SilentlyContinue){throw 'An OpenRGB instance already exists. Stop it or use its SDK server.'}
$settings=@{}
if(Test-Path -LiteralPath $settingsPath){
    $json=Get-Content -LiteralPath $settingsPath -Raw
    if($PSVersionTable.PSVersion.Major -ge 6){$settings=$json | ConvertFrom-Json -AsHashtable}
    else{
        # Windows PowerShell 5.1 lacks -AsHashtable. Preserve case-sensitive detector names.
        Add-Type -AssemblyName System.Web.Extensions
        $serializer=New-Object System.Web.Script.Serialization.JavaScriptSerializer
        $settings=$serializer.DeserializeObject($json)
    }
}
$settings.Server=@{default_host='127.0.0.1';default_port=$Port;all_controllers=$false;legacy_workaround=$false}
if(-not $settings.ContainsKey('Detectors')){$settings.Detectors=@{}}
$detectors=[Collections.Generic.Dictionary[string,bool]]::new([StringComparer]::Ordinal)
$names=Get-Content -LiteralPath (Join-Path $repo 'third_party\openrgb\detector-names-1.1.json') -Raw | ConvertFrom-Json
foreach($name in $names){$detectors[$name]=$Detection -eq 'All'}
if($Detection -eq 'ThisPc'){
    foreach($name in @('MSI Mystic Light X870','Gainward GeForce RTX 5080 Phoenix','Keychron RGB QMK/ZMK Keyboard')){$detectors[$name]=$true}
}
if($Detection -ne 'Configured'){$settings.Detectors.detectors=$detectors}
$settings | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $settingsPath -Encoding utf8
$configArgument=Join-Path $config '.'
$arguments=@('--server','--noautoconnect','--server-host','127.0.0.1','--server-port',"$Port",'--config',('"'+$configArgument+'"'))
if($Detection -eq 'None'){$arguments+='--nodetect'}
if($Gui){$arguments+='--gui'}
$windowStyle=if($Gui){'Normal'}else{'Hidden'}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path $exe -Parent) -WindowStyle $windowStyle -PassThru
$state=@{ProcessId=$process.Id;Executable=$exe;StartedUtc=$process.StartTime.ToUniversalTime().ToString('o');Port=$Port;Detection=$Detection;ConfigDirectory=$config}
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding utf8
$deadline=(Get-Date).AddSeconds(30)
do{
    $process.Refresh()
    if($process.HasExited){throw "OpenRGB exited ($($process.ExitCode)); inspect $config\logs"}
    $listener=Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $process.Id
    if($listener){$state | ConvertTo-Json;return}
    Start-Sleep -Milliseconds 200
}while((Get-Date) -lt $deadline)
throw 'OpenRGB SDK server did not start within 30 seconds. The owned process is recorded for stop-openrgb.ps1.'
