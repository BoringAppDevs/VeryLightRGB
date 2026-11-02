$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $repo
try {
    dotnet build src/RgbTray/RgbTray.csproj -c Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    dotnet run --project tests/RgbTray.Tests/RgbTray.Tests.csproj -c Release
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    dotnet publish src/RgbTray/RgbTray.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=None -o artifacts/RgbTray
    if ($LASTEXITCODE -ne 0) { throw 'Publish failed.' }
} finally { Pop-Location }
