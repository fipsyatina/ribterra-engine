$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $projectDir 'vcpkg.json') -Raw | ConvertFrom-Json
$version = [string]$manifest.'version-string'
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
$relativeExecutable = 'dist\releases\' + $version + '\Ribterra\Ribterra.exe'
$existingPackage = Join-Path $projectDir ('dist\releases\' + $version + '\Ribterra')
$requiredFiles = @('Ribterra.exe', 'RibterraServer.exe', 'SDL3.dll', 'SDL3_ttf.dll', 'assets\manifest.json', 'Play Ribterra.cmd')
$complete = $true
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $existingPackage $file) -PathType Leaf)) { $complete = $false }
}
if ($complete) {
    [IO.File]::WriteAllText((Join-Path $projectDir 'dist\current-build.txt'), $relativeExecutable, [Text.Encoding]::ASCII)
    exit 0
}
$repository = 'fipsyatina/ribterra'
$releaseUrl = 'https://github.com/' + $repository + '/releases/tag/v' + $version
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host ('Download the Windows ZIP at: ' + $releaseUrl)
    Write-Host 'This source archive needs a ready build. Sign in to GitHub to access the private release.'
    Start-Process $releaseUrl
    exit 1
}
& gh auth status *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host ('Sign in with gh auth login, or download the Windows ZIP at: ' + $releaseUrl)
    Start-Process $releaseUrl
    exit 1
}
$archiveName = 'Ribterra-Windows-x64-' + $version + '.zip'
$downloadDir = Join-Path $projectDir ('.deps\release-download-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null
Write-Host ('Downloading Ribterra ' + $version + '...')
& gh release download ('v' + $version) --repo $repository --pattern $archiveName --pattern ($archiveName + '.sha256') --dir $downloadDir
if ($LASTEXITCODE -ne 0) { throw ('Download failed. Open ' + $releaseUrl) }
$archive = Join-Path $downloadDir $archiveName
$checksum = (Get-Content -LiteralPath ($archive + '.sha256') -Raw).Trim() -split '\s+'
if ($checksum.Count -ne 2 -or $checksum[0] -notmatch '^[a-fA-F0-9]{64}$' -or $checksum[1] -ne $archiveName) { throw 'Invalid checksum file.' }
$stream = [IO.File]::OpenRead($archive)
$hasher = [Security.Cryptography.SHA256]::Create()
try { $actualHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
finally { $stream.Dispose(); $hasher.Dispose() }
if ($actualHash -ne $checksum[0]) { throw 'Download checksum mismatch.' }
# Use framework APIs: a CMD launcher may inherit PowerShell 7's module paths
# while Windows powershell.exe runs 5.1, where Archive/Utility autoload can fail.
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($archive, (Join-Path $downloadDir 'unpacked'))
$packageDir = Join-Path $downloadDir 'unpacked\Ribterra'
foreach ($file in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $file) -PathType Leaf)) { throw ('Incomplete package: ' + $file) }
}
$releaseDir = [IO.Path]::GetFullPath((Join-Path $projectDir ('dist\releases\' + $version)))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectDir 'dist\releases')) + [IO.Path]::DirectorySeparatorChar
if (-not $releaseDir.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid release destination.' }
$targetDir = Join-Path $releaseDir 'Ribterra'
if (Test-Path -LiteralPath $targetDir) { throw ('Release directory already exists: ' + $targetDir) }
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
$resolvedSource = (Resolve-Path -LiteralPath $packageDir).Path
if (-not $resolvedSource.StartsWith(([IO.Path]::GetFullPath($downloadDir) + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid extracted package path.' }
Move-Item -LiteralPath $resolvedSource -Destination $targetDir
[IO.File]::WriteAllText((Join-Path $projectDir 'dist\current-build.txt'), $relativeExecutable, [Text.Encoding]::ASCII)
Write-Host ('Installed and SHA-256 verified: Ribterra ' + $version)
