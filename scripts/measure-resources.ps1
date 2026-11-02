param(
    [ValidateRange(5,300)][int]$Seconds = 20,
    [string]$Label = 'Very Light RGB',
    [string]$ProcessName = 'VeryLightRGB',
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'local\resources-last.json')
)
$ErrorActionPreference = 'Stop'
$processes = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($processes.Count -ne 1) { throw 'Expected exactly one background process; close the menu first.' }
$rgbProcess = $processes[0]
try {
    $logicalProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    $initialCpu = $rgbProcess.TotalProcessorTime.TotalSeconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $samples = [Collections.Generic.List[object]]::new()
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 1000
        $rgbProcess.Refresh()
        if ($rgbProcess.HasExited) { throw 'The RGB process exited during measurement.' }
        $samples.Add([pscustomobject]@{WorkingSetMB=$rgbProcess.WorkingSet64/1MB;PrivateCommitMB=$rgbProcess.PrivateMemorySize64/1MB;Handles=$rgbProcess.HandleCount;Threads=$rgbProcess.Threads.Count})
    }
    $coreCpu = 100 * ($rgbProcess.TotalProcessorTime.TotalSeconds-$initialCpu) / $clock.Elapsed.TotalSeconds
    $result = [pscustomobject]@{
        Timestamp=(Get-Date).ToString('o'); Label=$Label; Version=$rgbProcess.MainModule.FileVersionInfo.FileVersion
        ProcessId=$rgbProcess.Id; Seconds=[Math]::Round($clock.Elapsed.TotalSeconds,2); LogicalProcessors=$logicalProcessors
        CpuPercentOneCore=[Math]::Round($coreCpu,3); CpuPercentMachine=[Math]::Round($coreCpu/$logicalProcessors,3)
        MeanWorkingSetMB=[Math]::Round(($samples.WorkingSetMB | Measure-Object -Average).Average,1)
        MinWorkingSetMB=[Math]::Round(($samples.WorkingSetMB | Measure-Object -Minimum).Minimum,1)
        MaxWorkingSetMB=[Math]::Round(($samples.WorkingSetMB | Measure-Object -Maximum).Maximum,1)
        MeanPrivateCommitMB=[Math]::Round(($samples.PrivateCommitMB | Measure-Object -Average).Average,1)
        Handles=$samples[-1].Handles; Threads=$samples[-1].Threads; WindowTitle=$rgbProcess.MainWindowTitle
    }
    $fullOutput = [IO.Path]::GetFullPath($OutputPath)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullOutput)) | Out-Null
    $json = $result | ConvertTo-Json
    [IO.File]::WriteAllText($fullOutput, $json, [Text.UTF8Encoding]::new($false))
    Write-Output $json
} finally { $rgbProcess.Dispose() }
