param(
    [Parameter(Mandatory = $true)][string]$SnapshotDirectory,
    [Parameter(Mandatory = $true)][string]$NewReportPath,
    [string]$CompatibilitySnapshotDirectory,
    [string]$ShaderSourceDirectory
)

$ErrorActionPreference = 'Stop'
$snapshotPath = (Resolve-Path -LiteralPath $SnapshotDirectory).Path
$reportPath = [System.IO.Path]::GetFullPath($NewReportPath)
if (Test-Path -LiteralPath $reportPath) { throw 'Report already exists; refusing overwrite.' }
$manifest = Get-Content -LiteralPath (Join-Path $snapshotPath 'snapshot.json') -Raw | ConvertFrom-Json
function Get-HashRow([string]$Name, [string]$Path, [string]$Expected) {
    $actual = $null
    $errorText = $null
    $exists = $Path -and (Test-Path -LiteralPath $Path -PathType Leaf)
    if ($exists) {
        try { $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
        catch { $errorText = $_.Exception.Message }
    }
    [ordered]@{ name=$Name; path=$Path; exists=[bool]$exists; expected_sha256=$Expected;
        actual_sha256=$actual; unchanged=([bool]$exists -and $Expected -and $actual -eq $Expected); error=$errorText }
}
$rows = @(
    foreach ($file in $manifest.files) {
        $path = Join-Path $manifest.runtime $file.name
        Get-HashRow $file.name $path $file.sha256
    }
)
$sourceRows = @(
    foreach ($file in $manifest.files) {
        if ($file.source -or $manifest.linked_runtime_files -or $manifest.linked_binaries) {
            Get-HashRow $file.name $file.source $file.sha256
        }
    }
)
$methods = @(
    foreach ($file in $manifest.files) {
        $expected = if ($manifest.linked_runtime_files -or ($manifest.linked_binaries -and
            [System.IO.Path]::GetExtension($file.name) -in '.exe', '.dll')) {
            'hard-link-read-only-by-convention'
        } else { 'copy' }
        # Older copy snapshots may predate the method field. Linked inputs must
        # explicitly declare the method and a source; they are never immutable.
        $matches = ($file.method -eq $expected) -or (-not $file.method -and $expected -eq 'copy')
        [ordered]@{ name=$file.name; expected_method=$expected; method=$file.method; unchanged=$matches }
    }
)
$external = @(
    foreach ($file in $manifest.external_command_files) {
        Get-HashRow $file.path $file.path $file.sha256
    }
)
$diagnosticManifest = @(Get-Content -LiteralPath (Join-Path $snapshotPath 'diagnostic-copy.json') -Raw | ConvertFrom-Json)
$diagnostic = @(
    foreach ($file in $diagnosticManifest) {
        $path = Join-Path $manifest.runtime ('calibration-shaders/' + $file.name)
        Get-HashRow $file.name $path $file.sha256
    }
)
$diagnosticSources = @(
    foreach ($file in $diagnosticManifest) {
        Get-HashRow $file.name (Join-Path $manifest.source_build ('calibration-shaders/' + $file.name)) $file.sha256
    }
)
$ctest = @(
    Get-HashRow 'source CTest' (Join-Path $manifest.source_build 'CTestTestfile.cmake') $manifest.source_ctest_sha256
    Get-HashRow 'runtime CTest' (Join-Path $manifest.runtime 'CTestTestfile.cmake') $manifest.runtime_ctest_sha256
)
$shaderSources = @()
if ($ShaderSourceDirectory) {
    $shaderRoot = (Resolve-Path -LiteralPath $ShaderSourceDirectory).Path
    $shaderManifest = Get-Content -LiteralPath (Join-Path $manifest.runtime 'compiled-shaders/manifest.json') -Raw | ConvertFrom-Json
    $shaderSources = @(
        foreach ($shader in $shaderManifest.shaders) {
            Get-HashRow $shader.source (Join-Path $shaderRoot $shader.source) $shader.source_sha256
            foreach ($dependency in $shader.source_dependencies) {
                Get-HashRow ($shader.source + ' -> ' + $dependency.source) (Join-Path $shaderRoot $dependency.source) $dependency.sha256
            }
        }
    )
}
$compatibility = @()
if ($CompatibilitySnapshotDirectory) {
    $previousPath = (Resolve-Path -LiteralPath $CompatibilitySnapshotDirectory).Path
    $compatibility = @(
        foreach ($name in @('RenderFrameTests.exe','RenderBatchTests.exe','SurfaceLightingProbe.exe',
                            'HdrColorProbe.exe','MaterialBridgeProbe.exe','ReworkVisualProbe.exe')) {
            $previous = Get-HashRow $name (Join-Path $previousPath "runtime/$name") ''
            $current = Get-HashRow $name (Join-Path $manifest.runtime $name) $previous.actual_sha256
            [ordered]@{ name=$name; previous_sha256=$previous.actual_sha256; current_sha256=$current.actual_sha256;
                previous_exists=$previous.exists; current_exists=$current.exists;
                previous_error=$previous.error; current_error=$current.error;
                unchanged=($previous.exists -and $previous.actual_sha256 -and $current.unchanged) }
        }
    )
}
$allRows = @($rows) + @($sourceRows) + @($methods) + @($external) + @($diagnostic) + @($diagnosticSources) + @($ctest) + @($shaderSources) + @($compatibility)
$passed = @($allRows | Where-Object { -not $_.unchanged }).Count -eq 0
$report = [ordered]@{
    schema=2
    passed=$passed
    scope='After-run hashes of listed runtime/source pairs, command inputs, diagnostics and CTest. Optional shader source/include hashes come from the compiled manifest. Other transitive inputs remain live: not hermetic, immutable or relocatable.'
    snapshot=$snapshotPath
    linked_runtime_files=[bool]$manifest.linked_runtime_files
    linked_binaries=[bool]$manifest.linked_binaries
    runtime_count=$rows.Count
    source_count=$sourceRows.Count
    method_count=$methods.Count
    external_command_count=$external.Count
    diagnostic_count=$diagnostic.Count
    diagnostic_source_count=$diagnosticSources.Count
    ctest_count=$ctest.Count
    shader_source_and_dependency_count=$shaderSources.Count
    compatibility_executable_count=$compatibility.Count
    runtime_files=$rows
    source_files=$sourceRows
    input_methods=$methods
    external_command_files=$external
    diagnostic_files=$diagnostic
    diagnostic_source_files=$diagnosticSources
    ctest_files=$ctest
    shader_sources_and_dependencies=$shaderSources
    compatibility_executables=$compatibility
}
[System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))
if (-not $passed) { throw "Snapshot input changed; evidence retained at $reportPath" }
Write-Output "Verified runtime/source $($rows.Count)/$($sourceRows.Count), commands $($external.Count), diagnostics runtime/source $($diagnostic.Count)/$($diagnosticSources.Count), CTest $($ctest.Count), shader sources/includes $($shaderSources.Count), compatibility $($compatibility.Count): $reportPath"
