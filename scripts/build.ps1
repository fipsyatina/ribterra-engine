param([switch]$SkipBootstrap)
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $projectDir 'vcpkg.json') -Raw | ConvertFrom-Json
$version = [string]$manifest.'version-string'
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Build version must use major.minor.patch.' }
$releaseDir = Join-Path $projectDir ('dist\releases\' + $version + '\Ribterra')
if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot 'bootstrap.ps1')
    & (Join-Path $PSScriptRoot 'bootstrap-engine.ps1')
}
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ and CMake tools.' }
$visualStudioDir = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudioDir) { throw 'MSVC x64 compiler not found. Install the C++ workload.' }
$cmakeDir = Join-Path $visualStudioDir 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmakeExe = Join-Path $cmakeDir 'cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw 'Install the Visual Studio C++ CMake tools component.' }
$commandFile = Join-Path $projectDir '.deps\build-local.cmd'
$env:RIBTERRA_BUILD_ROOT = $projectDir
$env:RIBTERRA_VS_ROOT = $visualStudioDir
$env:RIBTERRA_CMAKE_BIN = $cmakeDir
$env:RIBTERRA_INSTALL_DIR = $releaseDir
$lines = @(
    '@echo off',
    'call "%RIBTERRA_VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul',
    'if errorlevel 1 exit /b %errorlevel%',
    'cd /d "%RIBTERRA_BUILD_ROOT%"',
    '"%RIBTERRA_CMAKE_BIN%\cmake.exe" --preset windows-release',
    'if errorlevel 1 exit /b %errorlevel%',
    '"%RIBTERRA_CMAKE_BIN%\cmake.exe" --build --preset windows-release --parallel',
    'if errorlevel 1 exit /b %errorlevel%',
    '"%RIBTERRA_CMAKE_BIN%\ctest.exe" --preset windows-release',
    'if errorlevel 1 exit /b %errorlevel%',
    '"%RIBTERRA_CMAKE_BIN%\cmake.exe" --install build/engine-windows --prefix "%RIBTERRA_INSTALL_DIR%"',
    'exit /b %errorlevel%'
)
[IO.File]::WriteAllLines($commandFile, $lines, [Text.Encoding]::ASCII)
& $env:ComSpec /d /c $commandFile
if ($LASTEXITCODE -ne 0) { throw ('Build or validation failed: ' + $LASTEXITCODE) }
$relativeExecutable = 'dist\releases\' + $version + '\Ribterra\Ribterra.exe'
[IO.File]::WriteAllText((Join-Path $projectDir 'dist\current-build.txt'), $relativeExecutable, [Text.Encoding]::ASCII)
Write-Host ('Built and verified: ' + $relativeExecutable)
