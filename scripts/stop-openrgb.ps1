param([string]$ConfigDirectory=(Join-Path $env:LOCALAPPDATA 'VeryLightRGB\OpenRGB'))
$ErrorActionPreference='Stop'
$statePath=Join-Path ([IO.Path]::GetFullPath($ConfigDirectory)) 'very-light-server.json'
if(-not(Test-Path -LiteralPath $statePath)){throw 'No server owned by start-openrgb.ps1 was recorded.'}
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$process=Get-Process -Id $state.ProcessId -ErrorAction SilentlyContinue
if($process){
    $expectedStart=([datetime]$state.StartedUtc).ToUniversalTime()
    if($process.Path -ne $state.Executable -or $process.StartTime.ToUniversalTime().Ticks -ne $expectedStart.Ticks){throw 'PID has been reused; refusing to stop an unrelated process.'}
    $process | Stop-Process
    $process | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath $statePath
Write-Output 'Owned OpenRGB server stopped.'
