param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [switch]$Apply,
  [string]$ExpectedPlanSha256)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot, 'C:\Users\fipst\Desktop\ribterra')) {
  throw 'This bounded cleanup is only for the authoritative Ribterra checkout.'
}
$planDirectory = (Resolve-Path -LiteralPath (Join-Path $repoRoot '.omx/plans')).Path
$planFull = [IO.Path]::GetFullPath($PlanPath)
if (-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path -Parent $planFull), $planDirectory)) {
  throw 'PlanPath must be a new explicit file directly in .omx/plans.'
}

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Write-NewUtf8Text([string]$Path, [string]$Text) {
  $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
  $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
  try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
}
function Assert-NoReparseTree([string]$Root) {
  $rootItem = Get-Item -LiteralPath $Root
  if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse root: $Root" }
  $bad = @(Get-ChildItem -LiteralPath $Root -Recurse -Force | Where-Object {
    ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
  })
  if ($bad.Count) { throw "Reparse child: $($bad[0].FullName)" }
}
function Assert-CanonicalFile([string]$Path, [string[]]$AllowedRoots) {
  $full = [IO.Path]::GetFullPath($Path)
  if (-not [StringComparer]::OrdinalIgnoreCase.Equals($full, $Path)) { throw "Noncanonical path: $Path" }
  $inside = @($AllowedRoots | Where-Object { $full.StartsWith($_ + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) })
  if ($inside.Count -ne 1) { throw "Path is outside its fixed root: $Path" }
  $cursor = $full
  while ($cursor -and -not [StringComparer]::OrdinalIgnoreCase.Equals($cursor, $repoRoot)) {
    $item = Get-Item -LiteralPath $cursor
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse ancestry: $cursor" }
    $cursor = Split-Path -Parent $cursor
  }
  $full
}
function Get-Row([string]$Path, [string]$Scope, [string[]]$AllowedRoots) {
  $full = Assert-CanonicalFile $Path $AllowedRoots
  $item = Get-Item -LiteralPath $full
  if ($item.PSIsContainer) { throw "Expected file: $full" }
  [ordered]@{path=$full;scope=$Scope;bytes=[int64]$item.Length;
    last_write_utc_ticks=[int64]$item.LastWriteTimeUtc.Ticks;sha256=(Get-Sha256 $full)}
}
function Sum-Rows([object[]]$Rows) {
  $total = [int64]0
  foreach ($row in $Rows) {
    $bytes = [int64]$row.bytes
    if ($bytes -lt 0 -or $total -gt [int64]::MaxValue - $bytes) { throw 'Invalid logical byte count.' }
    $total += $bytes
  }
  $total
}
function Assert-RowsUnchanged([object[]]$Expected, [object[]]$Actual, [string]$Label) {
  if ($Expected.Count -ne $Actual.Count) { throw "$Label file count changed." }
  $e = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  $a = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($row in $Expected) { if ($e.ContainsKey([string]$row.path)) { throw "$Label duplicate." }; $e.Add([string]$row.path,$row) }
  foreach ($row in $Actual) { if ($a.ContainsKey([string]$row.path)) { throw "$Label duplicate." }; $a.Add([string]$row.path,$row) }
  foreach ($path in $e.Keys) {
    if (-not $a.ContainsKey($path)) { throw "$Label missing: $path" }
    $x=$e[$path];$y=$a[$path]
    if ($x.scope -ne $y.scope -or [int64]$x.bytes -ne [int64]$y.bytes -or
        [int64]$x.last_write_utc_ticks -ne [int64]$y.last_write_utc_ticks -or $x.sha256 -ne $y.sha256) { throw "$Label changed: $path" }
  }
}
function Get-JsonText([string]$Path) { Get-Content -LiteralPath $Path -Raw }
function Resolve-LauncherAnchors {
  $main = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'Play Ribterra.cmd')).Path
  $rework = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'Play Ribterra Rework.cmd')).Path
  $pointer = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'dist/current-build.txt')).Path
  $rootExe = Join-Path $repoRoot 'Ribterra.exe'
  if (Test-Path -LiteralPath $rootExe -PathType Leaf) { $mainTarget = (Resolve-Path -LiteralPath $rootExe).Path }
  else {
    $mainRelative = (Get-Content -LiteralPath $pointer -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($mainRelative) -or [IO.Path]::IsPathRooted($mainRelative)) { throw 'Invalid current-build pointer.' }
    $mainTarget = (Resolve-Path -LiteralPath (Join-Path $repoRoot $mainRelative)).Path
  }
  $match = [regex]::Match((Get-Content -LiteralPath $rework -Raw), '(?im)^set "RIBTERRA_REWORK=([^"\r\n]+\.exe)"\s*$')
  if (-not $match.Success -or [IO.Path]::IsPathRooted($match.Groups[1].Value)) { throw 'Unable to resolve rework launcher target.' }
  $reworkTarget = (Resolve-Path -LiteralPath (Join-Path $repoRoot $match.Groups[1].Value)).Path
  @($main,$rework,$pointer,$mainTarget,$reworkTarget)
}

$candidateRoots = @(6..9 | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $repoRoot ("build/gameview-engine-2026-10-08-attempt{0:00}" -f $_))).Path })
$rollbackRoot = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/gameview-engine-2026-10-08-attempt05')).Path
$latestRoot = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/gameview-engine-2026-10-08-attempt10')).Path
$nativeRoots = @(6,7 | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $repoRoot ("build/gameview-engine-native-2026-10-08-attempt{0:00}" -f $_))).Path })
$nativeLatest = @(1..7 | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $repoRoot ("build/gameview-engine-native-2026-10-08-attempt{0:00}" -f $_))).Path })
$skyProtectedRoots = @(
  (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/sky-authored-2026-10-08-attempt05')).Path,
  (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/sky-authored-2026-10-08-attempt10')).Path
) + @(1..6 | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $repoRoot ("build/sky-authored-native-2026-10-08-attempt{0:00}" -f $_))).Path })
$relativeRootFiles = @('GameViewEngineProbe.exe','GameViewEngineProbe.exp','GameViewEngineProbe.lib','SDL3.dll','SDL3_ttf.dll')
$productionObjectNames = @('asset_catalog.obj','atomic_file.obj','client_session.obj','contact_shadow.obj','economy.obj','foundation.obj','game_art.obj','game_server.obj','gns_transport.obj','lobby.obj','localization.obj','network_session.obj','platform.obj','protocol.obj','renderer2d.obj','skills.obj','sky_shadow.obj','texture_atlas.obj','view.obj','world_save.obj','world.obj')

foreach ($root in @($candidateRoots + $rollbackRoot + $latestRoot + $nativeLatest + $skyProtectedRoots)) { Assert-NoReparseTree $root }
$latestBuildReportPath = Join-Path $latestRoot 'build-report.json'
if (-not (Test-Path -LiteralPath $latestBuildReportPath -PathType Leaf)) { throw 'Fresh attempt10 build-report.json is required before planning cleanup.' }
$latestBuild = Get-Content -LiteralPath $latestBuildReportPath -Raw | ConvertFrom-Json
if ($latestBuild.translation_unit_count -ne 22 -or $latestBuild.source_final_binding.Count -ne 96 -or
    $latestBuild.command_evidence.Count -ne 54 -or $latestBuild.protected_binding.Count -ne 8 -or
    $latestBuild.control_kit.files.Count -ne 52) { throw 'Fresh attempt10 build report schema/count gate failed.' }
if (@($latestBuild.source_final_binding | Where-Object { $_.unchanged -ne $true }).Count) { throw 'Attempt10 source final binding is not unchanged.' }
if (@($latestBuild.source_final_binding | Where-Object { [string]::IsNullOrWhiteSpace($_.sha256) -or -not (Test-Path -LiteralPath $_.repository_path -PathType Leaf) -or -not (Test-Path -LiteralPath $_.snapshot_path -PathType Leaf) -or (Get-Sha256 $_.repository_path) -ne $_.sha256 -or (Get-Sha256 $_.snapshot_path) -ne $_.sha256 }).Count) { throw 'Attempt10 source final binding was not fully rehashed.' }
if (@($latestBuild.protected_binding | Where-Object { $_.unchanged -ne $true -or [string]::IsNullOrWhiteSpace($_.sha256) -or (Get-Sha256 $_.path) -ne $_.sha256 }).Count) { throw 'Attempt10 protected binding is not unchanged and rehashed.' }
if (-not $latestBuild.executable.path -or $latestBuild.executable.path -ne (Join-Path $latestRoot 'GameViewEngineProbe.exe') -or $latestBuild.executable.bytes -ne (Get-Item -LiteralPath (Join-Path $latestRoot 'GameViewEngineProbe.exe')).Length -or (Get-Sha256 (Join-Path $latestRoot 'GameViewEngineProbe.exe')) -ne $latestBuild.executable.sha256) { throw 'Attempt10 executable binding is invalid.' }
if (@($latestBuild.control_kit.files | Where-Object { [string]::IsNullOrWhiteSpace($_.sha256) -or -not (Test-Path -LiteralPath $_.path -PathType Leaf) -or (Get-Sha256 $_.path) -ne $_.sha256 }).Count) { throw 'Attempt10 control kit files were not rehashed.' }
if (@($latestBuild.command_evidence | Where-Object { $_.path -notlike '*.exit.txt' }).Count -ne 27) { throw 'Attempt10 command evidence lacks 27 commands.' }
$exitEvidence = @($latestBuild.command_evidence | Where-Object { $_.path -like '*.exit.txt' })
if ($exitEvidence.Count -ne 27 -or @($exitEvidence | Where-Object { $_.bytes -ne 2 -or (Get-Content -LiteralPath $_.path -Raw).Trim() -ne '0' }).Count) { throw 'Attempt10 command exit-sidecars are not all zero.' }
$probeSource = @($latestBuild.source_final_binding | Where-Object { $_.relative_path -eq 'tools/gameview_engine_probe.cpp' })
if ($probeSource.Count -ne 1) { throw 'Attempt10 lacks unique probe source binding.' }
$latestExe = Join-Path $latestRoot 'GameViewEngineProbe.exe'
if (-not (Test-Path -LiteralPath $latestExe -PathType Leaf)) { throw 'Fresh attempt10 executable is required.' }
$latestExeHash = Get-Sha256 $latestExe
$sourceHash = [string]$probeSource[0].sha256
if ([string]::IsNullOrWhiteSpace($sourceHash)) { throw 'Attempt10 build report lacks tools/gameview_engine_probe.cpp provenance.' }
$controlKitRoot = (Resolve-Path -LiteralPath $latestBuild.control_kit.root).Path
$controlKitEvidenceRoot = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/engine-control-kit-2026-10-08-check05')).Path
if (-not [StringComparer]::OrdinalIgnoreCase.Equals($controlKitRoot, (Join-Path $controlKitEvidenceRoot 'export-a'))) { throw 'Unexpected control kit evidence root.' }
Assert-NoReparseTree $controlKitEvidenceRoot
$controlKitManifest = [string](@($latestBuild.control_kit.files | Where-Object { $_.relative_path -eq 'manifest.json' } | Select-Object -First 1).path)
if (-not (Test-Path -LiteralPath $controlKitManifest -PathType Leaf)) { throw 'Attempt10 control kit manifest is missing.' }
$controlKitManifestHash = Get-Sha256 $controlKitManifest
$expectedCaptureNames = @('gameview-sun','gameview-sun-repeat','gameview-sun-hidden','gameview-no-sky-casters','gameview-moon',
  'gameview-window-radiance-on','gameview-window-radiance-off','gameview-disabled-mask-mismatch','gameview-window-self-only',
  'gameview-fixture-red-open','gameview-fixture-red-blocked','gameview-fixture-blue-open','gameview-fixture-blue-blocked')

foreach ($native in $nativeRoots) {
  $reportPath = Join-Path $native 'gameview-engine-report.json'
  if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) { throw "Missing native success report: $reportPath" }
  $raw = Get-JsonText $reportPath; $report = $raw | ConvertFrom-Json
  if ($report.schema -ne 2 -or $report.complete -ne $true) { throw "Native report is not schema2 complete: $reportPath" }
  if ($report.source_sha256 -ne $sourceHash -or $report.executable_sha256 -ne $latestExeHash -or $report.kit_manifest_sha256 -ne $controlKitManifestHash) { throw "Native report binding mismatch: $reportPath" }
  $captureFiles = @($report.captures | ForEach-Object { [string]$_.file })
  $captureNames = @($report.captures | ForEach-Object { [string]$_.name })
  $nativePngs = @(Get-ChildItem -LiteralPath $native -File -Filter '*.png' -Force | ForEach-Object { $_.Name })
  if ($report.captures.Count -ne 13 -or @($report.captures | Where-Object {
      [string]::IsNullOrWhiteSpace($_.file) -or [string]::IsNullOrWhiteSpace($_.name) -or [string]::IsNullOrWhiteSpace($_.sha256) -or
      $_.name -cnotin $expectedCaptureNames -or [string]$_.file -cne ([string]$_.name + '.png') -or
      $_.capture -ne $true -or $_.submitted -ne $true -or [IO.Path]::IsPathRooted([string]$_.file) -or
      [IO.Path]::GetFileName([string]$_.file) -ne [string]$_.file -or [IO.Path]::GetExtension([string]$_.file) -ne '.png' -or
      -not (Test-Path -LiteralPath (Join-Path $native $_.file) -PathType Leaf) -or
      (Get-Sha256 (Join-Path $native $_.file)) -ne $_.sha256
    }).Count -or @($captureFiles | Sort-Object -Unique).Count -ne 13 -or @($captureNames | Sort-Object -Unique).Count -ne 13 -or
    $nativePngs.Count -ne 13 -or @($nativePngs | Where-Object { $_ -cnotin $captureFiles }).Count -ne 0) { throw "Native capture manifest is not 13 complete unique named PNGs: $reportPath" }
  foreach ($capture in $report.captures) { [void](Assert-CanonicalFile (Join-Path $native $capture.file) @($native)) }
  if ($report.radiance.camera_matrix.Count -ne 6) { throw "Native camera_matrix is not six elements: $reportPath" }
  if (@($report.radiance.camera_matrix | Where-Object { $_.authored_lights -ne 2 -or $_.off_lights -ne 0 -or $_.picks -ne 2 }).Count) { throw "Native camera matrix values are not authored=2/off=0/picks=2: $reportPath" }
  $expectedGameViewIds = @('cell-radius','coordinate-flt-max','coordinate-nan','preexisting-light-count-over-four','external-light-nonzero-blocker-range','second-authored-light-identity-collision','authored-light-blocker-range-over-32')
  if ($report.gameview_rejections.Count -ne 7 -or @($report.gameview_rejections | Where-Object { $_.id -notin $expectedGameViewIds -or $_.frame_exact -ne $true -or $_.camera_pick_exact -ne $true -or [string]::IsNullOrWhiteSpace($_.diagnostic) }).Count -ne 0 -or @($report.gameview_rejections.id | Sort-Object -Unique).Count -ne 7) { throw "Native GameView rejection matrix is not seven exact rows: $reportPath" }
  $expectedDirectIds = @('stable-id-zero','stable-id-overflow','collision-at-second-emitter','capacity-over-four','scaled-clearance-below-0.002','disabled-invalid-id-anchor-filtered')
  if ($report.direct_emitter_rejections.Count -ne 6 -or @($report.direct_emitter_rejections | Where-Object { $_.id -notin $expectedDirectIds -or $_.lighting_exact -ne $true -or ($_.id -ne 'disabled-invalid-id-anchor-filtered' -and [string]::IsNullOrWhiteSpace($_.diagnostic)) }).Count -ne 0 -or @($report.direct_emitter_rejections.id | Sort-Object -Unique).Count -ne 6) { throw "Native direct-emitter rejection matrix is not six exact rows: $reportPath" }
}
$nativeReportRows = @($nativeRoots | ForEach-Object { Get-Row (Join-Path $_ 'gameview-engine-report.json') 'native-success-report' $nativeRoots })

$candidatePaths = [Collections.Generic.List[string]]::new()
foreach ($root in $candidateRoots) {
  foreach ($relative in $relativeRootFiles) {
    $rootFile = Join-Path $root $relative
    if (Test-Path -LiteralPath $rootFile -PathType Leaf) { $candidatePaths.Add([IO.Path]::GetFullPath($rootFile)) }
  }
  foreach ($name in $productionObjectNames) {
    $obj = Join-Path (Join-Path $root 'obj') $name
    if (-not (Test-Path -LiteralPath $obj -PathType Leaf)) { throw "Missing fixed production object: $obj" }
    $candidatePaths.Add([IO.Path]::GetFullPath($obj))
  }
  $unexpectedObjects = @(Get-ChildItem -LiteralPath (Join-Path $root 'obj') -File -Force | Where-Object { $_.Name -notin $productionObjectNames })
  if ($unexpectedObjects.Count -gt 1 -or ($root -eq $candidateRoots[0] -and $unexpectedObjects.Count -ne 0) -or ($root -ne $candidateRoots[0] -and $unexpectedObjects.Count -ne 1) -or ($unexpectedObjects.Count -eq 1 -and $unexpectedObjects[0].Name -ne 'gameview_engine_probe.obj')) { throw "Unexpected object inventory in $root" }
  if ($unexpectedObjects.Count -eq 1) { $candidatePaths.Add([IO.Path]::GetFullPath($unexpectedObjects[0].FullName)) }
  $rootCompiled = @(Get-ChildItem -LiteralPath $root -File -Force | Where-Object { $_.Name -in $relativeRootFiles })
  $expectedRootCount = if ($root -eq $candidateRoots[0]) { 0 } else { 5 }
  if ($rootCompiled.Count -ne $expectedRootCount) { throw "Unexpected root compiled inventory in $root" }
}
$candidatePaths = @($candidatePaths | Sort-Object -Unique)
if ($candidatePaths.Count -ne 102) { throw "Unexpected eligible GameView compiled inventory: $($candidatePaths.Count), expected 21+27+27+27." }
foreach ($path in $candidatePaths) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Expected obsolete compiled file missing: $path" }; [void](Assert-CanonicalFile $path $candidateRoots) }

$protectedRows = [Collections.Generic.List[object]]::new()
foreach ($root in $candidateRoots) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { if ($item.FullName -notin $candidatePaths) { $protectedRows.Add((Get-Row $item.FullName 'candidate-root-noncandidate' @($root))) } } }
foreach ($root in @($rollbackRoot,$latestRoot) + $nativeLatest) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { $protectedRows.Add((Get-Row $item.FullName 'protected-proof-tree' @($root))) } }
foreach ($root in $skyProtectedRoots) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { $protectedRows.Add((Get-Row $item.FullName 'protected-sky-proof-tree' @($root))) } }
foreach ($item in (Get-ChildItem -LiteralPath $controlKitEvidenceRoot -Recurse -Force -File)) { $protectedRows.Add((Get-Row $item.FullName 'protected-control-kit' @($controlKitEvidenceRoot))) }
foreach ($path in (Resolve-LauncherAnchors)) { $protectedRows.Add((Get-Row $path 'launcher-or-resolved-target' @($repoRoot))) }
$protectedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($row in $protectedRows.ToArray()) { [void]$protectedPaths.Add([string]$row.path) }
foreach ($binding in $latestBuild.source_final_binding) {
  if ($protectedPaths.Add([string]$binding.repository_path)) { $protectedRows.Add((Get-Row $binding.repository_path 'live-source-final-binding' @($repoRoot))) }
}
foreach ($binding in $latestBuild.protected_binding) {
  if ($protectedPaths.Add([string]$binding.path)) { $protectedRows.Add((Get-Row $binding.path 'build-protected-binding' @($repoRoot))) }
}
if (@($protectedRows.path | Sort-Object -Unique).Count -ne $protectedRows.Count) { throw 'Protected inventory contains duplicate paths.' }
$candidateRows = @($candidatePaths | ForEach-Object { Get-Row $_ 'obsolete-gameview-compiled-copy' $candidateRoots })

if (-not $Apply) {
  if (Test-Path -LiteralPath $planFull) { throw 'Refusing to overwrite a cleanup plan.' }
  $plan = [ordered]@{schema=1;scope='Only immediate compiled copies in GameView attempts 06-09';repository=$repoRoot;
    cleanup_script=$PSCommandPath;cleanup_script_sha256=(Get-Sha256 $PSCommandPath);candidate_roots=$candidateRoots;
    evidence_build_root=$latestRoot;evidence_build_report_sha256=(Get-Sha256 $latestBuildReportPath);evidence_exe_sha256=$latestExeHash;
    probe_source_sha256=$sourceHash;control_kit_root=$controlKitRoot;control_kit_manifest_sha256=$controlKitManifestHash;native_success_roots=$nativeRoots;native_success_reports=$nativeReportRows;candidate_files=$candidateRows;candidate_count=$candidateRows.Count;
    logical_bytes=(Sum-Rows $candidateRows);protected_files=@($protectedRows.ToArray());protected_count=$protectedRows.Count;
    preservation='All noncandidate files in attempts 06-09, GameView05 rollback, GameView10 latest, native01-07, sky-authored05/10, authored-native01-06, source snapshots, logs, reports, assets, compiler-temp, generated, shaders, kits, launchers and resolved launcher targets remain.';
    notification='Apply requires user-visible review of every exact path and this plan SHA256.'}
  Write-NewUtf8Text $planFull ($plan | ConvertTo-Json -Depth 10)
  [ordered]@{plan=$planFull;sha256=(Get-Sha256 $planFull);files=$candidateRows.Count;logical_bytes=$plan.logical_bytes;protected_files=$protectedRows.Count} | ConvertTo-Json
  exit 0
}

if ([string]::IsNullOrWhiteSpace($ExpectedPlanSha256)) { throw 'Apply requires the exact SHA256 of the user-notified plan.' }
if (-not (Test-Path -LiteralPath $planFull -PathType Leaf)) { throw 'Cleanup plan does not exist.' }
if ((Get-Sha256 $planFull) -ne $ExpectedPlanSha256) { throw 'Cleanup plan hash differs from the user-notified plan.' }
$plan = Get-Content -LiteralPath $planFull -Raw | ConvertFrom-Json
$reportPath = $planFull + '.result.json'
if (Test-Path -LiteralPath $reportPath) { throw 'Refusing to overwrite a cleanup result.' }
if ($plan.schema -ne 1 -or $plan.repository -ne $repoRoot -or $plan.cleanup_script -ne $PSCommandPath -or $plan.cleanup_script_sha256 -ne (Get-Sha256 $PSCommandPath)) { throw 'Cleanup plan identity or helper hash mismatch.' }
if ($plan.evidence_build_report_sha256 -ne (Get-Sha256 $latestBuildReportPath) -or $plan.evidence_exe_sha256 -ne $latestExeHash -or $plan.probe_source_sha256 -ne $sourceHash -or $plan.control_kit_manifest_sha256 -ne $controlKitManifestHash) { throw 'Cleanup evidence build/source/kit binding changed.' }
$planned = @($plan.candidate_files | Sort-Object path); $actual = @($candidatePaths | ForEach-Object { Get-Row $_ 'obsolete-gameview-compiled-copy' $candidateRoots })
Assert-RowsUnchanged $planned $actual 'Candidate'
Assert-RowsUnchanged @($plan.protected_files | Sort-Object path) @($protectedRows.ToArray() | Sort-Object path) 'Protected'
Assert-RowsUnchanged @($plan.native_success_reports | Sort-Object path) $nativeReportRows 'Native success report'
$freeBefore = [int64](Get-Volume -DriveLetter C).SizeRemaining
$removed = [Collections.Generic.List[object]]::new(); $failure = $null; $protectedAfter = @(); $protectedError = $null
try {
  foreach ($row in $planned) {
    $current = Get-Row ([string]$row.path) 'obsolete-gameview-compiled-copy' $candidateRoots
    Assert-RowsUnchanged @($row) @($current) 'Candidate during cleanup'
    Remove-Item -LiteralPath $current.path -Force
    if (Test-Path -LiteralPath $current.path) { throw "Removal did not complete: $($current.path)" }
    $removed.Add($row)
  }
} catch { $failure = $_.Exception.Message }
finally {
  try {
    $protectedAfterRows = [Collections.Generic.List[object]]::new()
    foreach ($root in $candidateRoots) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { if ($item.FullName -notin $planned.path) { $protectedAfterRows.Add((Get-Row $item.FullName 'candidate-root-noncandidate' @($root))) } } }
    foreach ($root in @($rollbackRoot,$latestRoot) + $nativeLatest) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { $protectedAfterRows.Add((Get-Row $item.FullName 'protected-proof-tree' @($root))) } }
    foreach ($root in $skyProtectedRoots) { foreach ($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File)) { $protectedAfterRows.Add((Get-Row $item.FullName 'protected-sky-proof-tree' @($root))) } }
    foreach ($item in (Get-ChildItem -LiteralPath $controlKitEvidenceRoot -Recurse -Force -File)) { $protectedAfterRows.Add((Get-Row $item.FullName 'protected-control-kit' @($controlKitEvidenceRoot))) }
    foreach ($path in (Resolve-LauncherAnchors)) { $protectedAfterRows.Add((Get-Row $path 'launcher-or-resolved-target' @($repoRoot))) }
    $afterPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $protectedAfterRows.ToArray()) { [void]$afterPaths.Add([string]$row.path) }
    foreach ($binding in $latestBuild.source_final_binding) { if ($afterPaths.Add([string]$binding.repository_path)) { $protectedAfterRows.Add((Get-Row $binding.repository_path 'live-source-final-binding' @($repoRoot))) } }
    foreach ($binding in $latestBuild.protected_binding) { if ($afterPaths.Add([string]$binding.path)) { $protectedAfterRows.Add((Get-Row $binding.path 'build-protected-binding' @($repoRoot))) } }
    $protectedAfter = @($protectedAfterRows.ToArray() | Sort-Object path)
    Assert-RowsUnchanged @($plan.protected_files | Sort-Object path) $protectedAfter 'Protected after cleanup'
  } catch { $protectedError = $_.Exception.Message; if ($null -eq $failure) { $failure = $protectedError } }
  $remaining = @($planned | Where-Object { Test-Path -LiteralPath $_.path }); $freeAfter = [int64](Get-Volume -DriveLetter C).SizeRemaining
  if ($remaining.Count -gt 0 -and $null -eq $failure) { $failure = "Candidate files reappeared after cleanup: $($remaining.Count)" }
  $result = [ordered]@{plan_sha256=$ExpectedPlanSha256;complete=($null -eq $failure -and $remaining.Count -eq 0 -and $protectedError -eq $null);exit_code=$(if($null -eq $failure){0}else{1});
    removed_file_count=$removed.Count;removed_logical_bytes=(Sum-Rows $removed.ToArray());removed_files=$removed.ToArray();remaining_file_count=$remaining.Count;
    remaining_files=@($remaining | ForEach-Object { [string]$_.path });free_before=$freeBefore;free_after=$freeAfter;observed_free_delta=$freeAfter-$freeBefore;
    protected_changed=($protectedError -ne $null);protected_binding_actual=$protectedAfter;protected_error=$protectedError;error=$failure}
  Write-NewUtf8Text $reportPath ($result | ConvertTo-Json -Depth 10)
  [ordered]@{report=$reportPath;complete=$result.complete;removed_files=$removed.Count;removed_logical_bytes=$result.removed_logical_bytes;remaining_files=@($result.remaining_files).Count;observed_free_delta=$result.observed_free_delta} | ConvertTo-Json
}
if ($failure) { throw $failure }
