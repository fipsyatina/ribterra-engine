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
$artifactsRoot=(Resolve-Path (Join-Path $repoRoot 'artifacts')).Path
$buildRoot=(Resolve-Path (Join-Path $repoRoot 'build')).Path
$helperPath=(Resolve-Path -LiteralPath $PSCommandPath).Path
$utf8=[Text.UTF8Encoding]::new($false)
$compiledExtensions=@('.exe','.dll','.lib','.pdb','.obj','.ilk','.exp')
$expectedTargetCount=104
$expectedTargetBytes=[int64]1506551808

$targetMap=[ordered]@{
  'package-0.10.0-final'=@(
    'Ribterra/ManualOrderVisualProbe.exe','Ribterra/MineVisualProbe.exe','Ribterra/Ribterra.exe',
    'Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll',
    'Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.4.0-smoke'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.5.0-final'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.5.0-fixed'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.5.0-smoke'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.6.0-verified'=@(
    'Ribterra/MineVisualProbe.exe','Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe',
    'Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.6.0-warning-clean'=@(
    'Ribterra/MineVisualProbe.exe','Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe',
    'Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.7.0-final'=@(
    'Ribterra/MineVisualProbe.exe','Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe',
    'Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.8.0-final'=@(
    'Ribterra/MineVisualProbe.exe','Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe',
    'Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0.9.0-final'=@(
    'Ribterra/MineVisualProbe.exe','Ribterra/ProfessionVisualProbe.exe','Ribterra/Ribterra.exe',
    'Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe','Ribterra/SDL3_ttf.dll',
    'Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0181-f7923b25'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0182-c5d49729'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0190-6409ac88'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0191-2a0b8951'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0200-e89bd797'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
  'package-0201-32b1e4af'=@(
    'Ribterra/Ribterra.exe','Ribterra/RibterraNetProbe.exe','Ribterra/RibterraServer.exe',
    'Ribterra/SDL3_ttf.dll','Ribterra/SDL3.dll','Ribterra/SimRunner.exe')
}

$expectedRootAggregates=[ordered]@{
  'package-0.10.0-final'=@{count=8;bytes=[int64]164463616}
  'package-0.4.0-smoke'=@{count=6;bytes=[int64]69753344}
  'package-0.5.0-final'=@{count=6;bytes=[int64]70246912}
  'package-0.5.0-fixed'=@{count=6;bytes=[int64]70246400}
  'package-0.5.0-smoke'=@{count=6;bytes=[int64]70246912}
  'package-0.6.0-verified'=@{count=7;bytes=[int64]116403200}
  'package-0.6.0-warning-clean'=@{count=7;bytes=[int64]116403200}
  'package-0.7.0-final'=@{count=7;bytes=[int64]116881920}
  'package-0.8.0-final'=@{count=7;bytes=[int64]117081088}
  'package-0.9.0-final'=@{count=8;bytes=[int64]163791872}
  'package-0181-f7923b25'=@{count=6;bytes=[int64]71829504}
  'package-0182-c5d49729'=@{count=6;bytes=[int64]71830528}
  'package-0190-6409ac88'=@{count=6;bytes=[int64]71841280}
  'package-0191-2a0b8951'=@{count=6;bytes=[int64]71842816}
  'package-0200-e89bd797'=@{count=6;bytes=[int64]71843840}
  'package-0201-32b1e4af'=@{count=6;bytes=[int64]71845376}
}

function Get-Sha256([string]$path) {return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}

function Get-TextSha256([string]$text) {
  $algorithm=[Security.Cryptography.SHA256]::Create()
  try {return [Convert]::ToHexString($algorithm.ComputeHash($utf8.GetBytes($text)))}
  finally {$algorithm.Dispose()}
}

function Assert-NoReparseAncestors([string]$path,[string]$boundary) {
  $current=[IO.Path]::GetFullPath($path);$stop=[IO.Path]::GetFullPath($boundary)
  while($true){
    $item=Get-Item -LiteralPath $current -Force
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse-point ancestor is forbidden: $current"}
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
  $resolved=(Resolve-Path -LiteralPath $root).Path;Assert-NoReparseAncestors $resolved $repoRoot
  $excluded=@{};foreach($value in $exclude){$excluded[$value.Replace('\','/')]=$true}
  $items=@(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
  foreach($item in $items){if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "$label contains a reparse point: $($item.FullName)"}}
  $rows=@($items|Where-Object{-not $_.PSIsContainer}|Sort-Object FullName|ForEach-Object{
    $relative=[IO.Path]::GetRelativePath($resolved,$_.FullName).Replace('\','/')
    if(!$excluded.ContainsKey($relative)){Get-FileRow $_.FullName $relative}
  })
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  return [ordered]@{label=$label;root=$resolved;file_count=$rows.Count;total_bytes=$bytes;
    inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function New-FileSetInventory([string]$label,[string[]]$paths) {
  $rows=@($paths|Sort-Object|ForEach-Object{$full=(Resolve-Path -LiteralPath $_).Path;Assert-NoReparseAncestors $full $repoRoot;Get-FileRow $full ([IO.Path]::GetRelativePath($repoRoot,$full))})
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  return [ordered]@{label=$label;root=$repoRoot;file_count=$rows.Count;total_bytes=$bytes;
    inventory_sha256=Get-TextSha256 (ConvertTo-Json @($rows) -Depth 5 -Compress);files=$rows}
}

function Test-Inventory([object]$expected,[object]$actual,[string]$phase) {
  foreach($field in @('label','root','file_count','total_bytes','inventory_sha256')){
    if([string]$expected.$field -ne [string]$actual.$field){throw "$phase inventory changed: $($expected.label) $field"}
  }
  if((ConvertTo-Json @($expected.files) -Depth 5 -Compress) -ne (ConvertTo-Json @($actual.files) -Depth 5 -Compress)){
    throw "$phase exact file inventory changed: $($expected.label)"
  }
}

function Get-LauncherBindings {
  $launcher=Join-Path $repoRoot 'Play Ribterra.cmd';$rework=Join-Path $repoRoot 'Play Ribterra Rework.cmd';$pointer=Join-Path $repoRoot 'dist/current-build.txt'
  foreach($path in @($launcher,$rework,$pointer)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing launcher evidence: $path"}}
  $rootExecutable=Join-Path $repoRoot 'Ribterra.exe'
  if(Test-Path -LiteralPath $rootExecutable -PathType Leaf){$currentTarget=$rootExecutable}else{
    $value=(Get-Content -Raw -LiteralPath $pointer).Trim();if([string]::IsNullOrWhiteSpace($value)){throw 'dist/current-build.txt is empty'}
    $currentTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $value))
  }
  $match=[regex]::Match((Get-Content -Raw -LiteralPath $rework),'(?im)^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$')
  if(!$match.Success){throw 'Could not resolve rework launcher target'}
  $reworkTarget=[IO.Path]::GetFullPath((Join-Path $repoRoot $match.Groups[1].Value))
  foreach($path in @($currentTarget,$reworkTarget)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing current launcher target: $path"}}
  return [ordered]@{files=@($launcher,$rework,$pointer,$currentTarget,$reworkTarget);target_roots=@((Split-Path $currentTarget),(Split-Path $reworkTarget)|Select-Object -Unique)}
}

function Get-ProtectedInventories {
  $roots=[ordered]@{
    legacy_latest='artifacts/launch-fix-0.10.2'
    legacy_rollback='artifacts/package-0.10.1-final'
    environment_build02='build/gameview-environment-2026-10-08-attempt02'
    environment_native01='build/gameview-environment-native-2026-10-08-attempt01'
    environment_native02='build/gameview-environment-native-2026-10-08-attempt02'
    atmosphere_build03='build/gameview-atmosphere-2026-10-08-attempt03'
    atmosphere_native02='build/gameview-atmosphere-native-2026-10-08-attempt02'
    atmosphere_native03='build/gameview-atmosphere-native-2026-10-08-attempt03'
    water_build02_rollback='build/gameview-water-2026-10-08-attempt02'
    control_kit='build/engine-control-kit-2026-10-08-check05/export-a'
  }
  $result=@();foreach($entry in $roots.GetEnumerator()){$path=Join-Path $repoRoot $entry.Value;if(!(Test-Path -LiteralPath $path -PathType Container)){throw "Missing protected root: $path"};$result+=New-Inventory $entry.Key $path}
  $launch=Get-LauncherBindings
  $result+=New-FileSetInventory 'current_launchers_pointer_and_executables' $launch.files
  $index=0;foreach($targetRoot in $launch.target_roots){$result+=New-Inventory "current_launcher_package_$index" $targetRoot;$index++}
  return @($result)
}

function Get-TargetRows {
  $rows=@()
  foreach($entry in $targetMap.GetEnumerator()){
    $rootName=$entry.Key;$root=(Resolve-Path (Join-Path $artifactsRoot $rootName)).Path;Assert-NoReparseAncestors $root $repoRoot
    $actualCompiled=@(Get-ChildItem -LiteralPath $root -File -Recurse|Where-Object{$compiledExtensions -contains $_.Extension.ToLowerInvariant()}|ForEach-Object{[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/')}|Sort-Object)
    $expectedCompiled=@($entry.Value|Sort-Object)
    if(Compare-Object $expectedCompiled $actualCompiled){throw "Unexpected compiled leaf in fixed legacy root: $rootName"}
    [int64]$rootBytes=0
    foreach($relative in $entry.Value){
      $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
      if(!$path.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw "Target escaped fixed package root: $rootName/$relative"}
      Assert-NoReparseAncestors $path $repoRoot;$base=Get-FileRow $path $relative;$rootBytes+=[int64]$base.bytes
      $rows+=[ordered]@{package_root=$rootName;relative_path=$base.relative_path;path=$base.path;bytes=$base.bytes;sha256=$base.sha256}
    }
    $aggregate=$expectedRootAggregates[$rootName]
    if($entry.Value.Count-ne[int]$aggregate.count-or$rootBytes-ne[int64]$aggregate.bytes){throw "Fixed root aggregate mismatch: $rootName"}
  }
  [int64]$bytes=0;foreach($row in $rows){$bytes+=[int64]$row.bytes}
  if($rows.Count-ne$expectedTargetCount-or$bytes-ne$expectedTargetBytes){throw "Legacy target aggregate mismatch: count=$($rows.Count), bytes=$bytes"}
  return @($rows)
}

function Get-RetainedPackageInventories {
  $result=@();foreach($entry in $targetMap.GetEnumerator()){$root=Join-Path $artifactsRoot $entry.Key;$result+=New-Inventory "retained_$($entry.Key)" $root $entry.Value};return @($result)
}

function Assert-DisjointTargets([object[]]$targets,[object[]]$protectedInventories) {
  $lookup=@{};foreach($inventory in $protectedInventories){foreach($file in @($inventory.files)){$lookup[[IO.Path]::GetFullPath([string]$file.path).ToLowerInvariant()]=$inventory.label}}
  foreach($target in $targets){$full=[IO.Path]::GetFullPath([string]$target.path).ToLowerInvariant();if($lookup.ContainsKey($full)){throw "Prune target overlaps protected inventory $($lookup[$full]): $($target.path)"}}
}

function Assert-NoLiveReferences([object[]]$targets) {
  $lookup=@{};foreach($row in $targets){$lookup[[IO.Path]::GetFullPath($row.path).ToLowerInvariant()]=$true}
  $hits=[Collections.Generic.List[string]]::new();$errors=[Collections.Generic.List[string]]::new()
  foreach($process in Get-Process){
    try {if($process.Path-and$lookup.ContainsKey([IO.Path]::GetFullPath($process.Path).ToLowerInvariant())){$hits.Add("image pid=$($process.Id) $($process.Path)")}}catch{$errors.Add("image pid=$($process.Id)")}
    try {foreach($module in $process.Modules){if($lookup.ContainsKey([IO.Path]::GetFullPath($module.FileName).ToLowerInvariant())){$hits.Add("module pid=$($process.Id) $($module.FileName)")}}}catch{$errors.Add("modules pid=$($process.Id)")}
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
  if($ExpectedPlanSha256-or$ExpectedHelperSha256-or$ResultPath){throw 'Expected hashes and ResultPath are apply-only parameters'}
  $planOutput=Assert-NewOutput $PlanPath 'PlanPath';$targets=Get-TargetRows;$protected=Get-ProtectedInventories;Assert-DisjointTargets $targets $protected
  $plan=[ordered]@{
    schema=1;classification='dry-run exact compiled-leaf prune plan for 16 superseded legacy packages; no file was deleted or moved'
    created_utc=[DateTimeOffset]::UtcNow.ToString('o');repository=$repoRoot;artifacts_root=$artifactsRoot;helper=$helperRow
    target_count=$targets.Count;target_bytes=$expectedTargetBytes;target_inventory_sha256=Get-TextSha256 (ConvertTo-Json @($targets) -Depth 5 -Compress);targets=$targets
    retained_package_roots=Get-RetainedPackageInventories;protected_inventories=$protected
    apply_requirements=@('Apply switch','ExpectedPlanSha256','ExpectedHelperSha256','fresh ResultPath')
    evidence_caveat='Removal permanently loses these byte-exact historical executables and DLL copies. Source may rebuild functionality, but byte-identical recovery is not guaranteed.'
  }
  Write-NewJson $planOutput $plan 12;Write-Output "Dry-run plan written: $planOutput";Write-Output "Plan SHA256: $(Get-Sha256 $planOutput)";Write-Output "Frozen helper SHA256: $($helperRow.sha256)";exit 0
}

if([string]::IsNullOrWhiteSpace($ExpectedPlanSha256)-or$ExpectedPlanSha256-notmatch'^[0-9A-Fa-f]{64}$'){throw 'Apply requires a 64-hex ExpectedPlanSha256'}
if([string]::IsNullOrWhiteSpace($ExpectedHelperSha256)-or$ExpectedHelperSha256-notmatch'^[0-9A-Fa-f]{64}$'){throw 'Apply requires a 64-hex ExpectedHelperSha256'}
$planInput=(Resolve-Path -LiteralPath $PlanPath).Path
if(-not[StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $planInput),$buildRoot)){throw 'Apply PlanPath must be one existing file directly inside the repository build directory'}
Assert-NoReparseAncestors $planInput $repoRoot;(Get-FileRow $planInput ([IO.Path]::GetRelativePath($repoRoot,$planInput)))|Out-Null
$resultOutput=Assert-NewOutput $ResultPath 'ResultPath'
if((Get-Sha256 $planInput)-ne$ExpectedPlanSha256.ToUpperInvariant()){throw 'Plan SHA256 mismatch'}
if($helperRow.sha256-ne$ExpectedHelperSha256.ToUpperInvariant()){throw 'Frozen helper SHA256 mismatch'}
$plan=Get-Content -Raw -LiteralPath $planInput|ConvertFrom-Json
if([int]$plan.schema-ne1-or$plan.classification-notlike'dry-run exact compiled-leaf prune plan for 16 superseded legacy packages*'){throw 'Unsupported legacy package prune plan'}
if(-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.repository,$repoRoot)-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.artifacts_root,$artifactsRoot)){throw 'Plan repository identity mismatch'}
if(-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$plan.helper.path,$helperRow.path)-or([string]$plan.helper.sha256).ToUpperInvariant()-ne$helperRow.sha256-or[int64]$plan.helper.bytes-ne[int64]$helperRow.bytes){throw 'Plan helper binding mismatch'}
$expectedKeys=@($targetMap.GetEnumerator()|ForEach-Object{$root=$_.Key;$_.Value|ForEach-Object{"$root|$_"}}|Sort-Object)
$planKeys=@($plan.targets|ForEach-Object{"$($_.package_root)|$($_.relative_path)"}|Sort-Object)
if($planKeys.Count-ne$expectedTargetCount-or(Compare-Object $expectedKeys $planKeys)){throw 'Plan target allowlist mismatch'}

$targetsBefore=Get-TargetRows
if([int]$plan.target_count-ne$expectedTargetCount-or[int64]$plan.target_bytes-ne$expectedTargetBytes-or([string]$plan.target_inventory_sha256).ToUpperInvariant()-ne(Get-TextSha256 (ConvertTo-Json @($plan.targets) -Depth 5 -Compress))){throw 'Plan target aggregate binding mismatch'}
if((ConvertTo-Json @($targetsBefore) -Depth 5 -Compress)-ne(ConvertTo-Json @($plan.targets) -Depth 5 -Compress)){throw 'Targets changed since planning'}
$retainedBefore=Get-RetainedPackageInventories
if($retainedBefore.Count-ne@($plan.retained_package_roots).Count){throw 'Retained package inventory count changed'}
for($index=0;$index-lt$retainedBefore.Count;$index++){Test-Inventory $plan.retained_package_roots[$index] $retainedBefore[$index] 'pre-apply'}
$protectedBefore=Get-ProtectedInventories
if($protectedBefore.Count-ne@($plan.protected_inventories).Count){throw 'Protected inventory count changed'}
for($index=0;$index-lt$protectedBefore.Count;$index++){Test-Inventory $plan.protected_inventories[$index] $protectedBefore[$index] 'pre-apply'}
Assert-DisjointTargets $targetsBefore $protectedBefore;Assert-NoLiveReferences $targetsBefore

$applied=[Collections.Generic.List[object]]::new();$failure=$null
try {
  foreach($row in $targetsBefore){
    if(!$targetMap.Contains([string]$row.package_root)-or-not($targetMap[[string]$row.package_root]-contains[string]$row.relative_path)){throw "Immediate fixed allowlist mismatch: $($row.package_root)/$($row.relative_path)"}
    $root=(Resolve-Path (Join-Path $artifactsRoot ([string]$row.package_root))).Path;$expectedPath=[IO.Path]::GetFullPath((Join-Path $root ([string]$row.relative_path)))
    if(!$expectedPath.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$row.path,$expectedPath)){throw "Immediate target path mismatch: $($row.package_root)/$($row.relative_path)"}
    Assert-NoReparseAncestors $expectedPath $repoRoot;$resolvedPath=(Resolve-Path -LiteralPath $expectedPath).Path
    if(-not[StringComparer]::OrdinalIgnoreCase.Equals($resolvedPath,$expectedPath)){throw "Immediate resolved path mismatch: $expectedPath"}
    $base=Get-FileRow $resolvedPath ([string]$row.relative_path)
    if([int64]$base.bytes-ne[int64]$row.bytes-or-not[StringComparer]::OrdinalIgnoreCase.Equals([string]$base.sha256,[string]$row.sha256)){throw "Immediate bytes or SHA256 changed: $expectedPath"}
    Remove-Item -LiteralPath $resolvedPath
    if(Test-Path -LiteralPath $resolvedPath){throw "Target remained after leaf deletion: $expectedPath"}
    $applied.Add($row)
  }
}catch{$failure=$_.Exception.Message}

[object[]]$remaining=@($plan.targets|Where-Object{Test-Path -LiteralPath $_.path});$retainedAfter=@();$protectedAfter=@();$verificationErrors=[Collections.Generic.List[string]]::new()
try {$retainedAfter=Get-RetainedPackageInventories;if($retainedAfter.Count-ne@($plan.retained_package_roots).Count){throw 'Retained package inventory count changed post-apply'};for($index=0;$index-lt$retainedAfter.Count;$index++){Test-Inventory $plan.retained_package_roots[$index] $retainedAfter[$index] 'post-apply'}}catch{$verificationErrors.Add($_.Exception.Message)}
try {$protectedAfter=Get-ProtectedInventories;if($protectedAfter.Count-ne@($plan.protected_inventories).Count){throw 'Protected inventory count changed post-apply'};for($index=0;$index-lt$protectedAfter.Count;$index++){Test-Inventory $plan.protected_inventories[$index] $protectedAfter[$index] 'post-apply'}}catch{$verificationErrors.Add($_.Exception.Message)}
if($remaining.Count){$verificationErrors.Add("Targets retained after apply attempt: $($remaining.path -join ', ')")};if($failure){$verificationErrors.Add("Leaf deletion failed: $failure")}
$success=($applied.Count-eq$expectedTargetCount-and$verificationErrors.Count-eq0);[int64]$appliedBytes=0;foreach($row in $applied){$appliedBytes+=[int64]$row.bytes}
$result=[ordered]@{
  schema=1;classification='bounded legacy package compiled-leaf prune result; package art, saves and reports retained';applied_utc=[DateTimeOffset]::UtcNow.ToString('o');success=$success
  repository=$repoRoot;artifacts_root=$artifactsRoot;plan=[ordered]@{path=$planInput;bytes=[int64](Get-Item $planInput).Length;sha256=Get-Sha256 $planInput};helper=$helperRow
  applied_target_count=$applied.Count;applied_target_bytes=$appliedBytes;applied_targets=$applied.ToArray();remaining_targets=@($remaining)
  retained_package_roots=$retainedAfter;protected_inventories=$protectedAfter;verification_errors=$verificationErrors.ToArray()
  evidence_caveat='Removed historical executables and DLLs are not byte-exactly recoverable from the retained source; recompilation may only recover functionality.'
}
Write-NewJson $resultOutput $result 12
if(!$success){throw "Bounded legacy package prune did not complete cleanly; retained result: $resultOutput"}
Write-Output "Bounded legacy package compiled-leaf prune completed; result: $resultOutput"
