param(
    [Parameter(Mandatory = $true)][string]$CsvPath,
    [Parameter(Mandatory = $true)][string]$ProbeReport,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$csvSource = (Resolve-Path -LiteralPath $CsvPath).Path
$probeSource = (Resolve-Path -LiteralPath $ProbeReport).Path
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Refusing to overwrite retained analysis.' }

function Metric-Values($Rows, [string]$Column) {
    foreach ($row in $Rows) {
        $number = 0.0
        if ([double]::TryParse($row.$Column, [System.Globalization.NumberStyles]::Float,
                [System.Globalization.CultureInfo]::InvariantCulture, [ref]$number) -and
            [double]::IsFinite($number) -and $number -ge 0) { $number }
    }
}

function Summarize-Metric($Rows, [string]$Column) {
    $values = @(Metric-Values $Rows $Column | Sort-Object)
    if ($values.Count -eq 0) {
        return [ordered]@{ valid_samples = 0; missing_samples = $Rows.Count; mean_ms = $null }
    }
    $mean = ($values | Measure-Object -Average).Average
    return [ordered]@{
        valid_samples = $values.Count
        missing_samples = $Rows.Count - $values.Count
        mean_ms = $mean
        p50_ms = $values[[int][math]::Ceiling($values.Count * 0.50) - 1]
        p95_ms = $values[[int][math]::Ceiling($values.Count * 0.95) - 1]
        p99_ms = $values[[int][math]::Ceiling($values.Count * 0.99) - 1]
        max_ms = $values[-1]
        over_120fps_budget = @($values | Where-Object { $_ -gt (1000.0 / 120) }).Count
    }
}

$probe = Get-Content -LiteralPath $probeSource -Raw | ConvertFrom-Json
if (-not $probe.probe_complete) { throw 'Probe did not complete; do not certify partial sampling.' }
$rows = @(Import-Csv -LiteralPath $csvSource | Where-Object { $_.Application -eq 'PresentationProbe.exe' })
if ($rows.Count -eq 0) { throw 'No target process frames in CSV.' }
foreach ($column in @('CPUStartQPC', 'MsGPUTime', 'MsGPUBusy', 'MsUntilDisplayed',
        'MsBetweenDisplayChange', 'SwapChainAddress', 'PresentMode', 'AllowsTearing')) {
    if ($column -notin $rows[0].PSObject.Properties.Name) { throw "Missing required column: $column" }
}
$processIds = @($rows.ProcessID | Sort-Object -Unique)
if ($processIds.Count -ne 1) { throw 'Multiple target processes; select a single run explicitly.' }

$cases = foreach ($case in $probe.cases) {
    $begin = [uint64]$case.sample_begin_qpc
    $end = [uint64]$case.sample_end_qpc
    $frequency = [uint64]$case.qpc_frequency
    if ($end -le $begin -or $frequency -eq 0) { throw 'Invalid sampled QPC interval.' }
    $selected = @($rows | Where-Object {
        [uint64]$_.CPUStartQPC -ge $begin -and [uint64]$_.CPUStartQPC -lt $end
    })
    if ($selected.Count -eq 0) { throw "No ETW coverage: $($case.name)" }
    $chains = @($selected.SwapChainAddress | Sort-Object -Unique)
    if ($chains.Count -ne 1) { throw "Mixed swapchains inside sampled interval: $($case.name)" }
    $displayIntervals = Summarize-Metric $selected 'MsBetweenDisplayChange'
    $untilDisplayed = @(Metric-Values $selected 'MsUntilDisplayed' | Where-Object { $_ -gt 0 })
    [ordered]@{
        name = $case.name
        CPUStartQPC_begin_inclusive = $begin
        CPUStartQPC_end_exclusive = $end
        qpc_frequency = $frequency
        selected_rows = $selected.Count
        probe_queued_presentations = $case.queued_presentations
        observed_displayed_present_rows = $untilDisplayed.Count
        unobserved_displayed_present_rows = $selected.Count - $untilDisplayed.Count
        swapchain = $chains[0]
        actual_modes = @($selected | Group-Object PresentMode | Select-Object Name, Count)
        allows_tearing_rows = @($selected | Where-Object { $_.AllowsTearing -eq '1' }).Count
        gpu_elapsed = (Summarize-Metric $selected 'MsGPUTime')
        gpu_busy = (Summarize-Metric $selected 'MsGPUBusy')
        display_change_interval = $displayIntervals
        display_change_fps_valid_intervals = $(if ($displayIntervals.mean_ms -gt 0) {
            1000.0 / $displayIntervals.mean_ms
        } else { $null })
        display_latency_from_present = (Summarize-Metric $selected 'MsUntilDisplayed')
    }
}

$summary = [ordered]@{
    schema = 1
    source_csv = $csvSource
    source_csv_sha256 = (Get-FileHash -LiteralPath $csvSource -Algorithm SHA256).Hash
    source_probe = $probeSource
    source_probe_sha256 = (Get-FileHash -LiteralPath $probeSource -Algorithm SHA256).Hash
    process_id = $processIds[0]
    method = 'CPUStartQPC in [probe sample begin, end); nearest-rank quantiles; NA omitted, never zero-filled.'
    display_method = '1000 / mean valid MsBetweenDisplayChange; coverage and tearing retained separately.'
    limitations = 'Display interval subset is not guaranteed whole-frame scanout. CPU-window filtering has boundary uncertainty; no full Dynamic or minimum-120 acceptance.'
    cases = @($cases)
}
$stream = [System.IO.File]::Open($destination, [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
try {
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
    try { $writer.WriteLine(($summary | ConvertTo-Json -Depth 8)); $writer.Flush() }
    finally { $writer.Dispose() }
} finally { $stream.Dispose() }
Write-Output "Retained PresentMon analysis: $destination"
