param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [switch]$Apply,
  [string]$ExpectedPlanSha256)

$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,'C:\Users\fipst\Desktop\ribterra')){
  throw 'This bounded cleanup is only for the authoritative Ribterra checkout.'
}
$planDirectory=(Resolve-Path -LiteralPath (Join-Path $repoRoot '.omx/plans')).Path
$planFull=[IO.Path]::GetFullPath($PlanPath)
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path -Parent $planFull),$planDirectory)){
  throw 'PlanPath must be a new explicit file directly in .omx/plans.'
}

$candidateRoots=@(6..9 | ForEach-Object {
  (Resolve-Path -LiteralPath (Join-Path $repoRoot ("build/sky-authored-2026-10-08-attempt{0:00}" -f $_))).Path
})
$rollbackRoot=(Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/sky-authored-2026-10-08-attempt05')).Path
$latestRoot=(Resolve-Path -LiteralPath (Join-Path $repoRoot 'build/sky-authored-2026-10-08-attempt10')).Path
$relativeCandidates=@(
  'AuthoredSkyBridgeProbe.exe','AuthoredSkyNativeProbe.exe','MaterialBridgeProbe.exe',
  'SDL3.dll','SDL3_ttf.dll','obj\asset_catalog.obj','obj\authored_sky_bridge_probe.obj',
  'obj\authored_sky_native_probe.obj','obj\contact_shadow.obj','obj\foundation.obj',
  'obj\game_art.obj','obj\material_bridge_probe.obj','obj\renderer2d.obj','obj\sky_shadow.obj')
$compiledExtensions=@('.exe','.dll','.obj')

function Assert-NoReparseTree([string]$Root){
  $rootItem=Get-Item -LiteralPath $Root
  if(($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse root: $Root"}
  $reparse=@(Get-ChildItem -LiteralPath $Root -Recurse -Force | Where-Object {
    ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
  })
  if($reparse.Count){throw "Reparse child: $($reparse[0].FullName)"}
}

function Assert-CanonicalFile([string]$Path,[string[]]$AllowedRoots){
  $full=[IO.Path]::GetFullPath($Path)
  if(-not [StringComparer]::OrdinalIgnoreCase.Equals($full,$Path)){throw "Noncanonical path: $Path"}
  $inside=@($AllowedRoots | Where-Object {
    $full.StartsWith($_+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)
  })
  if($inside.Count -ne 1){throw "Path is outside its fixed root: $Path"}
  $cursor=$full
  while($cursor -and -not [StringComparer]::OrdinalIgnoreCase.Equals($cursor,$repoRoot)){
    $item=Get-Item -LiteralPath $cursor
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse ancestry: $cursor"}
    $cursor=Split-Path -Parent $cursor
  }
  return $full
}

function Get-Sha256([string]$Path){
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Sum-LogicalBytes([object[]]$Rows){
  $total=[int64]0
  foreach($row in $Rows){
    $bytes=[int64]$row.bytes
    if($bytes -lt 0 -or $total -gt [int64]::MaxValue-$bytes){throw 'Invalid or overflowing logical byte count.'}
    $total+=$bytes
  }
  return $total
}

function Write-NewUtf8Text([string]$Path,[string]$Text){
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes($Text)
  $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}

function Get-FileRow([string]$Path,[string]$Scope,[string[]]$AllowedRoots){
  $full=Assert-CanonicalFile $Path $AllowedRoots
  $item=Get-Item -LiteralPath $full
  if($item.PSIsContainer){throw "Expected file: $full"}
  return [ordered]@{path=$full;scope=$Scope;bytes=[int64]$item.Length;
    last_write_utc_ticks=[int64]$item.LastWriteTimeUtc.Ticks;sha256=(Get-Sha256 $full)}
}

function Get-CandidatePaths{
  foreach($root in $candidateRoots){Assert-NoReparseTree $root}
  $expected=@(foreach($root in $candidateRoots){foreach($relative in $relativeCandidates){
    [IO.Path]::GetFullPath((Join-Path $root $relative))
  }})
  $actual=@(foreach($root in $candidateRoots){
    Get-ChildItem -LiteralPath $root -Recurse -Force -File | Where-Object {
      $_.Extension.ToLowerInvariant() -in $compiledExtensions
    } | ForEach-Object {$_.FullName}
  })
  if($expected.Count -ne 56 -or $actual.Count -ne 56 -or
     @(Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)).Count){
    throw 'The fixed authored-build compiled-copy inventory is not exactly 56 files.'
  }
  foreach($path in $expected){[void](Assert-CanonicalFile $path $candidateRoots)}
  return @($expected | Sort-Object)
}

function Resolve-LauncherAnchors{
  $mainLauncher=(Resolve-Path -LiteralPath (Join-Path $repoRoot 'Play Ribterra.cmd')).Path
  $reworkLauncher=(Resolve-Path -LiteralPath (Join-Path $repoRoot 'Play Ribterra Rework.cmd')).Path
  $pointer=(Resolve-Path -LiteralPath (Join-Path $repoRoot 'dist/current-build.txt')).Path
  $rootExecutable=Join-Path $repoRoot 'Ribterra.exe'
  if(Test-Path -LiteralPath $rootExecutable -PathType Leaf){
    $mainTarget=(Resolve-Path -LiteralPath $rootExecutable).Path
  }else{
    $mainRelative=(Get-Content -LiteralPath $pointer -Raw).Trim()
    if([string]::IsNullOrWhiteSpace($mainRelative) -or [IO.Path]::IsPathRooted($mainRelative)){throw 'Invalid current-build pointer.'}
    $mainTarget=(Resolve-Path -LiteralPath (Join-Path $repoRoot $mainRelative)).Path
  }
  $match=[regex]::Match((Get-Content -LiteralPath $reworkLauncher -Raw),'(?im)^set "RIBTERRA_REWORK=([^"\r\n]+\.exe)"\s*$')
  if(-not $match.Success -or [IO.Path]::IsPathRooted($match.Groups[1].Value)){throw 'Unable to resolve the rework launcher target.'}
  $reworkTarget=(Resolve-Path -LiteralPath (Join-Path $repoRoot $match.Groups[1].Value)).Path
  foreach($target in @($mainTarget,$reworkTarget)){[void](Assert-CanonicalFile $target @($repoRoot))}
  return @($mainLauncher,$reworkLauncher,$pointer,$mainTarget,$reworkTarget)
}

function Get-ProtectedRows([string[]]$CandidatePaths){
  $rows=[Collections.Generic.List[object]]::new()
  foreach($root in $candidateRoots){
    Assert-NoReparseTree $root
    foreach($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File | Sort-Object FullName)){
      if($item.FullName -notin $CandidatePaths){$rows.Add((Get-FileRow $item.FullName 'candidate-root-noncandidate' @($root)))}
    }
  }
  foreach($root in @($rollbackRoot,$latestRoot)){
    Assert-NoReparseTree $root
    foreach($item in (Get-ChildItem -LiteralPath $root -Recurse -Force -File | Sort-Object FullName)){
      $rows.Add((Get-FileRow $item.FullName 'rollback-or-latest-full-tree' @($root)))
    }
  }
  $gameViewRoots=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build') -Directory | Where-Object {
    $_.Name -like 'gameview-engine-2026-10-08-attempt*' -or
    $_.Name -like 'gameview-engine-native-2026-10-08-attempt*'
  } | Sort-Object FullName)
  foreach($rootItem in $gameViewRoots){
    Assert-NoReparseTree $rootItem.FullName
    foreach($item in (Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force -File | Sort-Object FullName)){
      $rows.Add((Get-FileRow $item.FullName 'current-gameview-proof' @($rootItem.FullName)))
    }
  }
  foreach($path in (Resolve-LauncherAnchors)){
    $rows.Add((Get-FileRow $path 'launcher-or-resolved-target' @($repoRoot)))
  }
  if(@($rows.path | Sort-Object -Unique).Count -ne $rows.Count){throw 'Protected anchor paths are not unique.'}
  return @($rows.ToArray() | Sort-Object path)
}

function Assert-RowsUnchanged([object[]]$Expected,[object[]]$Actual,[string]$Label){
  if($Expected.Count -ne $Actual.Count){throw "$Label file count changed."}
  $expectedByPath=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  $actualByPath=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($row in $Expected){
    $path=[string]$row.path
    if([string]::IsNullOrWhiteSpace($path) -or $expectedByPath.ContainsKey($path)){throw "$Label expected paths are invalid or duplicate."}
    $expectedByPath.Add($path,$row)
  }
  foreach($row in $Actual){
    $path=[string]$row.path
    if([string]::IsNullOrWhiteSpace($path) -or $actualByPath.ContainsKey($path)){throw "$Label actual paths are invalid or duplicate."}
    $actualByPath.Add($path,$row)
  }
  foreach($path in $expectedByPath.Keys){
    if(-not $actualByPath.ContainsKey($path)){throw "$Label missing: $path"}
    $a=$expectedByPath[$path];$b=$actualByPath[$path]
    if($a.scope -ne $b.scope -or [int64]$a.bytes -ne [int64]$b.bytes -or
       [int64]$a.last_write_utc_ticks -ne [int64]$b.last_write_utc_ticks -or $a.sha256 -ne $b.sha256){
      throw "$Label changed: $path"
    }
  }
}

function Assert-NoCandidateProcess([string[]]$CandidatePaths){
  $candidateExecutables=@($CandidatePaths | Where-Object {[IO.Path]::GetExtension($_) -eq '.exe'})
  $active=@(Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -and $_.ExecutablePath -in $candidateExecutables
  })
  if($active.Count){throw 'A candidate authored-build executable is running; no file may be removed.'}
}

$candidatePaths=Get-CandidatePaths
Assert-NoCandidateProcess $candidatePaths

if(-not $Apply){
  if(Test-Path -LiteralPath $planFull){throw 'Refusing to overwrite a cleanup plan.'}
  $candidateRows=@($candidatePaths | ForEach-Object {Get-FileRow $_ 'obsolete-authored-compiled-copy' $candidateRoots})
  $protectedRows=Get-ProtectedRows $candidatePaths
  $logicalBytes=Sum-LogicalBytes $candidateRows
  $plan=[ordered]@{schema=1;scope='Only 56 fixed compiled copies in authored attempts 06-09';repository=$repoRoot;
    cleanup_script=$PSCommandPath;cleanup_script_sha256=(Get-Sha256 $PSCommandPath);
    candidate_roots=$candidateRoots;candidate_files=$candidateRows;candidate_count=$candidateRows.Count;
    logical_bytes=$logicalBytes;protected_files=$protectedRows;protected_count=$protectedRows.Count;
    preservation='Attempts 05 and 10, every noncandidate file in 06-09, current GameView proofs, launchers, resolved launcher targets, sources, reports, logs, PNGs, assets, shaders, kits, fonts, and generated files remain.';
    notification='Apply is permitted only after the user is shown every candidate path and this exact plan SHA256.';
    recovery='Removal is permanent locally; these compiled copies are rebuildable but are not sent to the recycle bin.'}
  Write-NewUtf8Text $planFull ($plan | ConvertTo-Json -Depth 8)
  [ordered]@{plan=$planFull;sha256=(Get-Sha256 $planFull);files=$candidateRows.Count;
    logical_bytes=$logicalBytes;protected_files=$protectedRows.Count} | ConvertTo-Json
  exit 0
}

if([string]::IsNullOrWhiteSpace($ExpectedPlanSha256)){throw 'Apply requires the exact SHA256 of the user-notified plan.'}
if(-not (Test-Path -LiteralPath $planFull -PathType Leaf)){throw 'Cleanup plan does not exist.'}
if((Get-Sha256 $planFull) -ne $ExpectedPlanSha256){throw 'Cleanup plan hash differs from the user-notified plan.'}
$plan=Get-Content -LiteralPath $planFull -Raw | ConvertFrom-Json
if($plan.schema -ne 1 -or $plan.repository -ne $repoRoot -or $plan.candidate_count -ne 56){throw 'Cleanup plan identity mismatch.'}
if($plan.cleanup_script -ne $PSCommandPath -or $plan.cleanup_script_sha256 -ne (Get-Sha256 $PSCommandPath)){
  throw 'Cleanup helper changed since the dry plan was created.'
}
$reportPath=$planFull+'.result.json'
if(Test-Path -LiteralPath $reportPath){throw 'Refusing to overwrite a cleanup result.'}
$plannedCandidates=@($plan.candidate_files | Sort-Object path)
if($plannedCandidates.Count -ne 56 -or @($plannedCandidates.path | Sort-Object -Unique).Count -ne 56 -or
   @(Compare-Object $candidatePaths @($plannedCandidates.path)).Count){throw 'Cleanup candidate inventory changed.'}
$actualCandidates=@($candidatePaths | ForEach-Object {Get-FileRow $_ 'obsolete-authored-compiled-copy' $candidateRoots})
Assert-RowsUnchanged $plannedCandidates $actualCandidates 'Candidate'
$actualLogicalBytes=Sum-LogicalBytes $actualCandidates
if($actualLogicalBytes -le 0 -or [int64]$plan.logical_bytes -ne $actualLogicalBytes){
  throw 'Cleanup plan logical byte count is invalid or differs from the candidate files.'
}
$plannedProtected=@($plan.protected_files | Sort-Object path)
$actualProtected=Get-ProtectedRows $candidatePaths
Assert-RowsUnchanged $plannedProtected $actualProtected 'Protected anchor'
Assert-NoCandidateProcess $candidatePaths

$freeBefore=[int64](Get-Volume -DriveLetter C).SizeRemaining
$removed=[Collections.Generic.List[object]]::new();$failure=$null;$protectedAfter=@();$protectedError=$null
try{
  foreach($row in $plannedCandidates){
    Assert-NoCandidateProcess $candidatePaths
    $current=Get-FileRow ([string]$row.path) 'obsolete-authored-compiled-copy' $candidateRoots
    Assert-RowsUnchanged @($row) @($current) 'Candidate during cleanup'
    Remove-Item -LiteralPath $current.path -Force
    if(Test-Path -LiteralPath $current.path){throw "Removal did not complete: $($current.path)"}
    $removed.Add($row)
  }
}catch{$failure=$_.Exception.Message}finally{
  try{$protectedAfter=Get-ProtectedRows $candidatePaths;Assert-RowsUnchanged $plannedProtected $protectedAfter 'Protected anchor after cleanup'}
  catch{$protectedError=$_.Exception.Message;if(-not $failure){$failure=$protectedError}}
  $remaining=@($plannedCandidates | Where-Object {Test-Path -LiteralPath $_.path})
  $freeAfter=[int64](Get-Volume -DriveLetter C).SizeRemaining
  $result=[ordered]@{plan_sha256=$ExpectedPlanSha256;complete=($null -eq $failure -and $removed.Count -eq 56 -and $remaining.Count -eq 0);
    exit_code=$(if($null -eq $failure){0}else{1});
    removed_file_count=$removed.Count;removed_logical_bytes=(Sum-LogicalBytes $removed.ToArray());
    removed_files=$removed.ToArray();
    remaining_file_count=$remaining.Count;remaining_files=@($remaining | ForEach-Object {[string]$_.path});free_before=$freeBefore;free_after=$freeAfter;
    observed_free_delta=$freeAfter-$freeBefore;protected_files=$protectedAfter;protected_error=$protectedError;error=$failure}
  Write-NewUtf8Text $reportPath ($result | ConvertTo-Json -Depth 8)
  [ordered]@{report=$reportPath;complete=$result.complete;removed_files=$removed.Count;remaining_files=$remaining.Count;
    removed_logical_bytes=$result.removed_logical_bytes;observed_free_delta=$result.observed_free_delta} | ConvertTo-Json
}
if($failure){throw $failure}
