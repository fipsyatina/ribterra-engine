param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [switch]$Apply,
  [string]$ExpectedPlanSha256)

$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,'C:\Users\fipst\Desktop\ribterra')){
  throw 'This bounded cleanup is only for the authoritative Ribterra checkout.'
}
$artifactRoot=Join-Path $repoRoot 'artifacts'
$extensions=@('.exe','.dll','.lib','.pdb','.obj','.ilk','.exp')
# These historical test cohorts are explicit, not an age-based or wildcard rule.
$historicalCohorts=@(
  'presentation-2026-10-05','concept-rebuild-2026-10-05-map-ground',
  'concept-rebuild-2026-10-05','hdr-color-2026-10-06',
  'ground-forward-2026-10-07','material-bridge-2026-10-06',
  'local-occlusion-2026-10-07','material-master-2026-10-06',
  'surface-lighting-2026-10-05','outcrop-hud-2026-10-05',
  'relief-face-2026-10-05')
$allowedRoots=@()
foreach($cohort in $historicalCohorts){
  $cohortRoot=Join-Path $artifactRoot $cohort
  $cohortItem=Get-Item -LiteralPath $cohortRoot
  if(($cohortItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse cohort: $cohortRoot"}
  foreach($candidate in (Get-ChildItem -LiteralPath $cohortRoot -Directory)){
    if(($candidate.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse attempt: $($candidate.FullName)"}
    $runtime=Join-Path $candidate.FullName 'runtime'
    if(Test-Path -LiteralPath $runtime -PathType Container){
      $item=Get-Item -LiteralPath $runtime
      if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse runtime: $runtime"}
      $allowedRoots += $item.FullName
    }
  }
}

function Assert-Target([string]$path){
  $full=[IO.Path]::GetFullPath($path)
  if(-not [StringComparer]::OrdinalIgnoreCase.Equals($full,$path)){throw "Noncanonical target: $path"}
  $roots=@($allowedRoots | Where-Object {$full.StartsWith($_+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)})
  if($roots.Count -ne 1){throw "Target is outside the explicit historical runtime roots: $path"}
  if([IO.Path]::GetExtension($full).ToLowerInvariant() -notin $extensions){throw "Not a compiled-copy extension: $path"}
  $cursor=$full
  while($cursor -and -not [StringComparer]::OrdinalIgnoreCase.Equals($cursor,$repoRoot)){
    if(((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse target ancestry: $cursor"}
    $cursor=Split-Path -Parent $cursor
  }
  return $full
}

function Assert-NoActiveRuntime{
  $active=@(Get-CimInstance Win32_Process | Where-Object {
    $processPath=$_.ExecutablePath
    if(-not $processPath){return $false}
    return @($allowedRoots | Where-Object {$processPath.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0
  })
  if($active.Count){throw 'A historical runtime is running; no files removed.'}
}

function Sum-LogicalBytes([object[]]$rows){
  $total=[int64]0
  foreach($row in $rows){
    $bytes=[int64]$row.bytes
    if($bytes -lt 0 -or $total -gt [int64]::MaxValue-$bytes){throw 'Invalid or overflowing logical byte count.'}
    $total+=$bytes
  }
  return $total
}

function Get-ContentSha256([string]$path){
  # Large historical runtimes otherwise cause millions of tiny physical reads.
  # Buffering changes only read size; the digest remains SHA-256 of every byte.
  $source=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $buffer=$null
  try{
    $buffer=[IO.BufferedStream]::new($source,1048576)
    return (Get-FileHash -InputStream $buffer -Algorithm SHA256).Hash
  }finally{
    if($null -ne $buffer){$buffer.Dispose()}
    $source.Dispose()
  }
}

$planFull=[IO.Path]::GetFullPath($PlanPath)
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $planFull),(Join-Path $repoRoot '.omx/plans'))){
  throw 'PlanPath must be a new explicit file in .omx/plans.'
}
Assert-NoActiveRuntime

if(-not $Apply){
  if(Test-Path -LiteralPath $planFull){throw 'Refusing to overwrite a cleanup plan.'}
  $rows=@(foreach($root in ($allowedRoots | Sort-Object)){
    $items=@(Get-ChildItem -LiteralPath $root -Recurse -Force)
    if(@($items | Where-Object {($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0}).Count){throw "Reparse child: $root"}
    foreach($item in ($items | Where-Object {-not $_.PSIsContainer -and $_.Extension.ToLowerInvariant() -in $extensions} | Sort-Object FullName)){
      [void](Assert-Target $item.FullName)
      [ordered]@{path=$item.FullName;bytes=[int64]$item.Length;last_write_utc_ticks=$item.LastWriteTimeUtc.Ticks;
        sha256=(Get-ContentSha256 $item.FullName)}
    }
  })
  $plan=[ordered]@{schema=2;scope='Only compiled copies in explicitly named historical artifact runtime directories';
    repository=$repoRoot;roots=$allowedRoots;extensions=$extensions;files=$rows;
    file_count=$rows.Count;logical_bytes=(Sum-LogicalBytes $rows);
    preservation='No source, saves, assets, PNGs, reports, current build proof, dist, or original art is removed.';
    recovery='Deletion is permanent locally; executable copies can be rebuilt, not restored from the recycle bin.'}
  [IO.File]::WriteAllText($planFull,($plan | ConvertTo-Json -Depth 8))
  [ordered]@{plan=$planFull;sha256=(Get-FileHash -LiteralPath $planFull).Hash;
    files=$rows.Count;logical_bytes=$plan.logical_bytes;runtime_roots=$allowedRoots.Count} | ConvertTo-Json
  exit 0
}

if([string]::IsNullOrWhiteSpace($ExpectedPlanSha256)){throw 'Apply requires the exact SHA256 of the previously notified plan.'}
if((Get-FileHash -LiteralPath $planFull).Hash -ne $ExpectedPlanSha256){throw 'Cleanup plan hash differs from the notified plan.'}
$plan=Get-Content -Raw -LiteralPath $planFull | ConvertFrom-Json
if($plan.schema -ne 2 -or $plan.repository -ne $repoRoot){throw 'Cleanup plan identity mismatch.'}
$plannedRoots=@($plan.roots | Sort-Object)
$actualRoots=@($allowedRoots | Sort-Object)
if(@(Compare-Object -ReferenceObject $plannedRoots -DifferenceObject $actualRoots).Count){throw 'Runtime root inventory changed.'}
$files=@($plan.files)
if($files.Count -ne $plan.file_count -or @($files.path | Sort-Object -Unique).Count -ne $files.Count){throw 'Cleanup plan count/uniqueness mismatch.'}
if((Sum-LogicalBytes $files) -ne [int64]$plan.logical_bytes){throw 'Cleanup plan logical byte count mismatch.'}
$reportPath=$planFull+'.result.json'
if(Test-Path -LiteralPath $reportPath){throw 'Refusing to overwrite a cleanup result.'}
# Validate every exact target before the first removal; never recursively remove a directory.
foreach($row in $files){
  [void](Assert-Target ([string]$row.path))
  $item=Get-Item -LiteralPath $row.path
  if($item.PSIsContainer -or [int64]$item.Length -ne [int64]$row.bytes -or $item.LastWriteTimeUtc.Ticks -ne $row.last_write_utc_ticks){throw "Target changed since dry run: $($row.path)"}
  if((Get-ContentSha256 $item.FullName) -ne [string]$row.sha256){throw "Target bytes changed since dry run: $($row.path)"}
}
Assert-NoActiveRuntime
$freeBefore=[int64](Get-Volume -DriveLetter C).SizeRemaining
$removed=[Collections.Generic.List[object]]::new()
$failure=$null
try{
  foreach($row in $files){
    [void](Assert-Target ([string]$row.path))
    $item=Get-Item -LiteralPath $row.path
    if([int64]$item.Length -ne [int64]$row.bytes -or $item.LastWriteTimeUtc.Ticks -ne $row.last_write_utc_ticks){throw "Target changed during cleanup: $($row.path)"}
    if((Get-ContentSha256 $item.FullName) -ne [string]$row.sha256){throw "Target bytes changed during cleanup: $($row.path)"}
    Remove-Item -LiteralPath $item.FullName -Force
    if(Test-Path -LiteralPath $item.FullName){throw "Removal did not complete: $($item.FullName)"}
    $removed.Add($row)
  }
}catch{$failure=$_.Exception.Message}finally{
  $freeAfter=[int64](Get-Volume -DriveLetter C).SizeRemaining
  $result=[ordered]@{plan_sha256=$ExpectedPlanSha256;complete=($null -eq $failure -and $removed.Count -eq $files.Count);
    removed_file_count=$removed.Count;removed_logical_bytes=(Sum-LogicalBytes $removed.ToArray());
    free_before=$freeBefore;free_after=$freeAfter;observed_free_delta=$freeAfter-$freeBefore;
    error=$failure;removed_files=$removed.ToArray()}
  [IO.File]::WriteAllText($reportPath,($result | ConvertTo-Json -Depth 8))
  [ordered]@{report=$reportPath;complete=$result.complete;removed_files=$removed.Count;
    removed_logical_bytes=$result.removed_logical_bytes;observed_free_delta=$result.observed_free_delta} | ConvertTo-Json
}
if($failure){throw $failure}
