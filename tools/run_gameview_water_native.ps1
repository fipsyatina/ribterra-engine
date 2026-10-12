param(
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$KitRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){
  throw 'PowerShell 7 or newer is required; invoke this runner with pwsh, not Windows PowerShell.'
}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
$repositoryBuildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))

function Resolve-ExistingBuildChild([string]$path,[string]$label) {
  $resolved=(Resolve-Path -LiteralPath $path).Path
  if(!(Test-Path -LiteralPath $resolved -PathType Container)){throw "$label must be a directory: $resolved"}
  if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $resolved),$repositoryBuildRoot)){
    throw "$label must be one immediate child of the authoritative repository build directory"
  }
  if(((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw "$label must not be a reparse point: $resolved"
  }
  return $resolved
}

function Resolve-ExistingBuildDescendant([string]$path,[string]$label) {
  $resolved=(Resolve-Path -LiteralPath $path).Path
  if(!(Test-Path -LiteralPath $resolved -PathType Container)){throw "$label must be a directory: $resolved"}
  $prefix=$repositoryBuildRoot+[IO.Path]::DirectorySeparatorChar
  if(!$resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){
    throw "$label must be a descendant of the authoritative repository build directory"
  }
  $cursor=$resolved
  while(-not [StringComparer]::OrdinalIgnoreCase.Equals($cursor,$repositoryBuildRoot)){
    if(((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
      throw "$label path must not traverse a reparse point: $cursor"
    }
    $cursor=Split-Path $cursor
  }
  return $resolved
}

function Get-HashRow([string]$path) {
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing bound input: $path"}
  $item=Get-Item -LiteralPath $path
  return [ordered]@{path=$item.FullName;bytes=[int64]$item.Length;
    sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
}

function Test-SamePath([string]$first,[string]$second) {
  return [StringComparer]::OrdinalIgnoreCase.Equals(
    [IO.Path]::GetFullPath($first),[IO.Path]::GetFullPath($second))
}

function Test-PathOverlap([string]$first,[string]$second) {
  $separators=[char[]]@([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)
  $a=[IO.Path]::GetFullPath($first).TrimEnd($separators)
  $b=[IO.Path]::GetFullPath($second).TrimEnd($separators)
  if([StringComparer]::OrdinalIgnoreCase.Equals($a,$b)){return $true}
  return $a.StartsWith(($b+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
    $b.StartsWith(($a+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)
}

function Assert-Headroom([string]$phase) {
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below 384MiB during ${phase}: $free"}
}

function Get-OutputRows([string]$root) {
  $rows=@()
  foreach($item in @(Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName)){
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
      throw "Native output contains a reparse-point file: $($item.FullName)"
    }
    $hash=Get-HashRow $item.FullName
    $rows += [ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/');
      path=$hash.path;bytes=$hash.bytes;sha256=$hash.sha256}
  }
  return $rows
}

function Compare-Binding([object]$before) {
  $after=Get-HashRow ([string]$before.path)
  return [ordered]@{path=$before.path;bytes=$before.bytes;sha256=$before.sha256;
    after_bytes=$after.bytes;after_sha256=$after.sha256;
    unchanged=([int64]$before.bytes -eq [int64]$after.bytes -and
      [string]$before.sha256 -eq [string]$after.sha256)}
}

$buildPath=Resolve-ExistingBuildChild $BuildRoot 'BuildRoot'
$kitPath=Resolve-ExistingBuildDescendant $KitRoot 'KitRoot'
$outputPath=[IO.Path]::GetFullPath($OutputRoot)
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $outputPath),$repositoryBuildRoot)){
  throw 'OutputRoot must be one new immediate child of the authoritative repository build directory'
}
if(Test-Path -LiteralPath $outputPath){throw "Refusing to reuse existing OutputRoot: $outputPath"}
if((Test-PathOverlap $buildPath $kitPath) -or (Test-PathOverlap $buildPath $outputPath) -or
   (Test-PathOverlap $kitPath $outputPath)){
  throw 'BuildRoot, KitRoot, and OutputRoot must be distinct non-overlapping paths under the repository build root'
}
Assert-Headroom 'preflight'

$executablePath=Join-Path $buildPath 'GameViewWaterProbe.exe'
$buildReportPath=Join-Path $buildPath 'build-report.json'
$shaderManifestPath=Join-Path $buildPath 'assets/shaders/manifest.json'
$kitManifestPath=Join-Path $kitPath 'manifest.json'
$runnerPath=(Resolve-Path -LiteralPath $PSCommandPath).Path
$assetsPath=Join-Path $buildPath 'assets'
if(!(Test-Path -LiteralPath $assetsPath -PathType Container)){throw "Missing runtime assets: $assetsPath"}

$executableBinding=Get-HashRow $executablePath
$buildReportBinding=Get-HashRow $buildReportPath
$shaderManifestBinding=Get-HashRow $shaderManifestPath
$kitManifestBinding=Get-HashRow $kitManifestPath
$runnerBinding=Get-HashRow $runnerPath

$buildReport=Get-Content -Raw -LiteralPath $buildReportPath | ConvertFrom-Json
if(-not $buildReport.executable){throw 'Build report has no executable binding'}
$executableFields=@($buildReport.executable.psobject.Properties.Name)
if(@(Compare-Object @('path','bytes','sha256') $executableFields).Count){
  throw 'Build report executable binding schema must contain exactly path, bytes, and sha256'
}
$reportedExecutablePath=[IO.Path]::GetFullPath([string]$buildReport.executable.path)
$reportedExecutableHash=[string]$buildReport.executable.sha256
if(!(Test-SamePath $reportedExecutablePath $executablePath) -or
   [int64]$buildReport.executable.bytes -ne [int64]$executableBinding.bytes -or
   $reportedExecutableHash -notmatch '^[0-9A-Fa-f]{64}$' -or
   $reportedExecutableHash.ToUpperInvariant() -ne [string]$executableBinding.sha256){
  throw 'Build report executable binding does not match the live GameViewWaterProbe.exe'
}
if(!(Test-SamePath ([string]$buildReport.output_root) $buildPath)){
  throw 'Build report output_root does not match BuildRoot'
}
if(-not $buildReport.shader_compilation -or -not $buildReport.shader_compilation.fresh_shader_compilation -or
   -not $buildReport.shader_compilation.manifest){
  throw 'Build report does not declare a fresh shader manifest binding'
}
$reportedManifest=$buildReport.shader_compilation.manifest
if(!(Test-SamePath ([string]$reportedManifest.path) $shaderManifestPath) -or
   [int64]$reportedManifest.bytes -ne [int64]$shaderManifestBinding.bytes -or
   ([string]$reportedManifest.sha256).ToUpperInvariant() -ne [string]$shaderManifestBinding.sha256){
  throw 'Build report shader manifest binding does not match the live fresh manifest'
}
if(-not $buildReport.control_kit -or !(Test-SamePath ([string]$buildReport.control_kit.root) $kitPath)){
  throw 'Build report control-kit root does not match KitRoot'
}
$reportedKitManifest=@($buildReport.control_kit.files | Where-Object {
  Test-SamePath ([string]$_.path) $kitManifestPath
})
if($reportedKitManifest.Count -ne 1 -or
   [int64]$reportedKitManifest[0].bytes -ne [int64]$kitManifestBinding.bytes -or
   ([string]$reportedKitManifest[0].sha256).ToUpperInvariant() -ne [string]$kitManifestBinding.sha256){
  throw 'Build report control-kit manifest binding does not match the live KitRoot manifest'
}

$boundInputs=[ordered]@{
  executable=$executableBinding
  build_report=$buildReportBinding
  fresh_shader_manifest=$shaderManifestBinding
  kit_manifest=$kitManifestBinding
  runner=$runnerBinding
}
$arguments=@($assetsPath,$kitPath,$outputPath)
$actualArgv=@($executablePath)+$arguments
$startedUtc=[DateTimeOffset]::UtcNow
$combined=[Collections.Generic.List[string]]::new()
$nativeExit=$null
$savedErrorActionPreference=$ErrorActionPreference
try {
  $ErrorActionPreference='Continue'
  $LASTEXITCODE=$null
  & $executablePath @arguments 2>&1 | ForEach-Object {
    $line=$_.ToString()
    $combined.Add($line)
    Write-Host $line
  }
  if($null -ne $LASTEXITCODE){$nativeExit=[int]$LASTEXITCODE}
} finally {
  $ErrorActionPreference=$savedErrorActionPreference
}
$endedUtc=[DateTimeOffset]::UtcNow
$executionOutput=if($combined.Count){[string]::Join([Environment]::NewLine,$combined)+[Environment]::NewLine}else{''}

if(!(Test-Path -LiteralPath $outputPath -PathType Container)){
  [Console]::Error.WriteLine("Native process did not create OutputRoot; exit=$nativeExit; no success was recorded: $outputPath")
  if($combined.Count){[Console]::Error.WriteLine($executionOutput)}
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}
if(((Get-Item -LiteralPath $outputPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
  [Console]::Error.WriteLine("Native process created a reparse-point OutputRoot: $outputPath")
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}

$executionOutputPath=Join-Path $outputPath 'execution-output.txt'
$executionLedgerPath=Join-Path $outputPath 'native-execution.json'
if((Test-Path -LiteralPath $executionOutputPath) -or (Test-Path -LiteralPath $executionLedgerPath)){
  [Console]::Error.WriteLine('Native output already contains runner-owned evidence names; refusing overwrite')
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}

$nativeRows=@(Get-OutputRows $outputPath)
$nativeBytes=[int64]0
foreach($row in $nativeRows){$nativeBytes += [int64]$row.bytes}
$postBindings=[ordered]@{}
$integrityErrors=[Collections.Generic.List[string]]::new()
foreach($property in $boundInputs.GetEnumerator()){
  try {
    $comparison=Compare-Binding $property.Value
    $postBindings[$property.Key]=$comparison
    if(!$comparison.unchanged){$integrityErrors.Add("Bound input changed: $($property.Value.path)")}
  } catch {
    $postBindings[$property.Key]=[ordered]@{path=$property.Value.path;unchanged=$false;error=$_.Exception.Message}
    $integrityErrors.Add("Could not revalidate bound input: $($property.Value.path)")
  }
}
if($null -eq $nativeExit){$integrityErrors.Add('Native LASTEXITCODE was unavailable')}
if($nativeExit -eq 0 -and $nativeRows.Count -eq 0){$integrityErrors.Add('Native process returned success without output files')}
try { Assert-Headroom 'after native execution' } catch { $integrityErrors.Add($_.Exception.Message) }
if($nativeBytes -gt 134217728){$integrityErrors.Add("Native output exceeds 128MiB before runner evidence: $nativeBytes")}

$utf8NoBom=[Text.UTF8Encoding]::new($false)
$executionOutputBytes=$utf8NoBom.GetBytes($executionOutput)
if($nativeBytes+$executionOutputBytes.LongLength -gt 134217728){
  [Console]::Error.WriteLine('Combined execution output would exceed the 128MiB OutputRoot budget')
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}
[IO.File]::WriteAllBytes($executionOutputPath,$executionOutputBytes)
$executionOutputBinding=Get-HashRow $executionOutputPath

$accepted=($nativeExit -eq 0 -and $integrityErrors.Count -eq 0)
$ledger=[ordered]@{
  schema=1
  classification='wrapper-recorded actual subprocess execution; not signed evidence and not independent GPU timing'
  accepted=$accepted
  actual_subprocess=$true
  signed_evidence=$false
  independent_gpu_timing=$false
  repository=$repoRoot
  build_root=$buildPath
  kit_root=$kitPath
  output_root=$outputPath
  executable=$executablePath
  arguments=$arguments
  actual_argv=$actualArgv
  started_utc=$startedUtc.ToString('o')
  ended_utc=$endedUtc.ToString('o')
  elapsed_milliseconds=[int64]($endedUtc-$startedUtc).TotalMilliseconds
  exit_code=$nativeExit
  combined_output_file='execution-output.txt'
  combined_output_binding=$executionOutputBinding
  build_report_executable_binding_schema_valid=$true
  source_cohort_note='The isolated build source cohort excludes this new runner; the runner binds its own live bytes and SHA256 separately before execution.'
  bound_inputs_before=$boundInputs
  bound_inputs_after=$postBindings
  bound_inputs_unchanged=($integrityErrors.Count -eq 0)
  native_output_before_runner_evidence=[ordered]@{
    file_count=$nativeRows.Count
    total_bytes=$nativeBytes
    files=$nativeRows
  }
  integrity_errors=@($integrityErrors)
}
$ledgerText=($ledger | ConvertTo-Json -Depth 10)+[Environment]::NewLine
$projectedBytes=$nativeBytes+[int64]$executionOutputBinding.bytes+$utf8NoBom.GetByteCount($ledgerText)
if($projectedBytes -gt 134217728){
  [Console]::Error.WriteLine("Runner evidence would exceed 128MiB OutputRoot budget: $projectedBytes")
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}

[IO.File]::WriteAllText($executionLedgerPath,$ledgerText,$utf8NoBom)

$finalRows=@(Get-OutputRows $outputPath)
$finalRowBytes=[int64]0
foreach($row in $finalRows){$finalRowBytes += [int64]$row.bytes}
$finalItems=@(Get-ChildItem -LiteralPath $outputPath -File -Recurse)
$finalItemBytes=[int64]0
foreach($item in $finalItems){$finalItemBytes += [int64]$item.Length}
$finalFree=[int64](Get-Volume -DriveLetter C).SizeRemaining
if($finalRows.Count -ne $nativeRows.Count+2 -or $finalItems.Count -ne $finalRows.Count -or
   $finalRowBytes -ne $finalItemBytes -or $finalRowBytes -ne $projectedBytes -or
   $finalRowBytes -gt 134217728 -or $finalFree -lt 402653184){
  [Console]::Error.WriteLine(
    "Final OutputRoot validation failed: rows=$($finalRows.Count) files=$($finalItems.Count) row_bytes=$finalRowBytes item_bytes=$finalItemBytes projected=$projectedBytes free=$finalFree")
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}
if($integrityErrors.Count){
  [Console]::Error.WriteLine('Native execution was retained but wrapper metadata/integrity validation failed')
  if($null -ne $nativeExit -and $nativeExit -ne 0){exit $nativeExit}
  exit 70
}
if($nativeExit -ne 0){exit $nativeExit}
exit 0
