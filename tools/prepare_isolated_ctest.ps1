param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$LinkBinaries,
    [switch]$LinkRuntimeFiles
)

# Preserve previous test evidence and the real build assets. This prepares a
# fresh runtime snapshot only; running CTest and its fixture cleanup is a
# separate, explicitly disclosed operation.
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $BuildDirectory).Path
$destination = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) {
    throw "Output already exists; choose a new directory: $destination"
}
$testSource = Join-Path $source 'CTestTestfile.cmake'
$testHash = (Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash
$testContent = [System.IO.File]::ReadAllText($testSource)
if ($testContent -match '(?m)^subdirs\(') {
    throw 'Nested test directories need explicit snapshot support.'
}
foreach ($required in @('assets', 'compiled-shaders')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required) -PathType Container)) {
        throw "Missing build input: $required"
    }
}

$runtime = Join-Path $destination 'runtime'
$temporary = Join-Path $destination 'temp'
$repository = Split-Path -Parent $PSScriptRoot
$files = @(
    Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Extension -in '.exe', '.dll' }
    foreach ($folder in @('assets', 'compiled-shaders')) {
        Get-ChildItem -LiteralPath (Join-Path $source $folder) -File -Recurse
    }
)
$inputs = @($files | ForEach-Object {
    [pscustomobject]@{ Source = $_.FullName; Relative = [System.IO.Path]::GetRelativePath($source, $_.FullName) }
})
$fixture = Join-Path $repository 'tests/fixtures/world_save_v6_carry.ribsave'
if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) { throw "Missing compatibility fixture: $fixture" }
$inputs += [pscustomobject]@{ Source = $fixture; Relative = 'tests/fixtures/world_save_v6_carry.ribsave' }

# Hash before and after copying: do not silently certify mixed staging output.
$before = @{}
foreach ($inputFile in $inputs) {
    $before[$inputFile.Relative] = (Get-FileHash -LiteralPath $inputFile.Source -Algorithm SHA256).Hash
}
New-Item -ItemType Directory -Path $runtime, $temporary | Out-Null
$hashes = foreach ($inputFile in $inputs) {
    $copyPath = Join-Path $runtime $inputFile.Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $copyPath) -Force | Out-Null
    $method = 'copy'
    if ($LinkRuntimeFiles -or ($LinkBinaries -and [System.IO.Path]::GetExtension($inputFile.Source) -in '.exe', '.dll')) {
        # Tests execute these binaries and write outputs beside them, not into
        # them. Source builds must remain frozen until the after-run hash check.
        New-Item -ItemType HardLink -Path $copyPath -Target $inputFile.Source | Out-Null
        $method = 'hard-link-read-only-by-convention'
    } else {
        Copy-Item -LiteralPath $inputFile.Source -Destination $copyPath
    }
    $copy = (Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash
    $after = (Get-FileHash -LiteralPath $inputFile.Source -Algorithm SHA256).Hash
    if ($before[$inputFile.Relative] -ne $copy -or $copy -ne $after) {
        throw "Input changed while being copied: $($inputFile.Relative)"
    }
    [ordered]@{ name = $inputFile.Relative; sha256 = $copy; source = $inputFile.Source; method = $method }
}

$sourceCmake = $source.Replace('\', '/')
$runtimeCmake = $runtime.Replace('\', '/')
$isolatedTests = $testContent.Replace($sourceCmake, $runtimeCmake)
if ($isolatedTests.Contains($sourceCmake)) {
    throw 'A build-directory test path was not redirected.'
}
[System.IO.File]::WriteAllText((Join-Path $runtime 'CTestTestfile.cmake'), $isolatedTests,
    [System.Text.UTF8Encoding]::new($false))
if ($testHash -ne (Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash) {
    throw 'CTest configuration changed while preparing the snapshot.'
}
$externalPaths = @([regex]::Matches($testContent, '"([A-Za-z]:/[^";]+)"') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique |
    Where-Object { $_.TrimEnd('/') -ne $sourceCmake -and
        -not $_.StartsWith($sourceCmake + '/', [System.StringComparison]::OrdinalIgnoreCase) })
$externalFiles = @($externalPaths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
$manifest = [ordered]@{
    source_build = $source
    runtime = $runtime
    temp = $temporary
    scope = 'Output-isolated runtime; not a hermetic or relocatable source snapshot.'
    live_inputs_note = 'Python tests and repository inputs remain live. Listed command files are hashed at preparation; transitive inputs are not frozen. Do not edit source inputs during a run.'
    external_command_files = $externalFiles
    external_command_directories = @($externalPaths | Where-Object { Test-Path -LiteralPath $_ -PathType Container })
    source_ctest_sha256 = $testHash
    runtime_ctest_sha256 = (Get-FileHash -LiteralPath (Join-Path $runtime 'CTestTestfile.cmake') -Algorithm SHA256).Hash
    path_rewrite_only = $true
    linked_binaries = [bool]$LinkBinaries
    linked_runtime_files = [bool]$LinkRuntimeFiles
    linked_runtime_note = 'When enabled, ALL listed input contents are shared read-only by convention; outputs remain separate. This is not an immutable input snapshot. Freeze source/build/data writes until after-run verification.'
    linked_binaries_note = 'When enabled, EXE/DLL contents are shared with the frozen build; this is not an immutable binary snapshot. No builds or binary writes until after-run verification.'
    files = @($hashes)
}
[System.IO.File]::WriteAllText((Join-Path $destination 'snapshot.json'),
    ($manifest | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
Write-Output "Prepared $($inputs.Count) verified runtime files, including assets, shaders and compatibility fixture. No tests run and no files deleted."
Write-Output "CTest directory: $runtime"
Write-Output "Set TEMP and TMP to: $temporary"
Write-Output 'Set PYTHONDONTWRITEBYTECODE=1 before running CTest.'
