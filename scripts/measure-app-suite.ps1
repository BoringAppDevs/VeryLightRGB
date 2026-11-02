param(
    [Parameter(Mandatory=$true)][string]$Label,
    [Parameter(Mandatory=$true)][string]$NamePattern,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$Version = '',
    [string]$Scenario = 'background',
    [ValidateRange(5,600)][int]$Seconds = 60,
    [string[]]$DiskRoots = @()
)
$ErrorActionPreference = 'Stop'
if (-not ('RgbBenchmarkDisk' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class RgbBenchmarkDisk {
    [StructLayout(LayoutKind.Sequential)]
    public struct FileStandardInfo {
        public long AllocationSize;
        public long EndOfFile;
        public uint NumberOfLinks;
        [MarshalAs(UnmanagedType.U1)] public bool DeletePending;
        [MarshalAs(UnmanagedType.U1)] public bool Directory;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool GetFileInformationByHandleEx(SafeFileHandle file, int infoClass, out FileStandardInfo info, uint size);
    public static long AllocatedBytes(string path) {
        using (var handle = CreateFileW(path, 0, 7, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) return -1;
            FileStandardInfo info;
            if (!GetFileInformationByHandleEx(handle, 1, out info, (uint)Marshal.SizeOf(typeof(FileStandardInfo)))) return -1;
            return info.AllocationSize;
        }
    }
}
'@
}
$logicalProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$clock = [Diagnostics.Stopwatch]::StartNew()
$samples = [Collections.Generic.List[object]]::new()
$previous = @{}
$cpuTicks = [double]0
$identityNames = @{}
$start = Get-Date
while ($samples.Count -lt 2 -or ($samples[-1].ElapsedSeconds-$samples[0].ElapsedSeconds) -lt $Seconds) {
    if ($clock.Elapsed.TotalSeconds -gt $Seconds+120) { throw 'Performance counters did not advance within the expected duration.' }
    $inventory = @(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,CreationDate)
    $byId = @{}
    foreach ($entry in $inventory) { $byId[[int]$entry.ProcessId] = $entry }
    $selected = @{}
    foreach ($entry in $inventory) {
        if ($entry.Name -match $NamePattern) { $selected[[int]$entry.ProcessId] = $true }
    }
    do {
        $added = $false
        foreach ($entry in $inventory) {
            if ($selected.ContainsKey([int]$entry.ParentProcessId) -and -not $selected.ContainsKey([int]$entry.ProcessId) -and $entry.CreationDate -ge $byId[[int]$entry.ParentProcessId].CreationDate) {
                $selected[[int]$entry.ProcessId] = $true
                $added = $true
            }
        }
    } while ($added)
    $raw = @(Get-CimInstance Win32_PerfRawData_PerfProc_Process | Where-Object { $selected.ContainsKey([int]$_.IDProcess) })
    if ($raw.Count -eq 0) { throw "No processes found for $Label ($NamePattern)" }
    $current = @{}
    $details = @()
    foreach ($entry in $raw) {
        $processId = [int]$entry.IDProcess
        $birth = $byId[$processId].CreationDate
        $identity = "$processId/$($birth.ToString('o'))"
        $ticks = [double]$entry.PercentProcessorTime
        $current[$identity] = $ticks
        $identityNames[$identity] = $entry.Name
        if ($previous.ContainsKey($identity)) {
            $delta = $ticks - [double]$previous[$identity]
            if ($delta -lt 0) { throw 'CPU counter decreased for a stable process identity.' }
            $cpuTicks += $delta
        } elseif ($samples.Count -gt 0 -and $birth -ge $start) {
            $cpuTicks += $ticks
        }
        $details += [pscustomobject]@{ ProcessId=$processId; ParentProcessId=$byId[$processId].ParentProcessId; CreatedAt=$birth.ToString('o'); Name=$entry.Name; CpuTicks100ns=$ticks; WorkingSetBytes=[long]$entry.WorkingSet; PrivateWorkingSetBytes=[long]$entry.WorkingSetPrivate; PrivateCommitBytes=[long]$entry.PrivateBytes }
    }
    $samples.Add([pscustomobject]@{
        ElapsedSeconds=$clock.Elapsed.TotalSeconds
        CounterTimestamp100ns=[double]$raw[0].Timestamp_Sys100NS
        ProcessCount=$raw.Count
        WorkingSetMiB=($raw | Measure-Object WorkingSet -Sum).Sum/1MB
        PrivateWorkingSetMiB=($raw | Measure-Object WorkingSetPrivate -Sum).Sum/1MB
        PrivateCommitMiB=($raw | Measure-Object PrivateBytes -Sum).Sum/1MB
        Processes=$details
    })
    $previous=$current
    if ($samples.Count -lt 2 -or ($samples[-1].ElapsedSeconds-$samples[0].ElapsedSeconds) -lt $Seconds) { Start-Sleep -Milliseconds 1000 }
}
$duration=$samples[-1].ElapsedSeconds-$samples[0].ElapsedSeconds
$disk=@(foreach ($root in $DiskRoots) {
    $errors=@()
    if (Test-Path -LiteralPath $root) {
        $files=@(Get-ChildItem -LiteralPath $root -File -Recurse -Force -ErrorAction SilentlyContinue -ErrorVariable +errors)
        $allocated=[long]0
        $allocationErrors=0
        foreach ($file in $files) {
            $bytes=[RgbBenchmarkDisk]::AllocatedBytes($file.FullName)
            if ($bytes -lt 0) { $allocationErrors++ } else { $allocated += $bytes }
        }
        [pscustomobject]@{ Root=$root; FileCount=$files.Count; LogicalBytes=[long](($files | Measure-Object Length -Sum).Sum); AllocatedBytes=$allocated; ReadErrors=$errors.Count; AllocationErrors=$allocationErrors }
    }
})
$result=[ordered]@{
    Label=$Label; Version=$Version; Scenario=$Scenario; Timestamp=$start.ToString('o')
    LogicalProcessors=$logicalProcessors; DurationSeconds=[math]::Round($duration,3); SampleCount=$samples.Count
    CpuTimeSeconds=$cpuTicks/1e7
    CpuPercentMachine=[math]::Round(100*$cpuTicks/1e7/$duration/$logicalProcessors,5)
    MeanWorkingSetMiB=[math]::Round(($samples | Measure-Object WorkingSetMiB -Average).Average,3)
    MeanPrivateWorkingSetMiB=[math]::Round(($samples | Measure-Object PrivateWorkingSetMiB -Average).Average,3)
    MeanPrivateCommitMiB=[math]::Round(($samples | Measure-Object PrivateCommitMiB -Average).Average,3)
    PeakWorkingSetMiB=[math]::Round(($samples | Measure-Object WorkingSetMiB -Maximum).Maximum,3)
    ProcessNames=@($identityNames.Values | Sort-Object -Unique); Disk=$disk
    Caveats=@('Working sets summed across processes can double-count shared pages.', 'Processes that start and exit entirely between samples are not captured.', 'Disk includes logical lengths and FileStandardInfo.AllocationSize; installer downloads are excluded. NTFS metadata and hard-link deduplication are not accounted for.')
    Samples=@($samples)
}
$fullOutput=[IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullOutput)) | Out-Null
[IO.File]::WriteAllText($fullOutput,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$result.Remove('Samples')
$result | ConvertTo-Json -Depth 5
