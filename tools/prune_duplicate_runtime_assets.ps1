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
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"}
$artifactsRoot=(Resolve-Path (Join-Path $repoRoot 'artifacts')).Path
$buildRoot=(Resolve-Path (Join-Path $repoRoot 'build')).Path
$helperPath=(Resolve-Path -LiteralPath $PSCommandPath).Path
$utf8=[Text.UTF8Encoding]::new($false)
$expectedTargetCount=3346
$expectedTargetBytes=[int64]3377034378
$allowedExtensions=@('.png','.json','.dxil','.rcss','.ttf','.rml')

$pairs=@(
  [ordered]@{candidate='artifacts/concept-rebuild-2026-10-05-map-ground/bank-check/runtime/assets';anchor='artifacts/concept-rebuild-2026-10-05-map-ground/full-check-final/runtime/assets';count=184;bytes=[int64]195995683},
  [ordered]@{candidate='artifacts/concept-rebuild-2026-10-05-map-ground/client-check/runtime/assets';anchor='artifacts/concept-rebuild-2026-10-05-map-ground/full-check-final/runtime/assets';count=184;bytes=[int64]195995683},
  [ordered]@{candidate='artifacts/concept-rebuild-2026-10-05-map-ground/fixture-check/runtime/assets';anchor='artifacts/concept-rebuild-2026-10-05-map-ground/full-check-final/runtime/assets';count=184;bytes=[int64]195995683},
  [ordered]@{candidate='artifacts/concept-rebuild-2026-10-05-map-ground/full-check/runtime/assets';anchor='artifacts/concept-rebuild-2026-10-05-map-ground/full-check-final/runtime/assets';count=184;bytes=[int64]195995683},
  [ordered]@{candidate='artifacts/ground-forward-2026-10-07/check-final-01/runtime/assets';anchor='artifacts/ground-forward-2026-10-07/check-final-03/runtime/assets';count=218;bytes=[int64]199544555},
  [ordered]@{candidate='artifacts/ground-forward-2026-10-07/check-final-02/runtime/assets';anchor='artifacts/ground-forward-2026-10-07/check-final-03/runtime/assets';count=218;bytes=[int64]199544555},
  [ordered]@{candidate='artifacts/hdr-color-2026-10-06/check-02/runtime/assets';anchor='artifacts/hdr-color-2026-10-06/check-finalized-v2/runtime/assets';count=202;bytes=[int64]199468884},
  [ordered]@{candidate='artifacts/hdr-color-2026-10-06/check-finalized/runtime/assets';anchor='artifacts/hdr-color-2026-10-06/check-finalized-v2/runtime/assets';count=202;bytes=[int64]199468884},
  [ordered]@{candidate='artifacts/local-occlusion-2026-10-07/check-final-01/runtime/assets';anchor='artifacts/local-occlusion-2026-10-07/check-final-03/runtime/assets';count=218;bytes=[int64]199551636},
  [ordered]@{candidate='artifacts/material-bridge-2026-10-06/check-final/runtime/assets';anchor='artifacts/material-bridge-2026-10-06/check-final-v2/runtime/assets';count=206;bytes=[int64]199489002},
  [ordered]@{candidate='artifacts/material-master-2026-10-06/check-a/runtime/assets';anchor='artifacts/material-master-2026-10-06/check-final/runtime/assets';count=206;bytes=[int64]199489002},
  [ordered]@{candidate='artifacts/presentation-2026-10-05/check-baseline/runtime/assets';anchor='artifacts/surface-lighting-2026-10-05/check-final/runtime/assets';count=190;bytes=[int64]199414325},
  [ordered]@{candidate='artifacts/presentation-2026-10-05/check-final/runtime/assets';anchor='artifacts/surface-lighting-2026-10-05/check-final/runtime/assets';count=190;bytes=[int64]199414325},
  [ordered]@{candidate='artifacts/presentation-2026-10-05/check-optimized/runtime/assets';anchor='artifacts/surface-lighting-2026-10-05/check-final/runtime/assets';count=190;bytes=[int64]199414325},
  [ordered]@{candidate='artifacts/presentation-2026-10-05/check-pools/runtime/assets';anchor='artifacts/surface-lighting-2026-10-05/check-final/runtime/assets';count=190;bytes=[int64]199414325},
  [ordered]@{candidate='artifacts/presentation-2026-10-05/check-settings/runtime/assets';anchor='artifacts/presentation-2026-10-05/check-complete/runtime/assets';count=190;bytes=[int64]199423503},
  [ordered]@{candidate='artifacts/surface-lighting-2026-10-05/check/runtime/assets';anchor='artifacts/surface-lighting-2026-10-05/check-final/runtime/assets';count=190;bytes=[int64]199414325}
)

function Get-Sha256([string]$path){return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Get-TextSha256([string]$text){$algorithm=[Security.Cryptography.SHA256]::Create();try{return [Convert]::ToHexString($algorithm.ComputeHash($utf8.GetBytes($text)))}finally{$algorithm.Dispose()}}

function Assert-NoReparseAncestors([string]$path,[string]$boundary){
  $current=[IO.Path]::GetFullPath($path);$stop=[IO.Path]::GetFullPath($boundary)
  while($true){
    $item=Get-Item -LiteralPath $current -Force
    if(($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-ne0){throw "Reparse-point ancestor is forbidden: $current"}
    if([StringComparer]::OrdinalIgnoreCase.Equals($current,$stop)){break}
    if(!$current.StartsWith(($stop+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw "Path is outside validation boundary: $path"}
    $parent=Split-Path $current;if([string]::IsNullOrWhiteSpace($parent)-or[StringComparer]::OrdinalIgnoreCase.Equals($parent,$current)){throw "Path escaped validation boundary: $path"};$current=$parent
  }
}

function Assert-NewOutput([string]$path,[string]$label){
  if([string]::IsNullOrWhiteSpace($path)){throw "$label is required"};$full=[IO.Path]::GetFullPath($path)
  if(-not[StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $full),$buildRoot)){throw "$label must be one fresh file directly inside the repository build directory"}
  if(Test-Path -LiteralPath $full){throw "Refusing to overwrite existing ${label}: $full"};Assert-NoReparseAncestors (Split-Path $full) $repoRoot;return $full
}

function Get-FileRow([string]$path,[string]$relative){
  $item=Get-Item -LiteralPath $path -Force
  if($item.PSIsContainer-or($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-ne0){throw "Expected a regular non-reparse leaf: $path"}
  return [ordered]@{relative_path=$relative.Replace('\','/');path=$item.FullName;bytes=[int64]$item.Length;sha256=Get-Sha256 $item.FullName}
}

function New-Inventory([string]$label,[string]$root,[string]$excludePrefix=''){
  $resolved=(Resolve-Path -LiteralPath $root).Path;Assert-NoReparseAncestors $resolved $repoRoot
  $items=@(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
  foreach($item in $items){if(($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-ne0){throw "$label contains a reparse point: $($item.FullName)"}}
  $prefix=$excludePrefix.Replace('\','/').TrimEnd('/')
  $rows=@($items|Where-Object{-not$_.PSIsContainer}|Sort-Object FullName|ForEach-Object{$relative=[IO.Path]::GetRelativePath($resolved,$_.FullName).Replace('\','/');if(!$prefix-or-not($relative-eq$prefix-or$relative.StartsWith(($prefix+'/'),[StringComparison]::OrdinalIgnoreCase))){Get-FileRow $_.FullName $relative}})
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  return [ordered]@{label=$label;root=$resolved;file_count=$rows.Count;total_bytes=$bytes;inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function New-FileSetInventory([string]$label,[string[]]$paths){
  $rows=@($paths|Sort-Object|ForEach-Object{$full=(Resolve-Path -LiteralPath $_).Path;Assert-NoReparseAncestors $full $repoRoot;Get-FileRow $full ([IO.Path]::GetRelativePath($repoRoot,$full))})
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  return [ordered]@{label=$label;root=$repoRoot;file_count=$rows.Count;total_bytes=$bytes;inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function Test-Inventory([object]$expected,[object]$actual,[string]$phase){
  foreach($field in @('label','root','file_count','total_bytes','inventory_sha256')){if([string]$expected.$field-ne[string]$actual.$field){throw "$phase inventory changed: $($expected.label) $field"}}
  if((ConvertTo-Json @($expected.files) -Depth 5 -Compress)-ne(ConvertTo-Json @($actual.files) -Depth 5 -Compress)){throw "$phase exact file inventory changed: $($expected.label)"}
}

function Resolve-FixedAssetRoot([string]$relative,[string]$label){
  $full=(Resolve-Path -LiteralPath (Join-Path $repoRoot $relative)).Path
  if(!$full.StartsWith(($artifactsRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)-or-not$full.EndsWith("runtime$([IO.Path]::DirectorySeparatorChar)assets",[StringComparison]::OrdinalIgnoreCase)){throw "$label is not an exact historical runtime/assets root: $relative"}
  Assert-NoReparseAncestors $full $repoRoot;return $full
}

function Assert-RootTopology {
  $candidates=@($pairs|ForEach-Object{Resolve-FixedAssetRoot $_.candidate 'Candidate'});$anchors=@($pairs|ForEach-Object{Resolve-FixedAssetRoot $_.anchor 'Anchor'})
  if(@($candidates|Sort-Object -Unique).Count-ne$pairs.Count){throw 'Duplicate candidate root mapping'}
  foreach($candidate in $candidates){foreach($other in @($candidates+$anchors)){if([StringComparer]::OrdinalIgnoreCase.Equals($candidate,$other)){if($candidate-in$anchors){throw "Candidate overlaps anchor root: $candidate"};continue};if($candidate.StartsWith(($other+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)-or$other.StartsWith(($candidate+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw "Nested candidate/anchor roots are forbidden: $candidate <> $other"}}}
}

function Assert-AllowedDuplicateLeaf([string]$relative){
  $normalized=$relative.Replace('\','/');$extension=[IO.Path]::GetExtension($normalized).ToLowerInvariant()
  if($allowedExtensions-notcontains$extension){throw "Non-allowed runtime asset extension: $normalized"}
  if($normalized-match'(?i)(^|/)(source|sources|source[-_]?art|original|saves?)(/|$)|\.(save|ribsave|bak|cpp|cxx|cc|hpp|h|hlsl|psd|blend)$'){throw "Save, source or original-art path is forbidden: $normalized"}
}

function Get-TargetRows {
  Assert-RootTopology;$rows=@()
  for($pairIndex=0;$pairIndex-lt$pairs.Count;$pairIndex++){
    $pair=$pairs[$pairIndex];$candidate=Resolve-FixedAssetRoot $pair.candidate 'Candidate';$anchor=Resolve-FixedAssetRoot $pair.anchor 'Anchor'
    $candidateItems=@(Get-ChildItem -LiteralPath $candidate -Force -Recurse);$anchorItems=@(Get-ChildItem -LiteralPath $anchor -Force -Recurse)
    foreach($item in @($candidateItems+$anchorItems)){if(($item.Attributes-band[IO.FileAttributes]::ReparsePoint)-ne0){throw "Candidate or anchor contains reparse point: $($item.FullName)"}}
    $candidateFiles=@($candidateItems|Where-Object{-not$_.PSIsContainer}|Sort-Object FullName);$anchorFiles=@($anchorItems|Where-Object{-not$_.PSIsContainer})
    $anchorRelative=@{};foreach($file in $anchorFiles){$relative=[IO.Path]::GetRelativePath($anchor,$file.FullName).Replace('\','/');$anchorRelative[$relative]=$file.FullName}
    [int64]$pairBytes=0
    foreach($file in $candidateFiles){
      $relative=[IO.Path]::GetRelativePath($candidate,$file.FullName).Replace('\','/');Assert-AllowedDuplicateLeaf $relative
      if(!$anchorRelative.ContainsKey($relative)){throw "Missing protected counterpart: $($pair.candidate)/$relative"}
      $target=Get-FileRow $file.FullName $relative;$counterpart=Get-FileRow $anchorRelative[$relative] $relative
      if([int64]$target.bytes-ne[int64]$counterpart.bytes-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$target.sha256,[string]$counterpart.sha256)){throw "Counterpart differs: $($pair.candidate)/$relative"}
      $pairBytes+=[int64]$target.bytes
      $rows+=[ordered]@{pair_index=$pairIndex;candidate_root=$pair.candidate;anchor_root=$pair.anchor;relative_path=$relative;target_path=$target.path;anchor_path=$counterpart.path;bytes=$target.bytes;sha256=$target.sha256}
    }
    if($anchorRelative.Count-ne$candidateFiles.Count){throw "Unexpected anchor leaf count: $($pair.anchor)"}
    if($candidateFiles.Count-ne[int]$pair.count-or$pairBytes-ne[int64]$pair.bytes){throw "Candidate count or bytes changed: $($pair.candidate)"}
  }
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  if($rows.Count-ne$expectedTargetCount-or$bytes-ne$expectedTargetBytes){throw "Duplicate runtime target aggregate mismatch: count=$($rows.Count), bytes=$bytes"}
  return @($rows)
}

function Get-CandidateNonTargetInventories {
  $result=@();for($index=0;$index-lt$pairs.Count;$index++){$assetRoot=Resolve-FixedAssetRoot $pairs[$index].candidate 'Candidate';$attemptRoot=Split-Path (Split-Path $assetRoot);$result+=New-Inventory "candidate_non_target_$index" $attemptRoot 'runtime/assets'};return @($result)
}

function Get-AnchorInventories {
  $result=@();$seen=@{};foreach($pair in $pairs){$root=Resolve-FixedAssetRoot $pair.anchor 'Anchor';$key=$root.ToLowerInvariant();if(!$seen.ContainsKey($key)){$seen[$key]=$true;$result+=New-Inventory "protected_anchor_$($result.Count)" $root}};return @($result)
}

function Get-LauncherBindings {
  $launcher=Join-Path $repoRoot 'Play Ribterra.cmd';$rework=Join-Path $repoRoot 'Play Ribterra Rework.cmd';$pointer=Join-Path $repoRoot 'dist/current-build.txt'
  foreach($path in @($launcher,$rework,$pointer)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing launcher evidence: $path"}}
  $rootExecutable=Join-Path $repoRoot 'Ribterra.exe';if(Test-Path -LiteralPath $rootExecutable -PathType Leaf){$currentTarget=$rootExecutable}else{$value=(Get-Content -Raw -LiteralPath $pointer).Trim();if([string]::IsNullOrWhiteSpace($value)){throw 'dist/current-build.txt is empty'};$currentTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $value))}
  $match=[regex]::Match((Get-Content -Raw -LiteralPath $rework),'(?im)^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$');if(!$match.Success){throw 'Could not resolve rework launcher target'};$reworkTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $match.Groups[1].Value))
  foreach($path in @($currentTarget,$reworkTarget)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing launcher target: $path"}}
  return [ordered]@{files=@($launcher,$rework,$pointer,$currentTarget,$reworkTarget);target_roots=@((Split-Path $currentTarget),(Split-Path $reworkTarget)|Select-Object -Unique)}
}

function Get-ProtectedInventories {
  $roots=[ordered]@{
    authoritative_assets='assets';environment_build02='build/gameview-environment-2026-10-08-attempt02';environment_native01='build/gameview-environment-native-2026-10-08-attempt01';environment_native02='build/gameview-environment-native-2026-10-08-attempt02';atmosphere_build03='build/gameview-atmosphere-2026-10-08-attempt03';atmosphere_native02='build/gameview-atmosphere-native-2026-10-08-attempt02';atmosphere_native03='build/gameview-atmosphere-native-2026-10-08-attempt03';water_build02_rollback='build/gameview-water-2026-10-08-attempt02';control_kit='build/engine-control-kit-2026-10-08-check05/export-a'
  }
  $result=@();foreach($entry in $roots.GetEnumerator()){$path=Join-Path $repoRoot $entry.Value;if(!(Test-Path -LiteralPath $path -PathType Container)){throw "Missing protected root: $path"};$result+=New-Inventory $entry.Key $path}
  $launch=Get-LauncherBindings;$result+=New-FileSetInventory 'current_launchers_pointer_and_executables' $launch.files;$index=0;foreach($targetRoot in $launch.target_roots){$result+=New-Inventory "current_launcher_package_$index" $targetRoot;$index++};return @($result)
}

function Assert-DisjointTargets([object[]]$targets,[object[]]$inventories){
  $lookup=@{};foreach($inventory in $inventories){foreach($file in @($inventory.files)){$lookup[[IO.Path]::GetFullPath([string]$file.path).ToLowerInvariant()]=$inventory.label}}
  foreach($target in $targets){$full=[IO.Path]::GetFullPath([string]$target.target_path).ToLowerInvariant();if($lookup.ContainsKey($full)){throw "Duplicate target overlaps protected inventory $($lookup[$full]): $($target.target_path)"}}
}

function Test-PathWithinRoot([string]$path,[string]$root){
  $full=[IO.Path]::GetFullPath($path);$boundary=[IO.Path]::GetFullPath($root)
  return [StringComparer]::OrdinalIgnoreCase.Equals($full,$boundary)-or
    $full.StartsWith(($boundary+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)
}

function Get-CandidateAttemptRoots {
  $roots=@($pairs|ForEach-Object{$assetRoot=Resolve-FixedAssetRoot $_.candidate 'Candidate';Split-Path (Split-Path $assetRoot)}|Sort-Object -Unique)
  if($roots.Count-ne$pairs.Count){throw 'Candidate attempt root count mismatch'}
  foreach($root in $roots){Assert-NoReparseAncestors $root $repoRoot}
  return @($roots)
}

function Assert-NoLiveReferences {
  $candidateRoots=Get-CandidateAttemptRoots
  $hits=[Collections.Generic.List[string]]::new();$errors=[Collections.Generic.List[string]]::new()
  foreach($process in Get-Process){
    try {
      if($process.Path){
        foreach($root in $candidateRoots){if(Test-PathWithinRoot $process.Path $root){$hits.Add("image pid=$($process.Id) path=$($process.Path)");break}}
      }
    }catch{$errors.Add("image pid=$($process.Id)")}
    try {
      foreach($module in $process.Modules){
        foreach($root in $candidateRoots){if(Test-PathWithinRoot $module.FileName $root){$hits.Add("module pid=$($process.Id) path=$($module.FileName)");break}}
      }
    }catch{$errors.Add("modules pid=$($process.Id)")}
  }
  if($errors.Count){throw "Could not complete candidate-root live image/module audit: $($errors -join ', ')"}
  if($hits.Count){throw "A process image or module is inside a candidate attempt root: $($hits -join '; ')"}
}

function Write-NewJson([string]$path,[object]$value,[int]$depth=10){if(Test-Path -LiteralPath $path){throw "Refusing to overwrite JSON evidence: $path"};[IO.File]::WriteAllText($path,(ConvertTo-Json $value -Depth $depth)+[Environment]::NewLine,$utf8)}

$helperRow=Get-FileRow $helperPath ([IO.Path]::GetRelativePath($repoRoot,$helperPath))

if(!$Apply){
  if($ExpectedPlanSha256-or$ExpectedHelperSha256-or$ResultPath){throw 'Expected hashes and ResultPath are apply-only parameters'}
  $planOutput=Assert-NewOutput $PlanPath 'PlanPath';$targets=Get-TargetRows;$anchors=Get-AnchorInventories;$candidateNonTargets=Get-CandidateNonTargetInventories;$protected=Get-ProtectedInventories;Assert-DisjointTargets $targets @($anchors+$candidateNonTargets+$protected)
  $plan=[ordered]@{schema=1;classification='dry-run exact duplicate runtime asset leaf plan; no file was deleted, moved or copied';created_utc=[DateTimeOffset]::UtcNow.ToString('o');repository=$repoRoot;artifacts_root=$artifactsRoot;helper=$helperRow;target_count=$targets.Count;target_bytes=$expectedTargetBytes;target_inventory_sha256=Get-TextSha256 (ConvertTo-Json @($targets) -Depth 6 -Compress);targets=$targets;protected_anchors=$anchors;candidate_non_target_inventories=$candidateNonTargets;protected_inventories=$protected;apply_requirements=@('Apply switch','ExpectedPlanSha256','ExpectedHelperSha256','fresh ResultPath');live_gate_limit='The fail-closed gate rejects process images and loaded modules inside candidate attempt roots. It cannot prove that an outside process has no ordinary open asset-file handle; a locked leaf may therefore cause a sharing violation and a recorded partial failure.';evidence_caveat='Sixteen historical snapshots cease to be complete in place after Apply. Every removed byte is exactly recoverable from a protected anchor at planning and Apply time, but historical reports remain historical rather than current claims.'}
  Write-NewJson $planOutput $plan 12;Write-Output "Dry-run plan written: $planOutput";Write-Output "Plan SHA256: $(Get-Sha256 $planOutput)";Write-Output "Frozen helper SHA256: $($helperRow.sha256)";exit 0
}

if([string]::IsNullOrWhiteSpace($ExpectedPlanSha256)-or$ExpectedPlanSha256-notmatch'^[0-9A-Fa-f]{64}$'){throw 'Apply requires a 64-hex ExpectedPlanSha256'}
if([string]::IsNullOrWhiteSpace($ExpectedHelperSha256)-or$ExpectedHelperSha256-notmatch'^[0-9A-Fa-f]{64}$'){throw 'Apply requires a 64-hex ExpectedHelperSha256'}
$planInput=(Resolve-Path -LiteralPath $PlanPath).Path;if(-not[StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $planInput),$buildRoot)){throw 'Apply PlanPath must be one existing file directly inside repository build'};Assert-NoReparseAncestors $planInput $repoRoot;(Get-FileRow $planInput ([IO.Path]::GetRelativePath($repoRoot,$planInput)))|Out-Null
$resultOutput=Assert-NewOutput $ResultPath 'ResultPath';if((Get-Sha256 $planInput)-ne$ExpectedPlanSha256.ToUpperInvariant()){throw 'Plan SHA256 mismatch'};if($helperRow.sha256-ne$ExpectedHelperSha256.ToUpperInvariant()){throw 'Frozen helper SHA256 mismatch'}
$plan=Get-Content -Raw -LiteralPath $planInput|ConvertFrom-Json
if([int]$plan.schema-ne1-or$plan.classification-notlike'dry-run exact duplicate runtime asset leaf plan*'){throw 'Unsupported duplicate runtime asset plan'}
if(-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.repository,$repoRoot)-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.artifacts_root,$artifactsRoot)){throw 'Plan repository identity mismatch'}
if(-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.helper.path,$helperRow.path)-or([string]$plan.helper.sha256).ToUpperInvariant()-ne$helperRow.sha256-or[int64]$plan.helper.bytes-ne[int64]$helperRow.bytes){throw 'Plan helper binding mismatch'}
$targetsBefore=Get-TargetRows
if([int]$plan.target_count-ne$expectedTargetCount-or[int64]$plan.target_bytes-ne$expectedTargetBytes-or([string]$plan.target_inventory_sha256).ToUpperInvariant()-ne(Get-TextSha256 (ConvertTo-Json @($plan.targets) -Depth 6 -Compress))){throw 'Plan target aggregate binding mismatch'}
if((ConvertTo-Json @($targetsBefore) -Depth 6 -Compress)-ne(ConvertTo-Json @($plan.targets) -Depth 6 -Compress)){throw 'Duplicate target or counterpart changed since planning'}
$anchorsBefore=Get-AnchorInventories;$nonTargetsBefore=Get-CandidateNonTargetInventories;$protectedBefore=Get-ProtectedInventories
foreach($set in @(@{name='anchor';expected=@($plan.protected_anchors);actual=$anchorsBefore},@{name='candidate non-target';expected=@($plan.candidate_non_target_inventories);actual=$nonTargetsBefore},@{name='protected';expected=@($plan.protected_inventories);actual=$protectedBefore})){if($set.actual.Count-ne$set.expected.Count){throw "$($set.name) inventory count changed"};for($index=0;$index-lt$set.actual.Count;$index++){Test-Inventory $set.expected[$index] $set.actual[$index] 'pre-apply'}}
Assert-DisjointTargets $targetsBefore @($anchorsBefore+$nonTargetsBefore+$protectedBefore);Assert-NoLiveReferences

$applied=[Collections.Generic.List[object]]::new();$failure=$null
try{
  foreach($row in $targetsBefore){
    $pairIndex=[int]$row.pair_index;if($pairIndex-lt0-or$pairIndex-ge$pairs.Count){throw 'Immediate pair index mismatch'};$pair=$pairs[$pairIndex]
    if(-not[StringComparer]::Ordinal.Equals([string]$row.candidate_root,[string]$pair.candidate)-or-not[StringComparer]::Ordinal.Equals([string]$row.anchor_root,[string]$pair.anchor)){throw "Immediate fixed pair mismatch: $pairIndex"};Assert-AllowedDuplicateLeaf ([string]$row.relative_path)
    $candidate=Resolve-FixedAssetRoot $pair.candidate 'Candidate';$anchor=Resolve-FixedAssetRoot $pair.anchor 'Anchor';$targetPath=[IO.Path]::GetFullPath((Join-Path $candidate ([string]$row.relative_path)));$anchorPath=[IO.Path]::GetFullPath((Join-Path $anchor ([string]$row.relative_path)))
    if(!$targetPath.StartsWith(($candidate+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)-or!$anchorPath.StartsWith(($anchor+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)-or-not[StringComparer]::OrdinalIgnoreCase.Equals($targetPath,[string]$row.target_path)-or-not[StringComparer]::OrdinalIgnoreCase.Equals($anchorPath,[string]$row.anchor_path)){throw "Immediate target/counterpart path mismatch: $($row.relative_path)"}
    Assert-NoReparseAncestors $targetPath $repoRoot;Assert-NoReparseAncestors $anchorPath $repoRoot;$resolvedTarget=(Resolve-Path -LiteralPath $targetPath).Path;$resolvedAnchor=(Resolve-Path -LiteralPath $anchorPath).Path
    if(-not[StringComparer]::OrdinalIgnoreCase.Equals($resolvedTarget,$targetPath)-or-not[StringComparer]::OrdinalIgnoreCase.Equals($resolvedAnchor,$anchorPath)){throw "Immediate resolved path mismatch: $($row.relative_path)"}
    $target=Get-FileRow $resolvedTarget ([string]$row.relative_path);$counterpart=Get-FileRow $resolvedAnchor ([string]$row.relative_path)
    if([int64]$target.bytes-ne[int64]$row.bytes-or[int64]$counterpart.bytes-ne[int64]$row.bytes-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$target.sha256,[string]$row.sha256)-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$counterpart.sha256,[string]$row.sha256)){throw "Immediate target/counterpart bytes or SHA256 changed: $($row.relative_path)"}
    Remove-Item -LiteralPath $resolvedTarget;if(Test-Path -LiteralPath $resolvedTarget){throw "Duplicate target remained after leaf deletion: $resolvedTarget"};$applied.Add($row)
  }
}catch{$failure=$_.Exception.Message}

[object[]]$remaining=@($plan.targets|Where-Object{Test-Path -LiteralPath $_.target_path});$anchorsAfter=@();$nonTargetsAfter=@();$protectedAfter=@();$verificationErrors=[Collections.Generic.List[string]]::new()
foreach($set in @(@{name='anchor';expected=@($plan.protected_anchors);getter={Get-AnchorInventories}},@{name='candidate non-target';expected=@($plan.candidate_non_target_inventories);getter={Get-CandidateNonTargetInventories}},@{name='protected';expected=@($plan.protected_inventories);getter={Get-ProtectedInventories}})){try{$actual=@(& $set.getter);if($actual.Count-ne$set.expected.Count){throw "$($set.name) inventory count changed post-apply"};for($index=0;$index-lt$actual.Count;$index++){Test-Inventory $set.expected[$index] $actual[$index] 'post-apply'};if($set.name-eq'anchor'){$anchorsAfter=$actual}elseif($set.name-eq'candidate non-target'){$nonTargetsAfter=$actual}else{$protectedAfter=$actual}}catch{$verificationErrors.Add($_.Exception.Message)}}
if($remaining.Count){$verificationErrors.Add("Targets retained after apply attempt: $($remaining.target_path -join ', ')")};if($failure){$verificationErrors.Add("Leaf deletion failed: $failure")};$success=($applied.Count-eq$expectedTargetCount-and$verificationErrors.Count-eq0);[int64]$appliedBytes=0;foreach($row in $applied){$appliedBytes+=[int64]$row.bytes}
$result=[ordered]@{schema=1;classification='bounded duplicate runtime asset leaf result; anchors and non-target evidence retained';applied_utc=[DateTimeOffset]::UtcNow.ToString('o');success=$success;repository=$repoRoot;artifacts_root=$artifactsRoot;plan=[ordered]@{path=$planInput;bytes=[int64](Get-Item $planInput).Length;sha256=Get-Sha256 $planInput};helper=$helperRow;applied_target_count=$applied.Count;applied_target_bytes=$appliedBytes;applied_targets=$applied.ToArray();remaining_targets=@($remaining);protected_anchors=$anchorsAfter;candidate_non_target_inventories=$nonTargetsAfter;protected_inventories=$protectedAfter;verification_errors=$verificationErrors.ToArray();live_gate_limit='The fail-closed gate rejects process images and loaded modules inside candidate attempt roots. It cannot prove that an outside process has no ordinary open asset-file handle; a locked leaf may therefore cause a sharing violation and a recorded partial failure.';evidence_caveat='Sixteen historical snapshots are no longer complete in place. Removed bytes remain exactly represented by protected anchors at result verification time; historical reports are not current runtime claims.'}
Write-NewJson $resultOutput $result 12;if(!$success){throw "Bounded duplicate runtime asset prune did not complete cleanly; retained result: $resultOutput"};Write-Output "Bounded duplicate runtime asset prune completed; result: $resultOutput"
