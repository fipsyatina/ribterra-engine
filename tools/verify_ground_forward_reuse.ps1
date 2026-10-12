param(
    [Parameter(Mandatory = $true)][string]$Probe,
    [Parameter(Mandatory = $true)][string]$KitDirectory,
    [Parameter(Mandatory = $true)][string]$ExistingOutputDirectory,
    [Parameter(Mandatory = $true)][string]$NewEvidenceDirectory
)

$ErrorActionPreference = 'Stop'
$probePath = (Resolve-Path -LiteralPath $Probe).Path
$kitPath = (Resolve-Path -LiteralPath $KitDirectory).Path
$outputPath = (Resolve-Path -LiteralPath $ExistingOutputDirectory).Path
$evidencePath = [System.IO.Path]::GetFullPath($NewEvidenceDirectory)
if (Test-Path -LiteralPath $evidencePath) { throw 'Evidence already exists; refusing to overwrite.' }
if (-not (Test-Path -LiteralPath $outputPath -PathType Container)) { throw 'Expected an existing output directory.' }
if ($evidencePath.StartsWith($outputPath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must be outside the checked output directory.' }

function Get-Ledger([string]$Directory) {
    $result = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Directory -File -Recurse) {
        $relative = [System.IO.Path]::GetRelativePath($Directory, $file.FullName).Replace('\', '/')
        $result[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $result
}

$before = Get-Ledger $outputPath
if ($before.Count -eq 0) { throw 'No prior evidence files were found.' }
$refusal = & $probePath $kitPath $outputPath 2>&1
$probeExit = $LASTEXITCODE
$after = Get-Ledger $outputPath
$names = @(@($before.Keys) + @($after.Keys) | Sort-Object -Unique)
$rows = @(
    foreach ($name in $names) {
        [ordered]@{
            relative_path = $name
            before_sha256 = $before[$name]
            after_sha256 = $after[$name]
            unchanged = $before.ContainsKey($name) -and $after.ContainsKey($name) -and $before[$name] -eq $after[$name]
        }
    }
)
$refusalText = (($refusal | ForEach-Object { $_.ToString() }) -join "`n") + "`n"
$unchanged = $before.Count -eq $after.Count -and @($rows | Where-Object { -not $_.unchanged }).Count -eq 0
$passed = $probeExit -eq 1 -and $refusalText.StartsWith('Output already exists; retained evidence is never reused or overwritten') -and $unchanged
$report = [ordered]@{
    schema = 1
    scope = 'Existing-output refusal only, not a native render or performance check.'
    passed = $passed
    probe = $probePath
    probe_sha256 = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash
    checked_output = $outputPath
    expected_exit = 1
    actual_exit = $probeExit
    before_count = $before.Count
    after_count = $after.Count
    unchanged = $unchanged
    files = $rows
}
New-Item -ItemType Directory -Path $evidencePath | Out-Null
[System.IO.File]::WriteAllText((Join-Path $evidencePath 'refusal.log'), $refusalText,
    [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $evidencePath 'reuse-report.json'),
    ($report | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
if (-not $passed) { throw "Existing-output check failed; evidence retained at $evidencePath" }
Write-Output "Verified expected exit1 and $($before.Count) unchanged files: $evidencePath"
