$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$dependencyDir = Join-Path $projectDir '.deps'
New-Item -ItemType Directory -Path $dependencyDir -Force | Out-Null
$packages = @(
    @{Name='SDL3-devel-3.4.16-VC.zip'; Repository='SDL'; Tag='release-3.4.16'; Hash='1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de'; Folder='SDL3-3.4.16'},
    @{Name='SDL3_ttf-devel-3.2.2-VC.zip'; Repository='SDL_ttf'; Tag='release-3.2.2'; Hash='67805c5babfc49ca0c56882dc9b8cabbcdd1e6f9edde10ddac91ddb38f3afb8c'; Folder='SDL3_ttf-3.2.2'}
)
foreach ($package in $packages) {
    $archive = Join-Path $dependencyDir $package.Name
    if (-not (Test-Path -LiteralPath $archive)) {
        $uri = 'https://github.com/libsdl-org/{0}/releases/download/{1}/{2}' -f $package.Repository,$package.Tag,$package.Name
        Write-Host ('Downloading ' + $package.Name)
        Invoke-WebRequest -Uri $uri -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) {
        throw ('SHA256 mismatch: ' + $package.Name + '. Remove this archive and retry.')
    }
    # Restore all SDK files from the verified archive, including after a partial extraction.
    Expand-Archive -LiteralPath $archive -DestinationPath $dependencyDir -Force
}
Write-Host 'Pinned SDL dependencies ready.'
