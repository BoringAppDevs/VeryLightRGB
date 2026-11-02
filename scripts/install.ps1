param([switch]$NoLaunch,[switch]$Legacy)
$ErrorActionPreference='Stop'
if($Legacy){& (Join-Path $PSScriptRoot 'install-legacy.ps1') -NoLaunch:$NoLaunch}
else{& (Join-Path $PSScriptRoot 'install-native.ps1') -NoLaunch:$NoLaunch}
