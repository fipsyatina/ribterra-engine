param(
    [Parameter(Mandatory = $true)][string]$SnapshotDirectory,
    [Parameter(Mandatory = $true)][string]$NewEvidenceDirectory
)

$ErrorActionPreference = 'Stop'
$snapshotPath = (Resolve-Path -LiteralPath $SnapshotDirectory).Path
$evidencePath = [System.IO.Path]::GetFullPath($NewEvidenceDirectory)
if (Test-Path -LiteralPath $evidencePath) { throw 'Evidence exists; refusing overwrite.' }
$fixturePath = Join-Path $evidencePath 'fixture'
$missingCompatibility = Join-Path $evidencePath 'missing-compatibility'
New-Item -ItemType Directory -Path $fixturePath, $missingCompatibility | Out-Null

# Mutate only a NEW manifest, never any linked runtime/source input. Retain
# every fixture and failure report; no deletion or cleanup is performed.
$manifest = Get-Content -LiteralPath (Join-Path $snapshotPath 'snapshot.json') -Raw | ConvertFrom-Json
if (-not $manifest.linked_runtime_files) { throw 'Expected a linked-runtime fixture source.' }
$manifest.files[0].source = Join-Path $fixturePath 'never-created-source.bin'
$manifest.files[1].method = 'copy'
$manifest.external_command_files[0].path = Join-Path $fixturePath 'never-created-command.bin'
$manifest.files += [pscustomobject]@{
    name='never-created-runtime.bin'; source=$manifest.files[2].source;
    sha256=$manifest.files[2].sha256; method='hard-link-read-only-by-convention'
}
$encoding = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $fixturePath 'snapshot.json'), ($manifest | ConvertTo-Json -Depth 8), $encoding)
Copy-Item -LiteralPath (Join-Path $snapshotPath 'diagnostic-copy.json') -Destination (Join-Path $fixturePath 'diagnostic-copy.json')
$failureReport = Join-Path $evidencePath 'expected-failure.json'
$rejected = $false
try {
    & (Join-Path $PSScriptRoot 'verify_ground_forward_snapshot.ps1') -SnapshotDirectory $fixturePath `
        -NewReportPath $failureReport -CompatibilitySnapshotDirectory $missingCompatibility
} catch {
    if (-not $_.Exception.Message.StartsWith('Snapshot input changed; evidence retained at ')) { throw }
    $rejected = $true
}
$report = Get-Content -LiteralPath $failureReport -Raw | ConvertFrom-Json
$counts = [ordered]@{
    runtime=@($report.runtime_files | Where-Object { -not $_.unchanged }).Count
    source=@($report.source_files | Where-Object { -not $_.unchanged }).Count
    method=@($report.input_methods | Where-Object { -not $_.unchanged }).Count
    command=@($report.external_command_files | Where-Object { -not $_.unchanged }).Count
    compatibility=@($report.compatibility_executables | Where-Object { -not $_.unchanged }).Count
}
$passed = $rejected -and -not $report.passed -and $counts.runtime -eq 1 -and $counts.source -eq 1 `
    -and $counts.method -eq 1 -and $counts.command -eq 1 -and $counts.compatibility -eq 6
$summary = [ordered]@{ schema=1; passed=$passed; expected_rejection=$rejected;
    failure_report_retained=(Test-Path -LiteralPath $failureReport); detected=$counts;
    scope='Missing runtime/source/command files, wrong link method, and missing optional compatibility binaries must produce a failed ledger. Only a new manifest is mutated; linked input files are untouched.' }
[System.IO.File]::WriteAllText((Join-Path $evidencePath 'negative-report.json'), ($summary | ConvertTo-Json -Depth 4), $encoding)
if (-not $passed) { throw 'Snapshot negative verification failed; all evidence retained.' }
Write-Output "Verified missing-file/method failures are reported: $evidencePath"
