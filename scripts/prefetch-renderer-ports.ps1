$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$vcpkgDir = Join-Path $projectDir '.deps\vcpkg'
# Version resolution inspects the minimum-version ports as well as the baseline.
# Fetch their small Git blobs before vcpkg starts: nested lazy fetch from Git's
# checkout-index failed on this Windows host, while a direct fetch succeeds.
$compilerPort = Get-Content -LiteralPath (Join-Path $vcpkgDir 'ports\sdl3-shadercross\vcpkg.json') -Raw | ConvertFrom-Json
foreach ($dependency in $compilerPort.dependencies) {
    if ($dependency -is [string] -or -not $dependency.'version>=') { continue }
    $portName = [string]$dependency.name
    $minimum = [string]$dependency.'version>='
    $versionFile = Join-Path $vcpkgDir ('versions\' + $portName.Substring(0, 1) + '-\' + $portName + '.json')
    $versions = (Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json).versions
    $selected = $versions | Where-Object {
        $_.version -eq $minimum -or $_.'version-date' -eq $minimum -or
        $_.'version-semver' -eq $minimum -or $_.'version-string' -eq $minimum
    } | Select-Object -First 1
    if (-not $selected) { throw ('Pinned minimum port version not found: ' + $portName + '@' + $minimum) }
    $tree = [string]$selected.'git-tree'
    if ($tree -notmatch '^[a-f0-9]{40}$') { throw 'Invalid pinned port tree.' }
    $rows = git -C $vcpkgDir ls-tree -r $tree
    if ($LASTEXITCODE -ne 0) { throw ('Could not read pinned port tree: ' + $portName) }
    foreach ($row in $rows) {
        if ($row -match '^[0-9]+ blob ([a-f0-9]{40})') {
            $blob = $Matches[1]
            git -C $vcpkgDir cat-file -p $blob > $null
            if ($LASTEXITCODE -ne 0) { throw ('Could not fetch pinned port: ' + $portName) }
        }
    }
}
Write-Host 'Pinned shader compiler port metadata ready.'
