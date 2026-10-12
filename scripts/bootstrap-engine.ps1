$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$vcpkgDir = Join-Path $projectDir '.deps\vcpkg'
$baseline = '04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4'
if (-not (Test-Path -LiteralPath (Join-Path $vcpkgDir '.git'))) {
    git clone --filter=blob:none https://github.com/microsoft/vcpkg.git $vcpkgDir
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone vcpkg.' }
}
git -C $vcpkgDir checkout --detach $baseline
if ($LASTEXITCODE -ne 0) { throw 'Could not select pinned vcpkg baseline.' }
if (-not (Test-Path -LiteralPath (Join-Path $vcpkgDir 'vcpkg.exe'))) {
    & (Join-Path $vcpkgDir 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
}
& (Join-Path $PSScriptRoot 'prefetch-renderer-ports.ps1')
Write-Host 'Pinned engine dependency manager ready.'
