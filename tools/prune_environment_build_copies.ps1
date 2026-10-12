param(
  [Parameter(Mandatory=$true)][string]$PlanPath,
  [switch]$Apply,
  [string]$ExpectedPlanSha256,
  [string]$ExpectedHelperSha256,
  [string]$ResultPath)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 or newer is required'}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
$buildRoot=(Resolve-Path (Join-Path $repoRoot 'build')).Path
$failedRoot=(Resolve-Path (Join-Path $buildRoot 'gameview-environment-2026-10-08-attempt01')).Path
$helperPath=(Resolve-Path -LiteralPath $PSCommandPath).Path
$utf8=[Text.UTF8Encoding]::new($false)
$expectedTargetBytes=[int64]60070592

$objectNames=@(
  'asset_catalog.obj','atomic_file.obj','client_session.obj','contact_shadow.obj','economy.obj',
  'foundation.obj','game_art.obj','game_server.obj','gns_transport.obj','lobby.obj',
  'localization.obj','network_session.obj','platform.obj','protocol.obj','renderer2d.obj',
  'skills.obj','sky_shadow.obj','texture_atlas.obj','view.obj','world_save.obj','world.obj')
$relativeTargets=@($objectNames | ForEach-Object {"obj/$_"})

function Get-Sha256([string]$path) {
  return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

function Get-TextSha256([string]$text) {
  $algorithm=[Security.Cryptography.SHA256]::Create()
  try {return [Convert]::ToHexString($algorithm.ComputeHash($utf8.GetBytes($text)))}
  finally {$algorithm.Dispose()}
}

function Assert-NoReparseAncestors([string]$path,[string]$boundary) {
  $current=[IO.Path]::GetFullPath($path)
  $stop=[IO.Path]::GetFullPath($boundary)
  while($true){
    $item=Get-Item -LiteralPath $current -Force
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
      throw "Reparse-point ancestor is forbidden: $current"
    }
    if([StringComparer]::OrdinalIgnoreCase.Equals($current,$stop)){break}
    if(!$current.StartsWith(($stop+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
      throw "Path is outside validation boundary: $path"
    }
    $parent=Split-Path $current
    if([string]::IsNullOrWhiteSpace($parent) -or [StringComparer]::OrdinalIgnoreCase.Equals($parent,$current)){
      throw "Path escaped validation boundary: $path"
    }
    $current=$parent
  }
}

function Assert-NewOutput([string]$path,[string]$label) {
  if([string]::IsNullOrWhiteSpace($path)){throw "$label is required"}
  $full=[IO.Path]::GetFullPath($path)
  if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $full),$buildRoot)){
    throw "$label must be one fresh file directly inside the repository build directory"
  }
  if(Test-Path -LiteralPath $full){throw "Refusing to overwrite existing ${label}: $full"}
  Assert-NoReparseAncestors (Split-Path $full) $repoRoot
  return $full
}

function Get-FileRow([string]$path,[string]$relative) {
  $item=Get-Item -LiteralPath $path -Force
  if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw "Expected a regular non-reparse leaf: $path"
  }
  return [ordered]@{relative_path=$relative.Replace('\','/');path=$item.FullName;
    bytes=[int64]$item.Length;sha256=Get-Sha256 $item.FullName}
}

function New-Inventory([string]$label,[string]$root,[string[]]$exclude=@()) {
  $resolved=(Resolve-Path -LiteralPath $root).Path
  Assert-NoReparseAncestors $resolved $repoRoot
  $excluded=@{}; foreach($value in $exclude){$excluded[$value.Replace('\','/')]=$true}
  $items=@(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
  foreach($item in $items){
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
      throw "$label contains a reparse point: $($item.FullName)"
    }
  }
  $rows=@($items | Where-Object {-not $_.PSIsContainer} | Sort-Object FullName | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($resolved,$_.FullName).Replace('\','/')
    if(!$excluded.ContainsKey($relative)){Get-FileRow $_.FullName $relative}
  })
  [int64]$bytes=0; foreach($row in $rows){$bytes += [int64]$row.bytes}
  return [ordered]@{label=$label;root=$resolved;file_count=$rows.Count;total_bytes=$bytes;
    inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function New-FileSetInventory([string]$label,[string[]]$paths) {
  $rows=@($paths | Sort-Object | ForEach-Object {
    $full=(Resolve-Path -LiteralPath $_).Path
    Assert-NoReparseAncestors $full $repoRoot
    Get-FileRow $full ([IO.Path]::GetRelativePath($repoRoot,$full))
  })
  [int64]$bytes=0; foreach($row in $rows){$bytes += [int64]$row.bytes}
  return [ordered]@{label=$label;root=$repoRoot;file_count=$rows.Count;total_bytes=$bytes;
    inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function Test-Inventory([object]$expected,[object]$actual,[string]$phase) {
  foreach($field in @('label','root','file_count','total_bytes','inventory_sha256')){
    if([string]$expected.$field -ne [string]$actual.$field){
      throw "$phase inventory changed: $($expected.label) $field"
    }
  }
  if((ConvertTo-Json @($expected.files) -Depth 5 -Compress) -ne
     (ConvertTo-Json @($actual.files) -Depth 5 -Compress)){
    throw "$phase exact file inventory changed: $($expected.label)"
  }
}

function Get-LauncherFiles {
  $launcher=Join-Path $repoRoot 'Play Ribterra.cmd'
  $rework=Join-Path $repoRoot 'Play Ribterra Rework.cmd'
  $pointer=Join-Path $repoRoot 'dist/current-build.txt'
  foreach($path in @($launcher,$rework,$pointer)){
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing launcher evidence: $path"}
  }
  $rootExecutable=Join-Path $repoRoot 'Ribterra.exe'
  if(Test-Path -LiteralPath $rootExecutable -PathType Leaf){$currentTarget=$rootExecutable}
  else {
    $value=(Get-Content -Raw -LiteralPath $pointer).Trim()
    if([string]::IsNullOrWhiteSpace($value)){throw 'dist/current-build.txt is empty'}
    $currentTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $value))
  }
  $match=[regex]::Match((Get-Content -Raw -LiteralPath $rework),'(?im)^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$')
  if(!$match.Success){throw 'Could not resolve rework launcher target'}
  $reworkTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $match.Groups[1].Value))
  foreach($path in @($currentTarget,$reworkTarget)){
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing current launcher target: $path"}
  }
  return @($launcher,$rework,$pointer,$currentTarget,$reworkTarget)
}

function Get-ProtectedInventories {
  $roots=[ordered]@{
    latest_environment_build02='build/gameview-environment-2026-10-08-attempt02'
    environment_native01='build/gameview-environment-native-2026-10-08-attempt01'
    environment_native02='build/gameview-environment-native-2026-10-08-attempt02'
    atmosphere_build03='build/gameview-atmosphere-2026-10-08-attempt03'
    atmosphere_native02='build/gameview-atmosphere-native-2026-10-08-attempt02'
    atmosphere_native03='build/gameview-atmosphere-native-2026-10-08-attempt03'
    water_build02_rollback='build/gameview-water-2026-10-08-attempt02'
    control_kit='build/engine-control-kit-2026-10-08-check05/export-a'
  }
  $result=@()
  foreach($entry in $roots.GetEnumerator()){
    $path=Join-Path $repoRoot $entry.Value
    if(!(Test-Path -LiteralPath $path -PathType Container)){throw "Missing protected root: $path"}
    $result += New-Inventory $entry.Key $path
  }
  $result += New-FileSetInventory 'current_launchers_and_targets' (Get-LauncherFiles)
  return @($result)
}

function Get-TargetRows {
  $rows=@()
  foreach($relative in $relativeTargets){
    $path=[IO.Path]::GetFullPath((Join-Path $failedRoot $relative))
    if(!$path.StartsWith(($failedRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
      throw "Target escaped failed root: $relative"
    }
    Assert-NoReparseAncestors $path $repoRoot
    $rows += Get-FileRow $path $relative
  }
  $actualObjects=@(Get-ChildItem -LiteralPath $failedRoot -File -Recurse -Filter '*.obj' |
    ForEach-Object {[IO.Path]::GetRelativePath($failedRoot,$_.FullName).Replace('\','/')} | Sort-Object)
  $expectedObjects=@($relativeTargets | Sort-Object)
  if(Compare-Object $expectedObjects $actualObjects){
    throw 'Unexpected compiler OBJ name or location in failed environment build'
  }
  [int64]$bytes=0; foreach($row in $rows){$bytes += [int64]$row.bytes}
  if($rows.Count -ne 21 -or $bytes -ne $expectedTargetBytes){
    throw "Failed environment OBJ cohort mismatch: count=$($rows.Count), bytes=$bytes"
  }
  return @($rows)
}

function Assert-DisjointTargets([object[]]$targets,[object[]]$protectedInventories) {
  $protectedPaths=@{}
  foreach($inventory in $protectedInventories){
    foreach($file in @($inventory.files)){
      $protectedPaths[[IO.Path]::GetFullPath([string]$file.path).ToLowerInvariant()]=$inventory.label
    }
  }
  foreach($target in $targets){
    $full=[IO.Path]::GetFullPath([string]$target.path).ToLowerInvariant()
    if($protectedPaths.ContainsKey($full)){
      throw "Prune target overlaps protected inventory $($protectedPaths[$full]): $($target.path)"
    }
  }
}

function Assert-NoLiveReferences([object[]]$targets) {
  $lookup=@{}; foreach($row in $targets){$lookup[[IO.Path]::GetFullPath($row.path).ToLowerInvariant()]=$true}
  $hits=[Collections.Generic.List[string]]::new()
  $errors=[Collections.Generic.List[string]]::new()
  foreach($process in Get-Process){
    try {
      if($process.Path -and $lookup.ContainsKey([IO.Path]::GetFullPath($process.Path).ToLowerInvariant())){
        $hits.Add("image pid=$($process.Id) $($process.Path)")
      }
    } catch {$errors.Add("image pid=$($process.Id)")}
    try {
      foreach($module in $process.Modules){
        if($lookup.ContainsKey([IO.Path]::GetFullPath($module.FileName).ToLowerInvariant())){
          $hits.Add("module pid=$($process.Id) $($module.FileName)")
        }
      }
    } catch {$errors.Add("modules pid=$($process.Id)")}
  }
  if($errors.Count){throw "Could not complete live image/module audit: $($errors -join ', ')"}
  if($hits.Count){throw "A target is referenced by a live process: $($hits -join '; ')"}
}

function Write-NewJson([string]$path,[object]$value,[int]$depth=10) {
  if(Test-Path -LiteralPath $path){throw "Refusing to overwrite JSON evidence: $path"}
  [IO.File]::WriteAllText($path,(ConvertTo-Json $value -Depth $depth)+[Environment]::NewLine,$utf8)
}

$helperRow=Get-FileRow $helperPath ([IO.Path]::GetRelativePath($repoRoot,$helperPath))

if(!$Apply){
  if($ExpectedPlanSha256 -or $ExpectedHelperSha256 -or $ResultPath){
    throw 'Expected hashes and ResultPath are apply-only parameters'
  }
  $planOutput=Assert-NewOutput $PlanPath 'PlanPath'
  $targets=Get-TargetRows
  $protected=Get-ProtectedInventories
  Assert-DisjointTargets $targets $protected
  $plan=[ordered]@{
    schema=1
    classification='dry-run exact environment01 OBJ leaf prune plan; no file was deleted or moved'
    created_utc=[DateTimeOffset]::UtcNow.ToString('o')
    repository=$repoRoot
    failed_build_root=$failedRoot
    helper=$helperRow
    target_count=$targets.Count
    target_bytes=$expectedTargetBytes
    target_inventory_sha256=Get-TextSha256 (ConvertTo-Json @($targets) -Depth 5 -Compress)
    targets=$targets
    retained_failed_build=New-Inventory 'failed_environment01_non_targets' $failedRoot $relativeTargets
    protected_inventories=$protected
    apply_requirements=@('Apply switch','ExpectedPlanSha256','ExpectedHelperSha256','fresh ResultPath')
    evidence_caveat='Removing the compiled OBJ copies makes the retained failed-build report less self-contained; all non-target evidence remains.'
  }
  Write-NewJson $planOutput $plan 12
  Write-Output "Dry-run plan written: $planOutput"
  Write-Output "Plan SHA256: $(Get-Sha256 $planOutput)"
  Write-Output "Frozen helper SHA256: $($helperRow.sha256)"
  exit 0
}

if([string]::IsNullOrWhiteSpace($ExpectedPlanSha256) -or $ExpectedPlanSha256 -notmatch '^[0-9A-Fa-f]{64}$'){
  throw 'Apply requires a 64-hex ExpectedPlanSha256'
}
if([string]::IsNullOrWhiteSpace($ExpectedHelperSha256) -or $ExpectedHelperSha256 -notmatch '^[0-9A-Fa-f]{64}$'){
  throw 'Apply requires a 64-hex ExpectedHelperSha256'
}
$planInput=(Resolve-Path -LiteralPath $PlanPath).Path
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $planInput),$buildRoot)){
  throw 'Apply PlanPath must be one existing file directly inside the repository build directory'
}
Assert-NoReparseAncestors $planInput $repoRoot
(Get-FileRow $planInput ([IO.Path]::GetRelativePath($repoRoot,$planInput))) | Out-Null
$resultOutput=Assert-NewOutput $ResultPath 'ResultPath'
if((Get-Sha256 $planInput) -ne $ExpectedPlanSha256.ToUpperInvariant()){throw 'Plan SHA256 mismatch'}
if($helperRow.sha256 -ne $ExpectedHelperSha256.ToUpperInvariant()){throw 'Frozen helper SHA256 mismatch'}
$plan=Get-Content -Raw -LiteralPath $planInput | ConvertFrom-Json
if([int]$plan.schema -ne 1 -or $plan.classification -notlike 'dry-run exact environment01 OBJ leaf prune plan*'){
  throw 'Unsupported environment prune plan schema or classification'
}
if(-not [StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.repository,$repoRoot) -or
   -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.failed_build_root,$failedRoot)){
  throw 'Plan repository or failed-build identity mismatch'
}
if(-not [StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.helper.path,$helperRow.path) -or
   ([string]$plan.helper.sha256).ToUpperInvariant() -ne $helperRow.sha256 -or
   [int64]$plan.helper.bytes -ne [int64]$helperRow.bytes){throw 'Plan helper binding mismatch'}
if(@($plan.targets).Count -ne $relativeTargets.Count -or
   (Compare-Object @($relativeTargets) @($plan.targets.relative_path))){throw 'Plan target allowlist mismatch'}

$targetsBefore=Get-TargetRows
if([int]$plan.target_count -ne 21 -or [int64]$plan.target_bytes -ne $expectedTargetBytes -or
   ([string]$plan.target_inventory_sha256).ToUpperInvariant() -ne
     (Get-TextSha256 (ConvertTo-Json @($plan.targets) -Depth 5 -Compress))){
  throw 'Plan target aggregate binding mismatch'
}
if((ConvertTo-Json @($targetsBefore) -Depth 5 -Compress) -ne
   (ConvertTo-Json @($plan.targets) -Depth 5 -Compress)){throw 'Target bytes, hashes or paths changed since planning'}
$retainedBefore=New-Inventory 'failed_environment01_non_targets' $failedRoot $relativeTargets
Test-Inventory $plan.retained_failed_build $retainedBefore 'pre-apply'
$protectedBefore=Get-ProtectedInventories
if($protectedBefore.Count -ne @($plan.protected_inventories).Count){throw 'Protected inventory count changed'}
for($index=0;$index -lt $protectedBefore.Count;$index++){
  Test-Inventory $plan.protected_inventories[$index] $protectedBefore[$index] 'pre-apply'
}
Assert-DisjointTargets $targetsBefore $protectedBefore
Assert-NoLiveReferences $targetsBefore

$applied=[Collections.Generic.List[object]]::new()
$failure=$null
try {
  for($targetIndex=0;$targetIndex -lt $targetsBefore.Count;$targetIndex++){
    $row=$targetsBefore[$targetIndex]
    $relative=$relativeTargets[$targetIndex]
    $expectedPath=[IO.Path]::GetFullPath((Join-Path $failedRoot $relative))
    if(!$expectedPath.StartsWith(($failedRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
      throw "Immediate target escaped failed root: $relative"
    }
    if(-not [StringComparer]::OrdinalIgnoreCase.Equals([string]$row.path,$expectedPath) -or
       -not [StringComparer]::Ordinal.Equals(([string]$row.relative_path).Replace('\','/'),$relative)){
      throw "Immediate target path binding mismatch: $relative"
    }
    Assert-NoReparseAncestors $expectedPath $repoRoot
    $resolvedPath=(Resolve-Path -LiteralPath $expectedPath).Path
    if(-not [StringComparer]::OrdinalIgnoreCase.Equals($resolvedPath,$expectedPath)){
      throw "Immediate resolved target path mismatch: $relative"
    }
    $current=Get-FileRow $resolvedPath $relative
    if(-not [StringComparer]::OrdinalIgnoreCase.Equals([string]$current.path,[string]$row.path) -or
       [int64]$current.bytes -ne [int64]$row.bytes -or
       -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$current.sha256,[string]$row.sha256)){
      throw "Immediate target bytes or SHA256 changed: $relative"
    }
    Remove-Item -LiteralPath $resolvedPath
    if(Test-Path -LiteralPath $resolvedPath){throw "Target remained after leaf deletion: $relative"}
    $applied.Add($current)
  }
} catch {$failure=$_.Exception.Message}

[object[]]$remaining=@($plan.targets | Where-Object {Test-Path -LiteralPath $_.path})
$retainedAfter=$null; $protectedAfter=@(); $verificationErrors=[Collections.Generic.List[string]]::new()
try {$retainedAfter=New-Inventory 'failed_environment01_non_targets' $failedRoot $relativeTargets;
  Test-Inventory $plan.retained_failed_build $retainedAfter 'post-apply'} catch {$verificationErrors.Add($_.Exception.Message)}
try {
  $protectedAfter=Get-ProtectedInventories
  if($protectedAfter.Count -ne @($plan.protected_inventories).Count){throw 'Protected inventory count changed post-apply'}
  for($index=0;$index -lt $protectedAfter.Count;$index++){
    Test-Inventory $plan.protected_inventories[$index] $protectedAfter[$index] 'post-apply'
  }
} catch {$verificationErrors.Add($_.Exception.Message)}
if($remaining.Count){$verificationErrors.Add("Targets retained after apply attempt: $($remaining.relative_path -join ', ')")}
if($failure){$verificationErrors.Add("Leaf deletion failed: $failure")}
$success=($applied.Count -eq $relativeTargets.Count -and $verificationErrors.Count -eq 0)
[int64]$appliedBytes=0; foreach($row in $applied){$appliedBytes += [int64]$row.bytes}
$result=[ordered]@{
  schema=1
  classification='bounded exact environment01 OBJ leaf prune result; non-target evidence retained'
  applied_utc=[DateTimeOffset]::UtcNow.ToString('o')
  success=$success
  repository=$repoRoot
  failed_build_root=$failedRoot
  plan=[ordered]@{path=$planInput;bytes=[int64](Get-Item $planInput).Length;sha256=Get-Sha256 $planInput}
  helper=$helperRow
  applied_target_count=$applied.Count
  applied_target_bytes=$appliedBytes
  applied_targets=$applied.ToArray()
  remaining_targets=@($remaining)
  retained_failed_build=$retainedAfter
  protected_inventories=$protectedAfter
  verification_errors=$verificationErrors.ToArray()
  evidence_caveat='The failed environment build retains all non-target evidence but no longer retains the removed compiled OBJ copies.'
}
Write-NewJson $resultOutput $result 12
if(!$success){throw "Bounded environment prune did not complete cleanly; retained result: $resultOutput"}
Write-Output "Bounded environment OBJ prune completed; result: $resultOutput"
