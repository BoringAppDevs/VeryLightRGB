# SPDX-License-Identifier: GPL-3.0-or-later
# Opt-in real per-user installation test. Stops the app, saves its complete state,
# tests fresh install/upgrade/uninstall, then restores the user's state and upgrades it.
param([string]$Installer)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$payload=Join-Path $repo 'artifacts/VeryLightRGB'
$maintenance=Join-Path $payload 'VeryLightRGB.exe'
$version=(Get-Item $maintenance).VersionInfo.ProductVersion
if(-not $Installer){$Installer=Join-Path $repo ("artifacts/VeryLightRGB-$version-Setup-win-x64.exe")}
$Installer=(Resolve-Path -LiteralPath $Installer).Path
$root=Join-Path $repo ('local/clean-install-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$install=Join-Path $env:LOCALAPPDATA 'Programs/VeryLightRGB'
$data=Join-Path $env:LOCALAPPDATA 'VeryLightRGB'
$legacy=Join-Path $env:LOCALAPPDATA 'RgbTray'
$exe=Join-Path $install 'VeryLightRGB.exe'
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$taskName='VeryLightRGB-'+$sid
$task=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
$shortcuts=@((Join-Path ([Environment]::GetFolderPath('Programs')) 'Very Light RGB.lnk'),(Join-Path ([Environment]::GetFolderPath('Desktop')) 'Very Light RGB.lnk'))
$originalMenus=@(Get-CimInstance Win32_Process -Filter "Name = 'VeryLightRGB.exe'" | Where-Object {$_.ExecutablePath -eq $exe -and $_.CommandLine -match '(?:^|\s)--menu(?:\s|$)'}).Count -gt 0
$report=[ordered]@{Version=$version;Scope='Clean per-user application state on this Windows host; not a fresh Windows VM';Checks=@();Restored=$false}
function Check([bool]$condition,[string]$name){if(-not $condition){throw $name};$report.Checks+=@($name);Write-Output ('PASS: '+$name)}
function Run([string]$file,[string[]]$arguments){
    $p=Start-Process -FilePath $file -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(-not $p.WaitForExit(90000)){throw ('Process timed out: '+$file)}
    if($p.ExitCode -ne 0){throw ('Process failed ('+$p.ExitCode+'): '+$file+' '+($arguments -join ' '))}
}
function Status {
    $output=Join-Path $root 'status.json'
    Run $exe @('--status','--output',('"'+$output+'"'))
    Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
}
function Wait-Applied {
    $deadline=(Get-Date).AddSeconds(75)
    do {
        $s=Status
        if($s.Version -eq $version -and $s.AppliedRevision -eq $s.Revision -and $s.Message -notlike '*Запуск*' -and $s.Reload.State -notin @('queued','running') -and @($s.Status).Count -gt 0){return $s}
        Start-Sleep -Milliseconds 300
    }while((Get-Date) -lt $deadline)
    throw 'Agent did not finish applying its configuration'
}
function Preserve-Directory([string]$path,[string]$name){
    # Each target is an explicit per-user application folder, never an inferred parent.
    $absolute=[IO.Path]::GetFullPath($path)
    $allowed=@($install,$data,$legacy) | ForEach-Object {[IO.Path]::GetFullPath($_)}
    $destination=[IO.Path]::GetFullPath((Join-Path $root $name))
    if($absolute -notin $allowed -or -not $destination.StartsWith([IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Backup target escaped the intended application directories'}
    if(Test-Path -LiteralPath $absolute){
        [IO.Directory]::Move($absolute,$destination)
        if(Test-Path -LiteralPath $absolute){throw ('Application folder was recreated during backup: '+$absolute)}
    }
}
function Install([string]$name,[string]$tasks){
    Run $Installer @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOLAUNCH=1',('/TASKS="'+$tasks+'"'),('/LOG="'+(Join-Path $root ($name+'.log'))+'"'))
}
function Startup-Values {
    $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
    $values=[ordered]@{}
    if($key){try{foreach($name in ($key.GetValueNames() | Sort-Object)){$values[$name]=$key.GetValue($name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}}finally{$key.Dispose()}}
    $values | ConvertTo-Json -Depth 8 -Compress
}
$moved=@();$shortcutsSaved=@();$completed=$false;$stateTaken=$false;$isolated=$false;$taskSaved=$false
try {
    # Persist a live snapshot before shutting down, in addition to backing up all files.
    if(Test-Path $exe){
        $live=Join-Path $root 'original-live.json'
        $p=Start-Process $exe -ArgumentList @('--status','--output',('"'+$live+'"')) -WindowStyle Hidden -Wait -PassThru
        if($p.ExitCode -eq 0){$stateTaken=$true}
    }
    Run $maintenance @('--installer-stop','--target-exe',('"'+$exe+'"'))
    if($task){Export-ScheduledTask -TaskName $taskName | Set-Content (Join-Path $root 'original-task.xml');$taskSaved=$true;Unregister-ScheduledTask -TaskName $taskName -Confirm:$false}
    foreach($pair in @(@($install,'install-before'),@($data,'data-before'),@($legacy,'legacy-before'))){
        if(Test-Path -LiteralPath $pair[0]){Preserve-Directory $pair[0] $pair[1];$moved+=,@($pair[0],$pair[1])}
    }
    for($i=0;$i -lt $shortcuts.Count;$i++){
        if(Test-Path -LiteralPath $shortcuts[$i]){Copy-Item -LiteralPath $shortcuts[$i] -Destination (Join-Path $root ('shortcut-'+$i+'.lnk'));$shortcutsSaved+=,$i;Remove-Item -LiteralPath $shortcuts[$i]}
    }
    $isolated=$true
    $preflight=@{InstallExists=(Test-Path $install);DataExists=(Test-Path $data);TaskExists=[bool](Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue);InstallPath=$install;DataPath=$data;Backup=$root}
    $preflight | ConvertTo-Json | Set-Content (Join-Path $root 'isolation.json')
    Check (-not $preflight.InstallExists -and -not $preflight.DataExists -and -not $preflight.TaskExists) 'No previous app files, settings or startup task before clean install'
    $runBefore=Startup-Values
    Install 'fresh' 'startup'
    Check ((Get-Item $exe).VersionInfo.ProductVersion -eq $version) 'Setup installed the expected application version'
    Check ((Test-Path (Join-Path $install 'SOURCE.zip')) -and (Test-Path (Join-Path $install 'LICENSE'))) 'Installed package includes license and corresponding source'
    Run $exe @('--installer-check','--output',('"'+(Join-Path $root 'bundle-check.json')+'"'))
    Check ((Get-Content (Join-Path $root 'bundle-check.json') -Raw | ConvertFrom-Json).DriverBundle -eq 'ok') 'Installed child driver and DLLs start without hardware detection'
    $newTask=Get-ScheduledTask -TaskName $taskName
    Check ($newTask.Actions.Execute -eq $exe -and $newTask.Principal.RunLevel -eq 'Limited') 'Startup task targets this installation without elevation'
    Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null
    $s=Wait-Applied
    $s | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $root 'fresh-status.json')
    Check ($s.Config.Backend -eq 0 -and $s.Config.Language -eq 'en') 'Clean first run uses Native Auto and English defaults'
    Check (@($s.Status | Where-Object {-not $_.Success}).Count -eq 0) 'Clean first run applies all selected hardware outputs successfully'
    Check (@($s.Config.Catalog | Where-Object {$_.Id -like 'driver:*'}).Count -ge 3) 'Clean first run discovers native Razer zones without a server'
    $freshSettings=[IO.File]::ReadAllText((Join-Path $data 'settings.json'))
    Run $exe @('--installer-stop')
    $broken=$freshSettings | ConvertFrom-Json
    foreach($d in $broken.Devices | Where-Object {$_.Id -eq 'native:msi:argb:0'}){$d.Managed=$true}
    foreach($d in $broken.Catalog | Where-Object {$_.Id -like 'driver:*'}){$d.Driver.Detector='__VLR_NONEXISTENT_TEST_DETECTOR__'}
    $broken | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $data 'settings.json') -Encoding utf8
    Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null
    $partial=Wait-Applied
    Check (@($partial.Status | Where-Object {$_.Id -like 'native:*' -and $_.Success}).Count -ge 2 -and @($partial.Status | Where-Object {$_.Id -like 'driver:*' -and -not $_.Success}).Count -ge 3) 'A failed driver detector leaves MSI and GPU outputs operational'
    Run $exe @('--installer-stop')
    [IO.File]::WriteAllText((Join-Path $data 'settings.json'),$freshSettings)
    Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null
    $s=Wait-Applied
    $s.Config.Color='#123456';$s.Config.Brightness=15;$s.Config.Effect=0;$s.Config.Language='uk'
    $request=Join-Path $root 'upgrade-profile.request.json'
    @{Command='set-config';Revision=$s.Revision;Config=$s.Config} | ConvertTo-Json -Depth 40 | Set-Content $request -Encoding utf8
    Run $exe @('--request',('"'+$request+'"'),'--output',('"'+(Join-Path $root 'upgrade-profile.response.json')+'"'))
    $null=Wait-Applied
    $settingsHash=(Get-FileHash (Join-Path $data 'settings.json')).Hash
    Start-Process $exe -ArgumentList '--menu' -WindowStyle Hidden | Out-Null
    Install 'upgrade' ''
    Check ((Get-FileHash (Join-Path $data 'settings.json')).Hash -eq $settingsHash) 'Upgrade with a running agent and menu preserves all settings exactly'
    Check (-not(Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) 'Startup opt-out removes only the owned startup task'
    Run $Installer @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOLAUNCH=1',('/LOG="'+(Join-Path $root 'repeat-upgrade.log')+'"'))
    Check (-not(Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) 'An unattended upgrade preserves startup disabled in the app'
    Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null
    $s=Wait-Applied
    Check ($s.Config.Color -eq '#123456' -and $s.Config.Language -eq 'uk' -and @($s.Status | Where-Object {-not $_.Success}).Count -eq 0) 'Upgraded agent reapplies saved colors and language successfully'
    Run $exe @('--menu-self-test')
    $menu=Get-Content (Join-Path $data 'menu-self-test.json') -Raw | ConvertFrom-Json
    Check ($menu.TitleVerified -and @($menu.Rows -match ('v'+[regex]::Escape($version))).Count -gt 0) 'Installed menu renders the correct release version'
    # An uninstaller may not remove a startup task belonging to a portable copy.
    Run $maintenance @('--enable-startup')
    Run $exe @('--installer-remove-startup')
    Check ((Get-ScheduledTask -TaskName $taskName).Actions.Execute -eq $maintenance) 'Uninstall helper preserves a startup task owned by another copy'
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Run $exe @('--enable-startup')
    Start-Process $exe -ArgumentList '--menu' -WindowStyle Hidden | Out-Null
    Run (Join-Path $install 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $root 'uninstall.log')+'"'))
    Check (-not(Test-Path $exe)) 'Uninstall removes the application while its agent and menu were running'
    Check (-not(Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) 'Uninstall removes the owned startup task'
    Check ((Get-FileHash (Join-Path $data 'settings.json')).Hash -eq $settingsHash) 'Uninstall preserves user settings'
    $runAfter=Startup-Values
    Check ($runBefore -eq $runAfter) 'Install, upgrade and uninstall leave other applications startup values unchanged'
    $completed=$true
}finally {
    # Keep all test remnants for investigation; restore originals even on failure.
    if($isolated){
        Run $maintenance @('--installer-stop','--target-exe',('"'+$exe+'"'))
        if(-not $completed -and (Test-Path (Join-Path $install 'unins000.exe'))){
            try {Run (Join-Path $install 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')}
            catch {Write-Warning ('Test uninstall cleanup: '+$_.Exception.Message)}
        }
        $testTask=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if($testTask){Unregister-ScheduledTask -TaskName $taskName -Confirm:$false}
        Preserve-Directory $install 'install-after-test'
        Preserve-Directory $data 'data-after-test'
    }
    foreach($pair in $moved){[IO.Directory]::Move((Join-Path $root $pair[1]),$pair[0])}
    foreach($i in $shortcutsSaved){Copy-Item -LiteralPath (Join-Path $root ('shortcut-'+$i+'.lnk')) -Destination $shortcuts[$i] -Force}
    if($taskSaved){Register-ScheduledTask -TaskName $taskName -Xml (Get-Content (Join-Path $root 'original-task.xml') -Raw) -Force | Out-Null}
    $report.Restored=$true
    if($completed){
        # Leave the verified new release installed using the restored original user settings.
        Install 'final-user-upgrade' $(if($task -and $task.Settings.Enabled){'startup'}else{''})
        Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null
        $s=Wait-Applied
        if($stateTaken){
            $original=Get-Content (Join-Path $root 'original-live.json') -Raw | ConvertFrom-Json
            Check (($s.Config | ConvertTo-Json -Depth 40 -Compress) -eq ($original.Config | ConvertTo-Json -Depth 40 -Compress)) 'Final installed release preserves the original live configuration exactly'
        }
        $s | ConvertTo-Json -Depth 40 | Set-Content (Join-Path $root 'final-status.json')
        Check (@($s.Status | Where-Object {-not $_.Success}).Count -eq 0) 'Final user configuration is running successfully'
    }elseif(Test-Path $exe){Start-Process $exe -ArgumentList '--background' -WindowStyle Hidden | Out-Null}
    if($originalMenus -and (Test-Path $exe)){Start-Process $exe -ArgumentList '--menu' -WindowStyle Normal | Out-Null}
    $report.Completed=$completed
    $report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root 'report.json')
    Write-Output ('Report: '+(Join-Path $root 'report.json'))
}
