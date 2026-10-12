param(
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [Parameter(Mandatory=$true)][string]$ObjectRoot,
  [Parameter(Mandatory=$true)][string]$KitRoot,
  [Parameter(Mandatory=$true)][string]$UiKitRoot,
  [Parameter(Mandatory=$true)][string]$MaterialRoot,
  [Parameter(Mandatory=$true)][string]$PaintedRoot,
  [Parameter(Mandatory=$true)][string]$NeutralRoot,
  [Parameter(Mandatory=$true)][string]$CpuProofReport,
  [string]$ProbeSource='tools/gameview_textured_material_probe.cpp')

$ErrorActionPreference='Stop'
$commandRows=[Collections.Generic.List[object]]::new()
$bindingPhase=$null
$maximumPhaseLocks=8192
if($PSVersionTable.PSVersion.Major -lt 7){
  throw 'PowerShell 7 or newer is required; invoke this helper with pwsh, not Windows PowerShell.'
}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
# No canonicalization may erase an incoming traversed junction component.
foreach($incoming in @($OutputRoot,$ObjectRoot,$KitRoot,$UiKitRoot,$MaterialRoot,$NeutralRoot,$PaintedRoot,$CpuProofReport)){
  if(![IO.Path]::IsPathFullyQualified($incoming) -or $incoming -cnotmatch '^[A-Za-z]:\\' -or
     $incoming.Substring(3).Contains(':')){throw 'Inputs require canonical absolute local paths'}
  foreach($component in $incoming.Substring(3).Split([char[]]@('\','/'))){
    if($component -in @('','.','..') -or $component.EndsWith('.') -or $component.EndsWith(' ') -or
       $component -match '[\x00-\x1f<>"|?*]' -or
       $component.Split('.')[0] -match '^(CON|PRN|AUX|NUL|COM[0-9\u00b9\u00b2\u00b3]|LPT[0-9\u00b9\u00b2\u00b3])$'){
      throw 'Inputs contain an unsafe or aliased path component'
    }
  }
  $cursor=$incoming
  while($cursor){if(Test-Path -LiteralPath $cursor){if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Raw incoming ancestor is a reparse path'}}
    $parent=Split-Path $cursor;if(!$parent -or $parent -eq $cursor){break};$cursor=$parent}
}
$outPath=[IO.Path]::GetFullPath($OutputRoot)
$objectPath=[IO.Path]::GetFullPath($ObjectRoot)
$kitPath=[IO.Path]::GetFullPath($KitRoot)
$uiKitPath=[IO.Path]::GetFullPath($UiKitRoot)
$materialPath=[IO.Path]::GetFullPath($MaterialRoot)
$paintedPath=[IO.Path]::GetFullPath($PaintedRoot)
$neutralPath=[IO.Path]::GetFullPath($NeutralRoot)
$cpuProofPath=[IO.Path]::GetFullPath($CpuProofReport)
$originalArtPath=Join-Path $repoRoot 'assets'
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $outPath),$buildRoot)){
  throw 'OutputRoot must be one new immediate child of the authoritative repository build directory'
}
if(Test-Path -LiteralPath $outPath){throw 'Refusing to reuse an existing output root'}
if(-not [IO.Path]::IsPathFullyQualified($ObjectRoot) -or
   -not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $objectPath),$buildRoot)){
  throw 'ObjectRoot must be one new absolute immediate repository build child'
}
if(Test-Path -LiteralPath $objectPath){throw 'Refusing to reuse an existing object root'}
foreach($root in @($objectPath,$outPath)){
  if((Split-Path $root -Leaf) -notmatch '^[a-z][a-z0-9-]{0,127}$'){
    throw 'Fresh artifact root names must be portable lowercase identifiers'
  }
}
if(!(Test-Path -LiteralPath $kitPath -PathType Container)){throw "Missing separate generated engine control kit: $kitPath"}
if($kitPath.StartsWith(($outPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
   $outPath.StartsWith(($kitPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
   [StringComparer]::OrdinalIgnoreCase.Equals($kitPath,$outPath)){
  throw 'KitRoot must be separate from OutputRoot; this helper points to the kit and does not bulk-stage it'
}

# Both referenced kits are confined, distinct descendants; no reparse traversal.
foreach($root in @($buildRoot,$kitPath,$uiKitPath,$materialPath,$paintedPath,$neutralPath)){
  if(!(Test-Path -LiteralPath $root -PathType Container)){throw "Missing kit/build root: $root"}
  if($root -ne $buildRoot -and !$root.StartsWith(($buildRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Both kits must remain under the authoritative build root'
  }
  $cursor=$root
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse path forbidden: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
}
foreach($pair in @(@($outPath,$uiKitPath),@($kitPath,$uiKitPath),@($outPath,$materialPath),@($kitPath,$materialPath),@($uiKitPath,$materialPath))){
  $a=$pair[0];$b=$pair[1]
  if([StringComparer]::OrdinalIgnoreCase.Equals($a,$b) -or
     $a.StartsWith(($b+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $b.StartsWith(($a+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'OutputRoot and both kits must be distinct non-overlapping paths'
  }
}
foreach($root in @($outPath,$kitPath,$uiKitPath,$materialPath,$paintedPath,$neutralPath)){
  if([StringComparer]::OrdinalIgnoreCase.Equals($objectPath,$root) -or
     $objectPath.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $root.StartsWith(($objectPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'ObjectRoot must be distinct and non-overlapping with all other artifact/input roots'
  }
}
foreach($root in @($outPath,$kitPath,$uiKitPath,$materialPath)){
  if([StringComparer]::OrdinalIgnoreCase.Equals($paintedPath,$root) -or
     $paintedPath.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $root.StartsWith(($paintedPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'PaintedRoot must be distinct and non-overlapping with other artifact/input roots'
  }
}
foreach($root in @($outPath,$kitPath,$uiKitPath,$materialPath,$paintedPath)){
  if([StringComparer]::OrdinalIgnoreCase.Equals($neutralPath,$root) -or
     $neutralPath.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $root.StartsWith(($neutralPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'NeutralRoot must be distinct and non-overlapping with other artifact/input roots'
  }
}

function Test-Headroom([string]$phase) {
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below 384MiB during ${phase}: $free"}
  foreach($artifactRoot in @($outPath,$objectPath)){
    if(!(Test-Path -LiteralPath $artifactRoot)){continue}
    $measurement=Get-ChildItem -LiteralPath $artifactRoot -File -Recurse | Measure-Object Length -Sum
    $used=if($null -eq $measurement.Sum){[int64]0}else{[int64]$measurement.Sum}
    if($used -gt 134217728){throw "Artifact root exceeds 128MiB during ${phase}: $artifactRoot ($used bytes)"}
  }
}

function Assert-NewPath([string]$path) {
  if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing path: $path"}
}

function Write-NewJson([string]$path,[object]$value,[int]$depth=10) {
  Assert-NewPath $path
  [IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth $depth -Compress))
}

function Invoke-Checked([string]$exe,[object[]]$arguments,[string]$log) {
  if($null -ne $script:bindingPhase){throw 'A binding phase must release every read lock before a subprocess.'}
  Test-Headroom "before $log"
  $logPath=Join-Path $outPath $log
  Assert-NewPath $logPath
  $savedErrorActionPreference=$ErrorActionPreference
  $exitCode=-1
  try {
    $ErrorActionPreference='Continue'
    $global:LASTEXITCODE=$null
    & $exe @arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
    if($null -eq $global:LASTEXITCODE){throw 'Native exit was unavailable; no successful compile is inferred.'}
    $exitCode=[int]$global:LASTEXITCODE
  } finally {
    $ErrorActionPreference=$savedErrorActionPreference
  }
  if(!(Test-Path -LiteralPath $logPath)){[IO.File]::WriteAllText($logPath,'')}
  $exitPath=$logPath+'.exit.txt'
  Assert-NewPath $exitPath
  [IO.File]::WriteAllText($exitPath,"$exitCode`n")
  $commandRows.Add([ordered]@{executable=(Get-Command $exe -ErrorAction Stop).Source;
    arguments=@($arguments);working_directory=(Get-Location).Path;exit_code=$exitCode;
    log=Get-HashRow $logPath;exit_sidecar=Get-HashRow $exitPath})
  if($exitCode -ne 0){throw "$exe failed($exitCode), see $log"}
  Test-Headroom "after $log"
}

function Assert-FileAncestors([string]$path) {
  $cursor=[IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($path))
  while($true){
    $attributes=[IO.File]::GetAttributes($cursor)
    if(($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse path forbidden: $cursor"}
    $parent=[IO.Path]::GetDirectoryName($cursor)
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
}
function Start-BindingPhase([string]$name) {
  if($null -ne $script:bindingPhase){throw 'Binding phases cannot overlap.'}
  $script:bindingPhase=[pscustomobject]@{name=$name;files=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)}
}
function Close-BindingPhase {
  if($null -eq $script:bindingPhase){return}
  $phase=$script:bindingPhase;$script:bindingPhase=$null
  $closeErrors=[Collections.Generic.List[string]]::new()
  foreach($entry in $phase.files.Values){try{$entry.stream.Dispose()}catch{$closeErrors.Add($_.Exception.Message)}}
  $phase.files.Clear()
  if($closeErrors.Count){throw ('Read-lock cleanup failed: '+($closeErrors -join ' ; '))}
}
function Get-HashRow([string]$path,[string]$role='bound-immutable-file') {
  # Deduplicate one observation only while FileShare.Read excludes writers and
  # deletion/replacement. There is no timestamp-only or cross-process cache.
  Assert-FileAncestors $path
  $item=Get-Item -LiteralPath $path -Force -ErrorAction Stop
  if($item -isnot [IO.FileInfo]){throw 'A file binding must identify a regular file.'}
  $canonical=[IO.Path]::GetFullPath($item.FullName)
  $phase=$script:bindingPhase
  if($null -ne $phase -and $phase.files.ContainsKey($canonical)){
    $entry=$phase.files[$canonical]
    if($entry.role -cne $role -or $entry.stream.SafeFileHandle.IsClosed -or
        [int64]$item.Length -ne $entry.row.bytes -or $entry.stream.Length -ne $entry.row.bytes){
      throw 'Locked binding identity/role/extent changed within its observation phase.'
    }
    return $entry.row
  }
  if($null -ne $phase -and $phase.files.Count -ge $script:maximumPhaseLocks){throw 'Binding phase exceeds8192 retained read locks.'}
  $stream=[IO.FileStream]::new($canonical,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read,1,[IO.FileOptions]::SequentialScan)
  $retained=$false
  try{
    Assert-FileAncestors $canonical
    if($stream.Length -ne [int64]$item.Length){throw 'File grew/replaced before the phase read lock.'}
    $digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
    $row=[pscustomobject][ordered]@{path=$canonical;bytes=[int64]$stream.Length;sha256=$digest}
    if($null -ne $phase){$phase.files.Add($canonical,[pscustomobject]@{role=$role;stream=$stream;row=$row});$retained=$true}
    return $row
  }finally{if(!$retained){$stream.Dispose()}}
}

function Resolve-SystemLibrary([string]$name) {
  foreach($directory in ($env:LIB -split ';')){
    if([string]::IsNullOrWhiteSpace($directory)){continue}
    $candidate=Join-Path $directory $name
    if(Test-Path -LiteralPath $candidate -PathType Leaf){return (Resolve-Path $candidate).Path}
  }
  throw "Could not resolve toolchain library: $name"
}

function Get-FileTreeRows([string]$root) {
  Assert-FileAncestors $root
  $rows=[Collections.Generic.List[object]]::new();$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
  while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Unexpected tree reparse entry'}
    if($item.PSIsContainer){$pending.Push($item.FullName);continue}
    $row=Get-HashRow $item.FullName
    $rows.Add([pscustomobject][ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$row.path).Replace('\','/');path=$row.path;bytes=$row.bytes;sha256=$row.sha256})
  }}
  return @($rows | Sort-Object path)
}

function Resolve-InputChild([string]$root,[string]$relative) {
  # Reject aliases before GetFullPath can erase a component of the input.
  if(!$relative -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains('\') -or $relative.Contains(':')){
    throw 'Input child must have a canonical relative path'
  }
  foreach($component in $relative.Split('/')){
    if($component -in @('','.','..') -or $component.EndsWith('.') -or $component.EndsWith(' ') -or
       $component -match '[\x00-\x1f<>"|?*]' -or
       $component.Split('.')[0] -match '^(CON|PRN|AUX|NUL|COM[0-9\u00b9\u00b2\u00b3]|LPT[0-9\u00b9\u00b2\u00b3])$'){
      throw 'Input child contains an unsafe or aliased component'
    }
  }
  $path=Join-Path $root $relative
  Assert-FileAncestors $path
  return [IO.Path]::GetFullPath($path)
}

function Get-RowByteTotal([object[]]$rows,[string]$property='bytes') {
  [int64]$total=0
  foreach($row in $rows){
    $value=if($row -is [Collections.IDictionary]){$row[$property]}else{$row.PSObject.Properties[$property].Value}
    if(($value -isnot [int64] -and $value -isnot [int32]) -or $value -lt 0){
      throw "Invalid nonnegative byte value: $property"
    }
    [int64]$bytes=$value
    if($bytes -gt [int64]::MaxValue-$total){throw 'Byte total exceeds Int64'}
    $total+=$bytes
  }
  return $total
}

function Get-PrerequisiteRows([string]$cpuPath,[string]$selectedMaterial,[string]$selectedNeutral) {
  # Historical sources map through exact preserved/compiled epochs. AssetCatalog
  # additionally binds its failed-build preimage to the reviewed compiler repair.
  $state=Join-Path $repoRoot '.unlazy/engine-completion-2026-10-08'
  $pins=[ordered]@{
    'textured-material-cpu-accepted-2026-10-09.json'='F6CEE16A21AA2F6A0087E65B8EF342CA170DBF5E4C3A18C95270865E708616CB'
    'house-texture-source-CPU-accepted-2026-10-09.json'='AE44394E7C75D2B38730A5204D4921E93498E35FAF6B207F746585529D1DBBB6'
    'neutral-main-accepted-2026-10-10.json'='F2A13C5A5B853E6FD0219F5924EDFA9D92F3286DA169379CAF9D66DB1BFEA5A1'
    'textured-runtime-adoption-2026-10-10/receipt.json'='27FBAC0AB23605344D89346A7CF032DA6984DEBE39FCB5A240D64342459139D1'
    'textured-runtime-compile-repair-2026-10-10/receipt.json'='2788220B642C03F631C4A1152FD77CECECEA3D28B1D1CC36E4B135ADBCE3A32D'
  }
  $files=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  $documents=[ordered]@{}
  foreach($relative in $pins.Keys){
    $path=Join-Path $state $relative;$row=Get-HashRow $path
    if($row.sha256 -cne $pins[$relative]){throw 'Accepted prerequisite receipt identity differs'}
    $files[$row.path]=$row;$documents[$relative]=Get-Content -Raw -LiteralPath $path | ConvertFrom-Json -Depth 100
  }
  $cpu=$documents['textured-material-cpu-accepted-2026-10-09.json'];$house=$documents['house-texture-source-CPU-accepted-2026-10-09.json']
  $main=$documents['neutral-main-accepted-2026-10-10.json'];$adoption=$documents['textured-runtime-adoption-2026-10-10/receipt.json']
  $repair=$documents['textured-runtime-compile-repair-2026-10-10/receipt.json']
  if(!$cpu.accepted_CPU_only -or $cpu.parent_exit -ne 0 -or $cpu.independent_exit -ne 0 -or
     !$house.accepted_source_CPU_only -or $house.independent_actual_review.exit -ne 0 -or !$main.accepted -or
     $main.actual_processes -ne 10 -or $main.captures -ne 12 -or @($adoption.applied).Count -ne 4){throw 'Prerequisites lack actual paired acceptance'}
  if($repair.schema -ne 1 -or $repair.kind -cne 'ribterra-textured-runtime-compile-repair-v1' -or
     $repair.accepted_source_only -ne $true -or $repair.compile_accepted -ne $false -or
     $repair.native_accepted -ne $false -or $repair.automatic_gate_certification -ne $false -or
     @($repair.applied).Count -ne 1 -or $repair.applied[0].relative_path -cne 'src/assets/asset_catalog.cpp' -or
     @($repair.unchanged_runtime).Count -ne 3){throw 'Compiler repair must be the exact source-only AssetCatalog epoch'}
  $repairEntry=$repair.applied[0]
  foreach($binding in @($repair.original_adoption,$repair.failed_build,$repairEntry.before,$repairEntry.failed_source_snapshot,$repairEntry.candidate,$repairEntry.after)+@($repair.unchanged_runtime)){
    $actual=Get-HashRow $binding.path
    if($actual.bytes -ne $binding.bytes -or $actual.sha256 -ine $binding.sha256){throw 'Compiler repair binding changed'}
    $files[$actual.path]=$actual
  }
  $maps=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
  $mapping=[Collections.Generic.List[object]]::new()
  $repairMapping=[Collections.Generic.List[object]]::new()
  foreach($entry in $adoption.applied){
    $relative=[string]$entry.relative_path
    if($relative -notin @('include/ribterra/asset_catalog.hpp','src/assets/asset_catalog.cpp','src/client/view.cpp','src/client/main.cpp')){throw 'Unapproved historical source mapping'}
    $snapshotRoot=if($relative -eq 'src/client/main.cpp'){'build/gameview-neutral-material-main-2026-10-09-attempt02/source'}else{'build/gameview-neutral-material-2026-10-09-attempt06/source'}
    $snapshot=Get-HashRow (Join-Path (Join-Path $repoRoot $snapshotRoot) $relative)
    $archive=Get-HashRow $entry.before.path;$current=Get-HashRow (Join-Path $repoRoot $relative)
    if($archive.bytes -ne $entry.before.bytes -or $archive.sha256 -ne $entry.before.sha256 -or
       $snapshot.bytes -ne $archive.bytes -or $snapshot.sha256 -ne $archive.sha256){throw 'Historical preimage/snapshot mapping differs'}
    $intermediate=$null;$failedSnapshot=$null
    if($relative -ceq $repairEntry.relative_path){
      $intermediate=Get-HashRow $repairEntry.before.path;$failedSnapshot=Get-HashRow $repairEntry.failed_source_snapshot.path
      $candidate=Get-HashRow $repairEntry.candidate.path
      if(![StringComparer]::OrdinalIgnoreCase.Equals($current.path,[string]$repairEntry.historical_origin) -or
         ![StringComparer]::OrdinalIgnoreCase.Equals($current.path,[string]$repairEntry.after.path) -or
         $intermediate.bytes -ne $entry.after.bytes -or $intermediate.sha256 -ine $entry.after.sha256 -or
         $failedSnapshot.bytes -ne $intermediate.bytes -or $failedSnapshot.sha256 -cne $intermediate.sha256 -or
         $current.bytes -ne $candidate.bytes -or $current.sha256 -cne $candidate.sha256){throw 'Exact compiler repair epoch chain differs'}
      $repairMapping.Add([ordered]@{relative_path=$relative;historical_origin=$current.path;before=$intermediate;
        failed_source_snapshot=$failedSnapshot;candidate=$candidate;current_after=$current})
    }else{
      $unchanged=@($repair.unchanged_runtime | Where-Object {[StringComparer]::OrdinalIgnoreCase.Equals([string]$_.path,$current.path)})
      if($unchanged.Count -ne 1 -or $current.bytes -ne $entry.after.bytes -or $current.sha256 -ine $entry.after.sha256 -or
         $unchanged[0].bytes -ne $current.bytes -or $unchanged[0].sha256 -ine $current.sha256){throw 'Unchanged historical/current source mapping differs'}
    }
    $maps.Add($current.path,[pscustomobject]@{old_bytes=$archive.bytes;old_sha=$archive.sha256;archive=$archive;snapshot=$snapshot;
      current=$current;intermediate=$intermediate;failed_snapshot=$failedSnapshot})
    $mapping.Add([ordered]@{relative_path=$relative;historical_origin=$current.path;expected_old_bytes=$archive.bytes;
      expected_old_sha256=$archive.sha256;archived_preimage=$archive;compiled_snapshot=$snapshot;current_after=$current})
    foreach($row in @($archive,$snapshot,$current)){$files[$row.path]=$row}
  }
  $observe={param($record)
    $path=[string]$record.path
    if(!$path -or ![IO.Path]::IsPathFullyQualified($path) -or $null -eq $record.bytes -or [string]$record.sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw 'Invalid prerequisite absolute binding'}
    if($maps.ContainsKey($path) -and $record.sha256 -ieq $maps[$path].old_sha -and $record.bytes -eq $maps[$path].old_bytes){
      foreach($row in @($maps[$path].archive,$maps[$path].snapshot,$maps[$path].current)){$files[$row.path]=$row};return
    }
    if($maps.ContainsKey($path) -and $null -ne $maps[$path].intermediate -and
       $record.sha256 -ieq $maps[$path].intermediate.sha256 -and $record.bytes -eq $maps[$path].intermediate.bytes){
      foreach($row in @($maps[$path].intermediate,$maps[$path].failed_snapshot,$maps[$path].current)){$files[$row.path]=$row};return
    }
    $actual=Get-HashRow $path
    if($actual.bytes -ne $record.bytes -or $actual.sha256 -ine $record.sha256){throw "Accepted prerequisite changed: $path"}
    if($files.ContainsKey($actual.path) -and ($files[$actual.path].bytes -ne $actual.bytes -or $files[$actual.path].sha256 -ne $actual.sha256)){throw 'Conflicting prerequisite observation'}
    $files[$actual.path]=$actual
  }
  $walk=$null;$walk={param($value)
    if($null -eq $value){return}
    if($value -is [pscustomobject]){
      $names=@($value.psobject.Properties.Name)
      if($names -contains 'path' -and $names -contains 'bytes' -and $names -contains 'sha256' -and [IO.Path]::IsPathFullyQualified([string]$value.path)){& $observe $value}
      foreach($property in $value.psobject.Properties){& $walk $property.Value}
    }elseif($value -is [Collections.IEnumerable] -and $value -isnot [string]){foreach($child in $value){& $walk $child}}
  }
  foreach($document in $documents.Values){& $walk $document}
  $selected=@($cpu.parent_report,$cpu.independent_receipt,$house.parent_review,$house.independent_actual_review,$main.parent,$main.independent)
  $loaded=[Collections.Generic.List[object]]::new()
  foreach($record in $selected){& $observe $record;$loaded.Add((Get-Content -Raw -LiteralPath $record.path | ConvertFrom-Json -Depth 100))}
  foreach($document in $loaded){& $walk $document}
  if([IO.Path]::GetFullPath($cpuPath) -cne [IO.Path]::GetFullPath($cpu.parent_report.path) -or
     (Get-HashRow $cpuPath).bytes -gt 4194304){throw 'CPU proof must be the exact accepted report within its4MiB evidence role'}
  $cpuReport=$loaded[0];$cpuIndependent=$loaded[1];$houseIndependent=$loaded[3];$mainIndependent=$loaded[5]
  if(!$cpuReport.complete -or $cpuReport.kind -ne 'ribterra-textured-material-independent-cpu-v1' -or
     @($cpuReport.commands).Count -ne 92 -or !$cpuReport.measured.full_lifetime_counter_complete -or
     !$cpuIndependent.accepted -or !$houseIndependent.accepted -or !$mainIndependent.complete -or $mainIndependent.exit_code -ne 0){throw 'Referenced actual prerequisite proof is incomplete'}
  # The CPU UV oracle applies to these exact independently reviewed rasters.
  # A different self-consistent textured export must not inherit that proof.
  $acceptedTextured=@($houseIndependent.retained_roots | Where-Object {$_.files -eq 23})
  $selectedTextured=@($acceptedTextured | Where-Object {
    [StringComparer]::OrdinalIgnoreCase.Equals([string]$_.root,$selectedMaterial)})
  $selectedExport=@($houseIndependent.CPU_exports | Where-Object {
    [StringComparer]::OrdinalIgnoreCase.Equals([string]$_.root,$selectedMaterial)})
  $acceptedNeutral=Join-Path (Join-Path $repoRoot 'build') 'neutral-material-export-tests-2026-10-09-attempt01-export-a'
  $selectedFlat=@($houseIndependent.retained_roots | Where-Object {
    $_.files -eq 20 -and [StringComparer]::OrdinalIgnoreCase.Equals([string]$_.root,$selectedNeutral)})
  if($acceptedTextured.Count -ne 2 -or @($houseIndependent.CPU_exports).Count -ne 2 -or
     $selectedTextured.Count -ne 1 -or $selectedExport.Count -ne 1 -or
     $selectedExport[0].bytes -ne $selectedTextured[0].bytes -or
     $selectedFlat.Count -ne 1 -or ![StringComparer]::OrdinalIgnoreCase.Equals($selectedNeutral,$acceptedNeutral)){
    throw 'Selected textured/neutral roots are outside the exact accepted House raster cohorts'
  }
  foreach($pin in @(
      @{field='producer';relative='tools/export_material_master.py';bytes=118526L;sha='820F5AACD2BBF8CC5CC95984250DCF95AD8357EB3EF9D8AFE42A981A83752699'},
      @{field='test_source';relative='tests/gameview_textured_material_export_tests.py';bytes=118508L;sha='7DDDB89676DA31B7CDA25697A6F62E6304F751CBB0CB1FA2E01698789907DDB5'})){
    $expected=Join-Path (Join-Path $state 'neutral-texture-boundary-candidate') $pin.relative
    $declared=$cpuReport.($pin.field)
    if($declared -isnot [pscustomobject] -or
       ![StringComparer]::OrdinalIgnoreCase.Equals([string]$declared.path,$expected) -or
       [string]$declared.sha256 -ine $pin.sha){throw 'Accepted CPU producer/test source identity differs'}
    $actual=Get-HashRow $expected
    if($actual.bytes -ne $pin.bytes -or $actual.sha256 -cne $pin.sha){throw 'CPU producer/test source changed'}
    foreach($stage in @('protected_inputs_before','protected_inputs_after')){
      $rows=@($cpuReport.$stage | Where-Object {
        [StringComparer]::OrdinalIgnoreCase.Equals([string]$_.path,$expected)})
      if($rows.Count -ne 1 -or $rows[0].bytes -ne $pin.bytes -or $rows[0].sha256 -ine $pin.sha){
        throw 'CPU producer/test source is absent from an exact protected observation'
      }
    }
    $files[$actual.path]=$actual
  }
  $trees=[Collections.Generic.List[object]]::new();$aliases=@{}
  foreach($alias in @($cpuIndependent.owned_aliases)){
    if($alias.followed_in_inventory -ne $false){throw 'CPU prerequisite alias was followed'}
    $aliases[[string]$alias.path]=[string]$alias.target
  }
  foreach($rootRecord in @($cpuIndependent.retained_roots)+@($houseIndependent.retained_roots)){
    $root=[string]$rootRecord.root;Assert-FileAncestors $root
    if(!$root.StartsWith(($repoRoot+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Prerequisite root escaped repository'}
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
    $rows=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
      if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        if(!$aliases.ContainsKey($item.FullName) -or !$item.PSIsContainer -or @($item.Target).Count -ne 1 -or
           ![StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath(@($item.Target)[0]),[IO.Path]::GetFullPath($aliases[$item.FullName]))){throw 'Unplanned prerequisite reparse path'}
        Assert-FileAncestors $aliases[$item.FullName];continue
      }
      if($item.PSIsContainer){$pending.Push($item.FullName);continue}
      $row=Get-HashRow $item.FullName;$files[$row.path]=$row
      $relative=[IO.Path]::GetRelativePath($root,$row.path).Replace('\','/')
      $rows.Add($relative,[ordered]@{relative_path=$relative;path=$row.path;bytes=$row.bytes;sha256=$row.sha256})
    }}
    [string[]]$names=@($rows.Keys);[Array]::Sort($names,[StringComparer]::Ordinal)
    $ordered=@($names | ForEach-Object {$rows[$_]})
    $text=[Text.StringBuilder]::new('[');$first=$true
    $jsonOptions=[Text.Json.JsonSerializerOptions]::new();$jsonOptions.Encoder=[Text.Encodings.Web.JavaScriptEncoder]::UnsafeRelaxedJsonEscaping
    foreach($row in $ordered){if(!$first){[void]$text.Append(',')};$first=$false
      [void]$text.Append('{"bytes":').Append($row.bytes).Append(',"relative_path":').Append([Text.Json.JsonSerializer]::Serialize([object][string]$row.relative_path,[type][string],$jsonOptions)).Append(',"sha256":"').Append($row.sha256.ToLowerInvariant()).Append('"}')}
    [void]$text.Append("]`n")
    $sha=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text.ToString())))
    $bytes=Get-RowByteTotal $ordered
    if($sha -ine $rootRecord.inventory_sha256 -or $ordered.Count -ne $rootRecord.files -or $bytes -ne $rootRecord.bytes -or $bytes -gt 134217728){throw 'Accepted prerequisite retained-root inventory differs'}
    $trees.Add([ordered]@{root=$root;files=$ordered;file_count=$ordered.Count;total_bytes=$bytes;accepted_inventory_sha256=$sha})
  }
  # Main's independent3890 guarded identities include every actual raw capture,
  # process ledger, executable and compiled snapshot. Its source epoch is mapped
  # explicitly above; no old current-source verifier is invoked.
  if(@($mainIndependent.guard_before).Count -ne 3890 -or @($mainIndependent.guard_after).Count -ne 3890 -or $mainIndependent.changed_guards -isnot [array] -or @($mainIndependent.changed_guards).Count -ne 0){throw 'Historical main guard closure differs'}
  $membership=$mainIndependent.evidence_membership
  if($membership -isnot [pscustomobject] -or $membership.exact -ne $true -or $membership.root_count -ne 8 -or
     ($membership.before|ConvertTo-Json -Depth 8 -Compress) -cne ($membership.after|ConvertTo-Json -Depth 8 -Compress)){
    throw 'Historical main exact repeated membership is incomplete'
  }
  $mainRoots=@('gameview-neutral-material-main-2026-10-09-attempt02',
    'neutral-material-export-tests-2026-10-09-attempt01-export-a',
    'neutral-material-main-native-2026-10-09-attempt01','neutral-material-main-native-2026-10-09-attempt02',
    'neutral-material-native-cold-2026-10-09-attempt01','neutral-material-native-cold-2026-10-09-attempt02',
    'neutral-material-native-positive-2026-10-09-attempt05','neutral-material-native-positive-2026-10-09-attempt06') |
    ForEach-Object {Join-Path (Join-Path $repoRoot 'build') $_}
  if(@($membership.before.PSObject.Properties).Count -ne 8 -or
     @(Compare-Object @($mainRoots|Sort-Object) @($membership.before.PSObject.Properties.Name|Sort-Object)).Count){
    throw 'Historical main eight-root membership set differs'
  }
  $mainMembership=[Collections.Generic.List[object]]::new()
  foreach($root in $mainRoots){
    Assert-FileAncestors $root
    $accepted=@($membership.before.$root)
    $expected=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($record in $accepted){
      $relative=[string]$record.relative_path
      if(!$relative -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains('\') -or $relative.Contains(':') -or
         @($relative.Split('/')|Where-Object {$_ -in @('','.','..')}).Count -or !$expected.TryAdd($relative,$record)){
        throw 'Historical main membership has noncanonical/duplicate relative identity'
      }
    }
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
    $observed=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
      $relative=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')
      if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        if(!$expected.ContainsKey($relative)){throw 'Unexpected main evidence reparse point'}
        $record=$expected[$relative]
        $neutralExport=Join-Path (Join-Path $repoRoot 'build') 'neutral-material-export-tests-2026-10-09-attempt01-export-a'
        $allowedTarget=if($relative -ceq 'invalid-root-reparse'){$neutralExport}
          elseif($relative -cin @('invalid-notice-reparse/notice-alias','invalid-palette-internal-reparse/palette-alias')){
            Join-Path $neutralExport 'art-source/production-materials-v1/house-neutral-draft'}else{''}
        if(!(Split-Path $root -Leaf).StartsWith('neutral-material-native-cold-') -or !$allowedTarget -or
           $record.kind -cne 'owned-junction' -or $record.followed -ne $false -or !$item.PSIsContainer -or
           @($item.Target).Count -ne 1 -or
           ![StringComparer]::OrdinalIgnoreCase.Equals([string]$record.target,$allowedTarget) -or
            ![StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath(@($item.Target)[0]),$allowedTarget)){
          throw 'Historical main evidence junction changed or would be followed'
        }
        Assert-FileAncestors $allowedTarget
        $observed.Add($relative,[pscustomobject][ordered]@{followed=$false;kind='owned-junction';relative_path=$relative;target=[string]$record.target})
        continue
      }
      if($item.PSIsContainer){$pending.Push($item.FullName);continue}
      if(!$expected.ContainsKey($relative) -or $expected[$relative].kind -cne 'file' -or
         !$files.ContainsKey($item.FullName)){throw 'Unexpected or unguarded main evidence file'}
      $observed.Add($relative,[pscustomobject][ordered]@{kind='file';relative_path=$relative})
    }}
    if($observed.Count -ne $expected.Count){throw 'Historical main evidence membership count changed'}
    foreach($relative in $expected.Keys){
      if(!$observed.ContainsKey($relative) -or
         ($observed[$relative]|ConvertTo-Json -Compress) -cne ($expected[$relative]|ConvertTo-Json -Compress)){
        throw 'Historical main evidence membership row changed'
      }
    }
    $mainMembership.Add([ordered]@{root=$root;members=@($observed.Values|Sort-Object relative_path)})
  }
  return [ordered]@{classification='accepted historical CPU/House/main bytes plus exact4source epoch mappings and AssetCatalog compiler repair; no oracle or native replay';
    cpu_proof=Get-HashRow $cpuPath;files=@($files.Values | Sort-Object path);historical_mappings=@($mapping);
    compile_repair_mappings=@($repairMapping);retained_trees=@($trees);
    retained_aliases=@($cpuIndependent.owned_aliases);main_evidence_membership=@($mainMembership);
    CPU_role_maximum_bytes=4194304;old_current_source_verifiers_invoked=$false}
}

function Get-MaterialSourceRows([object]$document,[ValidateSet(14,20,35)][int]$expectedInputs=35,[string]$producerRelative='.unlazy/engine-completion-2026-10-08/neutral-texture-boundary-candidate/tools/export_material_master.py') {
  if($document -isnot [pscustomobject] -or
     ($document.schema -isnot [int64] -and $document.schema -isnot [int32]) -or $document.schema -ne 1 -or
     $document.complete -isnot [bool] -or $document.complete -ne $true -or
     ($document.item_count -isnot [int64] -and $document.item_count -isnot [int32]) -or $document.item_count -ne 2 -or
     $document.exporter_sha256 -isnot [string] -or $document.exporter_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
     $document.exporter_sha256.ToUpperInvariant() -cne (Get-HashRow (Join-Path $repoRoot $producerRelative)).sha256 -or
     $document.inputs_before -isnot [array] -or $document.inputs_after -isnot [array]){
    throw 'Material producer report is incomplete or its exporter binding differs'
  }
  $before=@($document.inputs_before);$after=@($document.inputs_after)
  if($before.Count -ne $expectedInputs -or $after.Count -ne $expectedInputs){throw 'Unexpected material producer source input count'}
  $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $result=[Collections.Generic.List[object]]::new()
  for($i=0;$i -lt $before.Count;$i++){
    $inputRow=$before[$i];$finalRow=$after[$i]
    if($inputRow -isnot [pscustomobject] -or $finalRow -isnot [pscustomobject] -or
       @(Compare-Object -CaseSensitive @('relative_path','bytes','sha256') @($inputRow.psobject.Properties.Name)).Count -or
       @(Compare-Object -CaseSensitive @('relative_path','bytes','sha256') @($finalRow.psobject.Properties.Name)).Count -or
       $inputRow.relative_path -isnot [string] -or $inputRow.sha256 -isnot [string] -or
       $finalRow.relative_path -isnot [string] -or $finalRow.sha256 -isnot [string] -or
       $inputRow.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $finalRow.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
       ($inputRow.bytes -isnot [int64] -and $inputRow.bytes -isnot [int32]) -or
       ($finalRow.bytes -isnot [int64] -and $finalRow.bytes -isnot [int32]) -or
       $inputRow.bytes -lt 0 -or $inputRow.bytes -gt 67108864){throw 'Invalid material producer source row'}
    $relative=[string]$inputRow.relative_path
    if([string]::IsNullOrEmpty($relative) -or $relative.IndexOfAny([char[]]@(':','\',[char]0)) -ge 0 -or
       [IO.Path]::IsPathRooted($relative) -or @($relative.Split('/') | Where-Object {$_ -eq '' -or $_ -eq '.' -or $_ -eq '..'}).Count -or
       !$seen.Add($relative)){throw 'Noncanonical or duplicate material source path'}
    foreach($component in $relative.Split('/')){
      if($component.EndsWith('.') -or $component.EndsWith(' ') -or $component -match '[\x00-\x1f<>"|?*]' -or
         $component.Split('.')[0] -match '^(CON|PRN|AUX|NUL|COM[0-9\u00b9\u00b2\u00b3]|LPT[0-9\u00b9\u00b2\u00b3])$'){
        throw 'Windows alias or nonportable material source component'
      }
    }
    if($finalRow.relative_path -cne $relative -or $finalRow.bytes -ne $inputRow.bytes -or
       $finalRow.sha256 -cne $inputRow.sha256){throw 'Material producer source observations differ'}
    $path=[IO.Path]::GetFullPath((Join-Path $repoRoot $relative))
    if(!$path.StartsWith(($repoRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
      throw 'Material source input escaped the repository'
    }
    $cursor=$path
    while($true){
      if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        throw "Material source input reparse path: $cursor"
      }
      $parent=Split-Path $cursor
      if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
      $cursor=$parent
    }
    $actual=Get-HashRow $path
    if($actual.bytes -ne $inputRow.bytes -or $actual.sha256 -ne $inputRow.sha256){throw 'Material source input changed'}
    $result.Add([ordered]@{relative_path=$relative;path=$actual.path;bytes=$actual.bytes;sha256=$actual.sha256})
  }
  return $result.ToArray()
}

function Test-FileTreeRows([string]$root,[object[]]$rows,[string]$label) {
  $current=@(Get-FileTreeRows $root)
  if($current.Count -ne $rows.Count){throw "$label file count changed"}
  for($index=0;$index -lt $rows.Count;$index++){
    if($current[$index].relative_path -ne $rows[$index].relative_path -or
       $current[$index].bytes -ne $rows[$index].bytes -or
       $current[$index].sha256 -ne $rows[$index].sha256){
      throw "$label changed: $($rows[$index].relative_path)"
    }
  }
}

function Convert-SdkTreeEvidence([object[]]$trees) {
  foreach($tree in $trees){
    $records=[Collections.Generic.List[object]]::new()
    foreach($row in @($tree.files)){
      $encoded=[Convert]::ToBase64String([Convert]::FromHexString([string]$row.sha256))
      $records.Add([object[]]@([string]$row.relative_path,[int64]$row.bytes,$encoded))
    }
    [ordered]@{root=[string]$tree.root;files=$records.ToArray()}
  }
}

# The bounded normal material probe is supplied separately. Bind only a repository source.
if($ProbeSource -cne 'tools/gameview_textured_material_probe.cpp'){
  throw 'ProbeSource must name the exact reviewed textured diagnostic translation unit'
}
Start-BindingPhase 'before'
try {
$probePath=[IO.Path]::GetFullPath((Join-Path $repoRoot $ProbeSource))
if(!$probePath.StartsWith(($repoRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
  throw 'ProbeSource must remain inside the authoritative repository'
}
if(!(Test-Path -LiteralPath $probePath -PathType Leaf)){
  throw "GameView normal material probe source is required: $probePath"
}
$probeRelative=[IO.Path]::GetRelativePath($repoRoot,$probePath).Replace('\','/')

# Validate and bind the separately generated diagnostic-only control kit. Nothing is copied.
$kitManifestPath=Join-Path $kitPath 'manifest.json'
$kitNoticePath=Join-Path $kitPath 'NOTICE.txt'
foreach($required in @($kitManifestPath,$kitNoticePath)){
  if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Invalid KitRoot; missing: $required"}
  [void](Get-HashRow $required)
}
$kitManifest=Get-Content -Raw -LiteralPath $kitManifestPath | ConvertFrom-Json
if([int]$kitManifest.schema -ne 1){throw 'KitRoot manifest schema must be 1'}
if(@($kitManifest.assets).Count -eq 0){throw 'KitRoot manifest has no diagnostic assets'}
if(@($kitManifest.assets | Where-Object {[string]$_.source -ne 'export_engine_control_kit.py'}).Count){
  throw 'KitRoot was not wholly generated by tools/export_engine_control_kit.py'
}
if(@($kitManifest.assets | Where-Object {[string]$_.id -eq 'control.metadata'}).Count -ne 1){
  throw 'KitRoot manifest must contain exactly one control.metadata asset'
}
if((Get-Content -Raw -LiteralPath $kitNoticePath) -notmatch 'diagnostics only'){
  throw 'KitRoot NOTICE.txt does not identify diagnostic-only controls'
}
foreach($asset in @($kitManifest.assets)){
  $assetPath=Resolve-InputChild $kitPath ([string]$asset.path)
  if(!$assetPath.StartsWith(($kitPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw "Kit manifest path escapes KitRoot: $($asset.path)"
  }
  if(!(Test-Path -LiteralPath $assetPath -PathType Leaf)){throw "Missing kit asset: $assetPath"}
  $assetRow=Get-HashRow $assetPath
  if($assetRow.bytes -ne [int64]$asset.bytes){throw "Kit asset byte mismatch: $($asset.path)"}
  if($assetRow.sha256 -ne ([string]$asset.sha256).ToUpperInvariant()){
    throw "Kit asset hash mismatch: $($asset.path)"
  }
}
$kitRows=Get-FileTreeRows $kitPath
$uiKitRows=Get-FileTreeRows $uiKitPath
$uiManifest=Get-Content -Raw -LiteralPath (Join-Path $uiKitPath 'manifest.json') | ConvertFrom-Json
if([int]$uiManifest.schema -ne 1 -or @($uiManifest.assets).Count -ne 50 -or $uiKitRows.Count -ne 52){
  throw 'UiKitRoot must be the fresh source-bound 50-payload subset with manifest and exporter report'
}
$uiKitReport=Get-Content -Raw -LiteralPath (Join-Path $uiKitPath 'ui-kit-report.json') | ConvertFrom-Json
foreach($asset in @($uiManifest.assets)){
  $path=Resolve-InputChild $uiKitPath ([string]$asset.path)
  if(!$path.StartsWith(($uiKitPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'UI asset escapes UiKitRoot'
  }
  $row=Get-HashRow $path
  if($row.bytes -ne [int64]$asset.bytes -or $row.sha256 -ne ([string]$asset.sha256).ToUpperInvariant()){
    throw "UI asset mismatch: $($asset.path)"
  }
}
if($uiKitReport.schema -ne 1 -or !$uiKitReport.complete -or $uiKitReport.payload_count -ne 50){
  throw 'UiKitRoot exporter report is incomplete or its schema/payload differs'
}

# Normal composition reads original art by reference; never bulk-stage it.
$originalWorldPath=Join-Path $originalArtPath 'art/world.json'
$originalManifestPath=Join-Path $originalArtPath 'manifest.json'
[void](Get-HashRow $originalWorldPath)
[void](Get-HashRow $originalManifestPath)
$originalWorld=Get-Content -Raw -LiteralPath $originalWorldPath | ConvertFrom-Json
$originalManifest=Get-Content -Raw -LiteralPath $originalManifestPath | ConvertFrom-Json
if($originalWorld.schema -ne 1 -or @($originalWorld.pages).Count -ne 67){
  throw 'Expected the original schema1/67-page normal art catalog'
}
if(@($originalWorld.pages | ForEach-Object {[string]$_.path} | Sort-Object -Unique).Count -ne 67){
  throw 'Original catalog must have 67 distinct page paths'
}
$originalRelative=@('art/world.json','manifest.json')+@($originalWorld.pages | ForEach-Object {[string]$_.path})
$originalCatalogRows=@($originalRelative | Sort-Object -Unique | ForEach-Object {
  $relative=[string]$_
  $path=Resolve-InputChild $originalArtPath $relative
  if(!$path.StartsWith(($originalArtPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Original page escaped the authoritative asset root'
  }
  $cursor=$path
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Original art reparse path: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
  $row=Get-HashRow $path
  if($relative -ne 'manifest.json'){
    $entry=@($originalManifest.assets | Where-Object {[string]$_.path -eq $relative})
    if($entry.Count -ne 1 -or [int64]$entry[0].bytes -ne $row.bytes -or
       ([string]$entry[0].sha256).ToUpperInvariant() -ne $row.sha256){throw "Original manifest mismatch: $relative"}
  }
  [ordered]@{relative_path=$relative;path=$row.path;bytes=$row.bytes;sha256=$row.sha256}
})
if($originalCatalogRows.Count -ne 69){throw 'Original catalog binding must contain exactly 69 files'}
# AssetCatalog verifies every manifest payload at construction, including files
# outside GameArt's page subset. Bind that complete input tree as well.
$originalArtRows=Get-FileTreeRows $originalArtPath
$expectedOriginalPaths=@('manifest.json')+@($originalManifest.assets | ForEach-Object {[string]$_.path})
if(@($originalManifest.assets).Count -ne 180 -or $originalArtRows.Count -ne 181 -or
   @($expectedOriginalPaths | Sort-Object -Unique).Count -ne 181 -or
   @(Compare-Object @($expectedOriginalPaths | Sort-Object) @($originalArtRows.relative_path | Sort-Object)).Count){
  throw 'Original AssetCatalog requires exactly 180 current payloads plus its manifest'
}
foreach($asset in @($originalManifest.assets)){
  $path=Resolve-InputChild $originalArtPath ([string]$asset.path)
  if(!$path.StartsWith(($originalArtPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Original manifest path escaped root'}
  $cursor=$path
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Original manifest reparse path: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
  $row=@($originalArtRows | Where-Object {$_.relative_path -eq [string]$asset.path})
  if($row.Count -ne 1 -or $row[0].bytes -ne [int64]$asset.bytes -or
     $row[0].sha256 -ne ([string]$asset.sha256).ToUpperInvariant()){
    throw "Original AssetCatalog payload mismatch: $($asset.path)"
  }
}
$materialRows=Get-FileTreeRows $materialPath
$materialMetadata=Get-Content -Raw -LiteralPath (Join-Path $materialPath 'art/material-collection.json') | ConvertFrom-Json
$materialManifest=Get-Content -Raw -LiteralPath (Join-Path $materialPath 'manifest.json') | ConvertFrom-Json
if($materialMetadata.schema -ne 1 -or $materialMetadata.kind -ne 'ribterra-material-collection-v1' -or
   $materialMetadata.item_count -ne 2 -or @($materialMetadata.items).Count -ne 2){
  throw 'This bounded diagnostic uses exactly two accepted registered collection items'
}
$neutralItems=@($materialMetadata.items | Where-Object {$_.PSObject.Properties.Name -contains 'albedo_source'})
if($neutralItems.Count -ne 1 -or $neutralItems[0].legacy_key -ne 'building.house.ready' -or
   $neutralItems[0].albedo_source.kind -ne 'authored-xyz-triangle-textures-v1' -or
   @($materialMetadata.items | Where-Object {$_.legacy_key -eq 'resident.man.walk.2.1'}).Count -ne 1 -or
   $materialMetadata.paint_colour -ne 'authored-XYZ-textured-palette-draft-and-legacy-painted-neutral-acceptance-pending'){
  throw 'This neutral cohort requires one explicit authored house and the unchanged diagnostic resident'
}
if($materialRows.Count -ne 23 -or $materialManifest.schema -ne 1 -or
   @($materialManifest.assets).Count -ne 18 -or
   @($materialManifest.assets | ForEach-Object {[string]$_.path} | Sort-Object -Unique).Count -ne 18){
  throw 'Textured collection must contain exactly23files/18unique manifest payloads'
}
$expectedMaterialPaths=@('manifest.json','NOTICE.txt','material-collection-report.json')+
  @($materialManifest.assets | ForEach-Object {[string]$_.path})+
  @($materialManifest.assets | ForEach-Object {[string]$_.notice} | Sort-Object -Unique)
$expectedMaterialPaths=@($expectedMaterialPaths | Sort-Object -Unique)
if(@($expectedMaterialPaths | Sort-Object -Unique).Count -ne 23 -or
   @($materialManifest.assets | Where-Object {$_.path -eq 'art/world.json'}).Count -ne 1 -or
   @($materialManifest.assets | Where-Object {$_.path -eq 'art/material-collection.json'}).Count -ne 1 -or
   @(Compare-Object @($expectedMaterialPaths | Sort-Object) @($materialRows.relative_path | Sort-Object)).Count){
  throw 'Unexpected material collection disclosure/payload overlap'
}
foreach($asset in @($materialManifest.assets)){
  $path=Resolve-InputChild $materialPath ([string]$asset.path)
  if(!$path.StartsWith(($materialPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Material path escaped root'}
  $cursor=$path
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Material reparse path: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
  if(@($materialRows | Where-Object {$_.relative_path -eq [string]$asset.path}).Count -ne 1){
    throw 'Material manifest payload is missing from the bound recursive inventory'
  }
  $row=Get-HashRow $path
  if($row.bytes -ne [int64]$asset.bytes -or $row.sha256 -ne ([string]$asset.sha256).ToUpperInvariant()){
    throw "Material manifest mismatch: $($asset.path)"
  }
}
foreach($relative in $expectedMaterialPaths){
  $cursor=Resolve-InputChild $materialPath $relative
  if(!$cursor.StartsWith(($materialPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Material disclosure escaped root'}
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Material disclosure reparse path: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
}

$materialExportReport=Get-Content -Raw -LiteralPath (Join-Path $materialPath 'material-collection-report.json') | ConvertFrom-Json
$materialSourceBefore=@(Get-MaterialSourceRows $materialExportReport 35)

$paintedRows=Get-FileTreeRows $paintedPath
$paintedManifest=Get-Content -Raw -LiteralPath (Join-Path $paintedPath 'manifest.json') | ConvertFrom-Json
$paintedMetadata=Get-Content -Raw -LiteralPath (Join-Path $paintedPath 'art/material-collection.json') | ConvertFrom-Json
if($paintedRows.Count -ne 18 -or $paintedManifest.schema -ne 1 -or @($paintedManifest.assets).Count -ne 14 -or
   $paintedMetadata.schema -ne 1 -or $paintedMetadata.kind -ne 'ribterra-material-collection-v1' -or
   $paintedMetadata.item_count -ne 2 -or @($paintedMetadata.items).Count -ne 2 -or
   @($paintedMetadata.items | Where-Object {$_.PSObject.Properties.Name -contains 'albedo_source'}).Count -or
   @($paintedMetadata.items | Where-Object {$_.legacy_key -eq 'building.house.ready'}).Count -ne 1 -or
   @($paintedMetadata.items | Where-Object {$_.legacy_key -eq 'resident.man.walk.2.1'}).Count -ne 1){
  throw 'PaintedRoot must be the entire two-item absent-mode compatibility collection'
}
$expectedPaintedPaths=@('manifest.json','NOTICE.txt','material-collection-report.json')+
  @($paintedManifest.assets | ForEach-Object {[string]$_.path})+
  @($paintedManifest.assets | ForEach-Object {[string]$_.notice} | Sort-Object -Unique)
$expectedPaintedPaths=@($expectedPaintedPaths | Sort-Object -Unique)
if($expectedPaintedPaths.Count -ne 18 -or
   @($paintedManifest.assets | ForEach-Object {[string]$_.path} | Sort-Object -Unique).Count -ne 14 -or
   @(Compare-Object @($expectedPaintedPaths | Sort-Object) @($paintedRows.relative_path | Sort-Object)).Count){
  throw 'PaintedRoot file disclosure or manifest membership differs'
}
foreach($row in $paintedRows){
  $relative=[string]$row.relative_path
  if($relative.IndexOfAny([char[]]@(':','\',[char]0)) -ge 0 -or [IO.Path]::IsPathRooted($relative) -or
     @($relative.Split('/') | Where-Object {$_ -in @('','.', '..')}).Count){throw 'Nonportable painted compatibility path'}
  $cursor=[string]$row.path
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
      throw "Painted compatibility reparse path: $cursor"
    }
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
}
foreach($asset in $paintedManifest.assets){
  $row=@($paintedRows | Where-Object {$_.relative_path -ceq [string]$asset.path})
  if($row.Count -ne 1 -or $row[0].bytes -ne $asset.bytes -or $row[0].sha256 -ne $asset.sha256){
    throw 'Painted compatibility manifest payload differs'
  }
}
$paintedExportReport=Get-Content -Raw -LiteralPath (Join-Path $paintedPath 'material-collection-report.json') | ConvertFrom-Json
$paintedSourceBefore=@(Get-MaterialSourceRows $paintedExportReport 14 'tools/export_material_master.py')
$neutralRows=Get-FileTreeRows $neutralPath
$neutralManifest=Get-Content -Raw -LiteralPath (Join-Path $neutralPath 'manifest.json') | ConvertFrom-Json
$neutralExportReport=Get-Content -Raw -LiteralPath (Join-Path $neutralPath 'material-collection-report.json') | ConvertFrom-Json
if($neutralRows.Count -ne 20 -or @($neutralManifest.assets).Count -ne 15){throw 'Old MaterialCollection must retain20files/15manifest payloads'}
$neutralSourceBefore=@(Get-MaterialSourceRows $neutralExportReport 20 'tools/export_material_master.py')
Close-BindingPhase
Start-BindingPhase 'prerequisites-before'
$prerequisites=Get-PrerequisiteRows $cpuProofPath $materialPath $neutralPath
Close-BindingPhase
Start-BindingPhase 'dependencies-before'


# Bind installed static SDK/header/target metadata by reference, never copy its libraries.
$dependencyRelative=@(Get-FileTreeRows (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/RmlUi') | ForEach-Object {$_.path})+
  @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/rmlui') -File -Filter '*.cmake' | ForEach-Object {$_.FullName})+
  @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/freetype') -File -Filter '*.cmake' | ForEach-Object {$_.FullName})
$dependencyRows=@($dependencyRelative | Sort-Object -Unique | ForEach-Object {Get-HashRow $_})
$versionText=Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/rmlui/RmlUiConfigVersion.cmake')
if($versionText -notmatch 'set\(PACKAGE_VERSION "6\.3"\)'){throw 'The exact installed RmlUi6.3 SDK is required'}
$externalHeaderRoots=@(
  (Join-Path $repoRoot '.deps/SDL3-3.4.16/include'),
  (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include'))
$externalHeaderTreesBefore=@($externalHeaderRoots | ForEach-Object {
  $root=$_
  $cursor=$root
  while($true){
    if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "External header ancestor reparse path: $cursor"}
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
  foreach($directory in @((Get-Item -LiteralPath $root -Force))+
      @(Get-ChildItem -LiteralPath $root -Directory -Recurse -Force)){
    if(($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "External header reparse directory: $($directory.FullName)"}
  }
  [ordered]@{root=$root;files=@(Get-FileTreeRows $root)}
})

# Bind the current launcher selection. The fresh shader cohort below never reads
# or copies an accepted/retained shader directory.
$currentBuildPointer=Join-Path $repoRoot 'dist/current-build.txt'
$launcherPath=Join-Path $repoRoot 'Play Ribterra.cmd'
$reworkLauncherPath=Join-Path $repoRoot 'Play Ribterra Rework.cmd'
foreach($required in @($currentBuildPointer,$launcherPath,$reworkLauncherPath)){
  if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing protected launcher input: $required"}
  [void](Get-HashRow $required)
}
$rootExecutable=Join-Path $repoRoot 'Ribterra.exe'
if(Test-Path -LiteralPath $rootExecutable -PathType Leaf){
  $launcherExecutable=$rootExecutable
} else {
  $pointerValue=(Get-Content -Raw -LiteralPath $currentBuildPointer).Trim()
  if([string]::IsNullOrWhiteSpace($pointerValue)){throw 'dist/current-build.txt is empty'}
  $launcherExecutable=[IO.Path]::GetFullPath((Join-Path $repoRoot $pointerValue))
}
$reworkText=Get-Content -Raw -LiteralPath $reworkLauncherPath
$reworkMatch=[regex]::Match($reworkText,'(?im)^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$')
if(!$reworkMatch.Success){throw 'Could not resolve the exact executable target from Play Ribterra Rework.cmd'}
$reworkExecutable=[IO.Path]::GetFullPath((Join-Path $repoRoot $reworkMatch.Groups[1].Value))

$protectedPaths=@($launcherPath,$reworkLauncherPath,$currentBuildPointer,$launcherExecutable,$reworkExecutable)
$protectedBefore=@($protectedPaths | ForEach-Object {
  if(!(Test-Path -LiteralPath $_ -PathType Leaf)){throw "Missing protected input: $_"}
  Get-HashRow $_
})

$currentShaderRoot=Join-Path $repoRoot 'shaders'
$currentShaderFiles=@(Get-FileTreeRows $currentShaderRoot)
if($currentShaderFiles.Count -eq 0){throw 'Current shader source closure is empty'}
$shaderSourceRelativePaths=@($currentShaderFiles | ForEach-Object {
  [IO.Path]::GetRelativePath($repoRoot,$_.path).Replace('\','/')
})
$shaderPrograms=@('sprite','surface_lit','surface_hdr','world_linear','hdr_composite','material_sprite',
  'ground_gbuffer','ground_lighting','material_forward','ground_contact','ground_contact_gbuffer',
  'ground_contact_lighting','ground_sky_lighting','material_sky_forward','ground_shadow_lighting',
  'material_shadow_forward','sky_shadow_depth','water_ground_gbuffer','water_ground_lighting','water_reflection',
  'atmosphere_ground_lighting','atmosphere_material_forward','atmosphere_water_reflection')
foreach($program in $shaderPrograms){
  foreach($stage in @('vert','frag')){
    $required=Join-Path $currentShaderRoot "$program.$stage.hlsl"
    if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing current shader source: $required"}
  }
}

$translationUnits=[ordered]@{
  foundation='src/foundation/foundation.cpp'
  economy='src/foundation/economy.cpp'
  lobby='src/foundation/lobby.cpp'
  atomic_file='src/foundation/atomic_file.cpp'
  platform='src/foundation/platform.cpp'
  world='src/sim/world.cpp'
  weather='src/sim/weather.cpp'
  skills='src/sim/skills.cpp'
  world_save='src/sim/world_save.cpp'
  game_server='src/server/game_server.cpp'
  gns_transport='src/net/gns_transport.cpp'
  protocol='src/net/protocol.cpp'
  network_session='src/net/network_session.cpp'
  asset_catalog='src/assets/asset_catalog.cpp'
  texture_atlas='src/assets/texture_atlas.cpp'
  localization='src/ui/localization.cpp'
  renderer2d='src/render/renderer2d.cpp'
  contact_shadow='src/render/contact_shadow.cpp'
  sky_shadow='src/render/sky_shadow.cpp'
  view='src/client/view.cpp'
  client_session='src/client/client_session.cpp'
  game_art='src/client/game_art.cpp'
  game_ui='src/ui/game_ui.cpp'
  gameview_textured_material_probe=$probeRelative
}
$incClosure=@(
  'src/ui/game_ui_lobby.inc','src/ui/game_ui_manual.inc','src/ui/game_ui_settlement.inc',
  'src/ui/game_ui_economy.inc','src/ui/game_ui_buildings.inc','src/ui/game_ui_professions.inc',
  'src/sim/world_settlement.inc','src/sim/world_economy.inc','src/sim/world_wilderness.inc',
  'src/sim/world_nations.inc','src/sim/world_logistics.inc','src/sim/world_logistics_validation.inc',
  'src/sim/world_manual.inc','src/sim/world_manual_validation.inc','src/sim/world_autonomy.inc',
  'src/sim/world_professions.inc','src/sim/world_professions_validation.inc','src/sim/world_work.inc',
  'src/sim/world_work_validation.inc','src/sim/world_economy_api.inc','src/sim/world_wilderness_api.inc',
  'src/sim/world_nations_api.inc','src/server/game_server_lobby.inc','src/client/client_production_group.inc')
$snapshotFiles=@($translationUnits.Values)+$incClosure+@(
  'tools/compile_skill_balance.py','data/skills.json','schemas/world_save.fbs','schemas/network.fbs',
  'schemas/network_frame.fbs','CMakeLists.txt','vcpkg.json','tests/shader_contract.py',
  'tools/build_gameview_textured_material_isolated.ps1','tools/engine_control_probe.hpp',
  'tools/export_engine_control_kit.py','tools/export_material_master.py',
  'tools/export_engine_ui_kit.py','tools/asset_tool.py','content/asset_sources.json',
  'licenses/Cormorant-OFL.txt','licenses/Noto-OFL.txt','licenses/PTSerif-OFL.txt','licenses/Ribterra-Assets.txt',
  'tools/run_gameview_textured_material_isolated.ps1','tests/engine_control_kit_tests.py',
  'tests/gameview_cumulative_evidence_tests.py',
  'tests/gameview_session_time_evidence_tests.py',
  'tests/gameview_opacity_evidence_tests.py',
  'tests/gameview_material_evidence_tests.py',
  'tests/gameview_material_semantics_tests.py',
  'tests/gameview_textured_material_evidence_tests.py',
  'tests/gameview_neutral_material_export_tests.py',
  'tests/gameview_ui_evidence_tests.py',
  'tests/gameview_ui_scene_evidence_tests.py',
  'tests/weather_fixture.hpp')+$shaderSourceRelativePaths
} finally { Close-BindingPhase }

Test-Headroom 'before output creation'
New-Item -ItemType Directory -Path $outPath | Out-Null
New-Item -ItemType Directory -Path $objectPath | Out-Null
foreach($directory in @('source','compiler-temp','generated','assets','assets/fonts','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outPath $directory) | Out-Null
}
Write-NewJson (Join-Path $outPath 'protected-before.json') $protectedBefore 6

try {
  $sourceRoot=Join-Path $outPath 'source'
  Start-BindingPhase 'source-snapshot'
  try {
    $includeRows=@(Get-FileTreeRows (Join-Path $repoRoot 'include'))
    $snapshotFiles+=@($includeRows | ForEach-Object {'include/'+$_.relative_path})
    $sourceIdentities=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $sourceRows=@(foreach($relative in $snapshotFiles){
      if(!$sourceIdentities.Add($relative)){throw 'Duplicate source snapshot identity'}
      $origin=Resolve-InputChild $repoRoot $relative
      $originRow=Get-HashRow $origin
      $destination=Join-Path $sourceRoot $relative
      New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
      [IO.File]::Copy($origin,$destination,$false)
      $snapshotRow=Get-HashRow $destination
      if($originRow.bytes -ne $snapshotRow.bytes -or $originRow.sha256 -ne $snapshotRow.sha256){throw "Snapshot hash mismatch: $relative"}
      [pscustomobject][ordered]@{relative_path=$relative;repository_path=$originRow.path;
        snapshot_path=$snapshotRow.path;bytes=$snapshotRow.bytes;sha256=$snapshotRow.sha256}
    })
    $sourceRows=@($sourceRows | Sort-Object snapshot_path)
  } finally { Close-BindingPhase }
  Write-NewJson (Join-Path $outPath 'source-provenance.json') $sourceRows 7

  Test-Headroom 'after source snapshot'

  . 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:TEMP=Join-Path $outPath 'compiler-temp'
  $env:TMP=$env:TEMP
  $env:PYTHONDONTWRITEBYTECODE='1'

  $python='C:/Python314/python.exe'
  $flatc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe'
  $shadercross=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe'
  $shadercrossLibrary=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/SDL3_shadercross.dll'
  $shadercrossDxcompiler=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/dxcompiler.dll'
  $dxc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe'
  foreach($tool in @('cl.exe','link.exe')){
    if($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)){throw "Missing VS18 amd64 tool: $tool"}
  }
  foreach($tool in @($python,$flatc,$shadercross,$shadercrossLibrary,$shadercrossDxcompiler,$dxc)){
    if(!(Test-Path -LiteralPath $tool -PathType Leaf)){throw "Missing pinned build tool: $tool"}
  }
  $toolRows=@(Get-HashRow (Get-Command cl.exe).Source; Get-HashRow (Get-Command link.exe).Source;
    Get-HashRow $python; Get-HashRow $flatc; Get-HashRow $shadercross; Get-HashRow $shadercrossLibrary;
    Get-HashRow $shadercrossDxcompiler; Get-HashRow $dxc)

  $compilerEnvironment=[ordered]@{include=$env:INCLUDE;lib=$env:LIB;
    vs_install_dir=$env:VSINSTALLDIR;vc_tools_version=$env:VCToolsVersion;
    windows_sdk_dir=$env:WindowsSdkDir;windows_sdk_version=$env:WindowsSDKVersion}
  if(@($compilerEnvironment.Values | Where-Object {[string]::IsNullOrWhiteSpace([string]$_)}).Count){
    throw 'Visual Studio and Windows SDK compile environment must be fully identified'
  }
  $snapshotShaderRoot=Join-Path $sourceRoot 'shaders'
  $compiledShaderRoot=Join-Path $outPath 'assets/shaders'
  foreach($program in $shaderPrograms){
    foreach($stage in @('vert','frag')){
      $stageName=if($stage -eq 'vert'){'vertex'}else{'fragment'}
      $source=Join-Path $snapshotShaderRoot "$program.$stage.hlsl"
      foreach($destinationFormat in @('DXIL','JSON')){
        $extension=if($destinationFormat -eq 'DXIL'){'dxil'}else{'json'}
        $destination=Join-Path $compiledShaderRoot "$program.$stage.$extension"
        Invoke-Checked $shadercross @($source,'-s','HLSL','-d',$destinationFormat,'-t',$stageName,
          '-e','main','-I',$snapshotShaderRoot,'-o',$destination) "shader-$program-$stage-$extension.log"
      }
    }
  }
  Invoke-Checked $python @((Join-Path $sourceRoot 'tests/shader_contract.py'),$snapshotShaderRoot,
    $compiledShaderRoot,'--dxc',$dxc,'--shadercross',$shadercross,'--write-manifest') 'shader-contract.log'

  Invoke-Checked $python @((Join-Path $sourceRoot 'tools/compile_skill_balance.py'),
    (Join-Path $sourceRoot 'data/skills.json'),(Join-Path $outPath 'generated/skill_balance_generated.h')) 'generate-skill-balance.log'
  foreach($schema in @('world_save','network','network_frame')){
    Invoke-Checked $flatc @('--cpp','--scoped-enums','--gen-object-api','-o',(Join-Path $outPath 'generated'),
      (Join-Path $sourceRoot "schemas/$schema.fbs")) "generate-$schema.log"
  }

  $common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
    '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/GR','/Zc:__cplusplus','/DRMLUI_STATIC_LIB',
    '/DRIBTERRA_PROBE_BUILD="gameview-textured-material-isolated-unversioned"',
    ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $outPath 'generated')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
  foreach($entry in $translationUnits.GetEnumerator()){
    $unitOptions=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}
      elseif($entry.Key -eq 'gameview_textured_material_probe'){@('/Zc:inline')}
      else{@()}
    Invoke-Checked 'cl.exe' ($common+$unitOptions+@('/c',(Join-Path $sourceRoot $entry.Value),
      ('/Fo'+(Join-Path $objectPath ($entry.Key+'.obj'))))) ('compile-'+$entry.Key+'.log')
  }

  $vcpkgLibRoot=Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/lib'
  $vcpkgNames=@(
    'flatbuffers.lib','zstd.lib','GameNetworkingSockets_s.lib','libprotobuf.lib',
    'absl_log_internal_check_op.lib','absl_die_if_null.lib','absl_log_internal_conditions.lib',
    'absl_log_internal_message.lib','absl_log_internal_nullguard.lib','absl_examine_stack.lib',
    'absl_log_internal_format.lib','absl_log_internal_structured_proto.lib','absl_log_internal_log_sink_set.lib',
    'absl_log_sink.lib','absl_log_entry.lib','absl_log_internal_proto.lib','absl_flags_internal.lib',
    'absl_flags_marshalling.lib','absl_flags_reflection.lib','absl_flags_config.lib','absl_flags_program_name.lib',
    'absl_flags_private_handle_accessor.lib','absl_flags_commandlineflag.lib','absl_flags_commandlineflag_internal.lib',
    'absl_log_initialize.lib','absl_log_internal_globals.lib','absl_log_globals.lib','absl_vlog_config_internal.lib',
    'absl_log_internal_fnmatch.lib','absl_raw_hash_set.lib','absl_hashtablez_sampler.lib',
    'absl_random_distributions.lib','absl_random_seed_sequences.lib','absl_random_internal_entropy_pool.lib',
    'absl_random_internal_randen.lib','absl_random_internal_randen_hwaes.lib',
    'absl_random_internal_randen_hwaes_impl.lib','absl_random_internal_randen_slow.lib',
    'absl_random_internal_platform.lib','absl_random_internal_seed_material.lib','absl_random_seed_gen_exception.lib',
    'absl_statusor.lib','absl_status.lib','absl_cord.lib','absl_cordz_info.lib','absl_cord_internal.lib',
    'absl_cordz_functions.lib','absl_exponential_biased.lib','absl_cordz_handle.lib','absl_crc_cord_state.lib',
    'absl_crc32c.lib','absl_crc_internal.lib','absl_crc_cpu_detect.lib','absl_leak_check.lib','absl_strerror.lib',
    'absl_str_format_internal.lib','absl_synchronization.lib','absl_stacktrace.lib','absl_borrowed_fixup_buffer.lib',
    'absl_hash.lib','absl_city.lib','absl_symbolize.lib','absl_debugging_internal.lib','absl_demangle_internal.lib',
    'absl_demangle_rust.lib','absl_decode_rust_punycode.lib','absl_utf8_for_code_point.lib',
    'absl_graphcycles_internal.lib','absl_kernel_timeout_internal.lib','absl_malloc_internal.lib',
    'absl_tracing_internal.lib','absl_time.lib','absl_civil_time.lib','absl_time_zone.lib','utf8_validity.lib',
    'absl_strings.lib','absl_int128.lib','absl_strings_internal.lib','absl_base.lib','absl_spinlock_wait.lib',
    'absl_throw_delegate.lib','absl_raw_logging_internal.lib','absl_log_severity.lib',
    'libcrypto.lib','icuin.lib','icuuc.lib','icudt.lib','libpng16.lib','zs.lib',
    'rmlui.lib','freetype.lib','bz2.lib','brotlidec.lib','brotlicommon.lib')
  $thirdPartyLibraries=@(
    (Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),
    (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'))+
    @($vcpkgNames | ForEach-Object {Join-Path $vcpkgLibRoot $_})
  foreach($library in $thirdPartyLibraries){
    if(!(Test-Path -LiteralPath $library -PathType Leaf)){throw "Missing pinned link library: $library"}
  }
  $systemLibraryNames=@('winmm.lib','iphlpapi.lib','crypt32.lib','bcrypt.lib','shell32.lib','ws2_32.lib',
    'kernel32.lib','user32.lib','gdi32.lib','winspool.lib','ole32.lib','oleaut32.lib','uuid.lib',
    'comdlg32.lib','advapi32.lib')
  $systemLibraries=@($systemLibraryNames | ForEach-Object {Resolve-SystemLibrary $_})
  $linkLibraries=@($thirdPartyLibraries+$systemLibraries)
  $linkLibraryRows=@($linkLibraries | ForEach-Object {Get-HashRow $_})
  $objects=@($translationUnits.Keys | ForEach-Object {Join-Path $objectPath ($_.ToString()+'.obj')})
  $objectBeforeLink=@($objects | ForEach-Object {Get-HashRow $_})
  Invoke-Checked 'link.exe' (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
    ('/out:'+(Join-Path $outPath 'GameViewTexturedMaterialProbe.exe')))+$objects+$linkLibraries) 'link-GameViewTexturedMaterialProbe.log'

  $runtimeInputs=@(
    @('.deps/SDL3-3.4.16/lib/x64/SDL3.dll','SDL3.dll'),
    @('.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll','SDL3_ttf.dll'),
    @('assets/fonts/NotoSans-Regular.ttf','assets/fonts/NotoSans-Regular.ttf'))
  $runtimeRows=@()
  foreach($runtime in $runtimeInputs){
    $origin=Join-Path $repoRoot $runtime[0]
    if(!(Test-Path -LiteralPath $origin -PathType Leaf)){throw "Missing pinned runtime input: $origin"}
    $destination=Join-Path $outPath $runtime[1]
    Assert-NewPath $destination
    [IO.File]::Copy($origin,$destination,$false)
    $originRow=Get-HashRow $origin
    $destinationRow=Get-HashRow $destination
    if($originRow.sha256 -ne $destinationRow.sha256){throw "Runtime copy hash mismatch: $($runtime[1])"}
    $runtimeRows += [ordered]@{origin=$originRow.path;destination=$destinationRow.path;
      bytes=$destinationRow.bytes;sha256=$destinationRow.sha256}
  }

  Start-BindingPhase 'final'
  try {
  $sourceFinalRows=@($sourceRows | ForEach-Object {
    $origin=Get-HashRow $_.repository_path
    $snapshot=Get-HashRow $_.snapshot_path
    $unchanged=($origin.bytes -eq $_.bytes -and $snapshot.bytes -eq $_.bytes -and
      $origin.sha256 -eq $_.sha256 -and $snapshot.sha256 -eq $_.sha256)
    [ordered]@{relative_path=$_.relative_path;repository_path=$_.repository_path;snapshot_path=$_.snapshot_path;
      bytes=$_.bytes;sha256=$_.sha256;unchanged=$unchanged}
  })
  if(@($sourceFinalRows | Where-Object {-not $_.unchanged}).Count){throw 'Source cohort changed during isolated build'}
  $linkLibraryFinal=@($linkLibraryRows | ForEach-Object {
    $after=Get-HashRow $_.path
    [ordered]@{path=$_.path;bytes=$_.bytes;sha256=$_.sha256;after_bytes=$after.bytes;
      after_sha256=$after.sha256;unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  if(@($linkLibraryFinal | Where-Object {-not $_.unchanged}).Count){throw 'Link-library cohort changed during build'}
  Test-FileTreeRows $materialPath $materialRows 'MaterialRoot'
  $materialSourceAfter=@(Get-MaterialSourceRows $materialExportReport 35)
  Test-FileTreeRows $paintedPath $paintedRows 'PaintedRoot'
  $paintedSourceAfter=@(Get-MaterialSourceRows $paintedExportReport 14 'tools/export_material_master.py')
  Test-FileTreeRows $neutralPath $neutralRows 'NeutralRoot'
  $neutralSourceAfter=@(Get-MaterialSourceRows $neutralExportReport 20 'tools/export_material_master.py')
  Close-BindingPhase
  Start-BindingPhase 'prerequisites-final'
  $prerequisitesFinal=Get-PrerequisiteRows $cpuProofPath $materialPath $neutralPath
  Close-BindingPhase
  Start-BindingPhase 'remaining-final'
  if(($prerequisites|ConvertTo-Json -Depth 16 -Compress) -cne ($prerequisitesFinal|ConvertTo-Json -Depth 16 -Compress)){throw 'Accepted prerequisite closure changed during actual build'}
  Test-FileTreeRows $originalArtPath $originalArtRows 'OriginalAssetCatalog'
  foreach($row in $originalArtRows){
    $after=Get-HashRow $row.path
    if($row.bytes -ne $after.bytes -or $row.sha256 -ne $after.sha256){throw 'Original art changed during build'}
  }
  Test-FileTreeRows $kitPath $kitRows 'KitRoot'
  Test-FileTreeRows $uiKitPath $uiKitRows 'UiKitRoot'
  $dependencyFinal=@($dependencyRows | ForEach-Object {
    $after=Get-HashRow $_.path
    [ordered]@{path=$_.path;bytes=$_.bytes;sha256=$_.sha256;after_bytes=$after.bytes;
      after_sha256=$after.sha256;unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  if(@($dependencyFinal | Where-Object {-not $_.unchanged}).Count){throw 'Installed RmlUi dependency cohort changed'}
  $externalHeaderTreesAfter=@($externalHeaderTreesBefore | ForEach-Object {
    Test-FileTreeRows ([string]$_.root) @($_.files) 'ExternalHeaderTree'
    [ordered]@{root=$_.root;files=@(Get-FileTreeRows ([string]$_.root))}
  })
  Write-NewJson (Join-Path $outPath 'source-final-hashes.json') $sourceFinalRows 7

  $shaderManifestPath=Join-Path $compiledShaderRoot 'manifest.json'
  if(!(Test-Path -LiteralPath $shaderManifestPath -PathType Leaf)){throw 'Fresh shader manifest was not produced'}
  $shaderManifest=Get-Content -Raw -LiteralPath $shaderManifestPath | ConvertFrom-Json
  $manifestSourceClosure=@($shaderManifest.source_closure)
  if($manifestSourceClosure.Count -ne $currentShaderFiles.Count){
    throw 'Fresh shader manifest does not bind every current shader source'
  }
  $seenShaderSources=@{}
  foreach($record in $manifestSourceClosure){
    $relative=([string]$record.source).Replace('/','\')
    $source=[IO.Path]::GetFullPath((Join-Path $snapshotShaderRoot $relative))
    if(!$source.StartsWith(($snapshotShaderRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
      throw "Shader source closure path escapes snapshot: $($record.source)"
    }
    if($seenShaderSources.ContainsKey($source)){throw "Duplicate shader source closure path: $($record.source)"}
    $seenShaderSources[$source]=$true
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Shader source closure file missing: $($record.source)"}
    $row=Get-HashRow $source
    if($row.bytes -ne [int64]$record.bytes -or $row.sha256 -ne ([string]$record.sha256).ToUpperInvariant()){
      throw "Shader source closure hash mismatch: $($record.source)"
    }
  }
  $manifestPrograms=@($shaderManifest.programs.PSObject.Properties.Name | Sort-Object)
  $expectedPrograms=@($shaderPrograms | Sort-Object)
  if(Compare-Object $expectedPrograms $manifestPrograms){throw 'Fresh shader manifest program set is incomplete'}
  if(@($shaderManifest.shaders).Count -ne (2*$shaderPrograms.Count)){
    throw "Fresh shader manifest must contain exactly $((2*$shaderPrograms.Count)) stage records"
  }
  $shaderArtifactRows=@(Get-ChildItem -LiteralPath $compiledShaderRoot -File | Sort-Object Name |
    ForEach-Object {Get-HashRow $_.FullName})
  if($shaderArtifactRows.Count -ne (4*$shaderPrograms.Count+1)){
    throw "Fresh shader root must contain exactly $((4*$shaderPrograms.Count+1)) artifacts including manifest"
  }
  foreach($record in @($shaderManifest.shaders)){
    foreach($binding in @(@([string]$record.file,[string]$record.sha256),
                          @([string]$record.reflection,[string]$record.reflection_sha256))){
      $name=[IO.Path]::GetFileName($binding[0])
      if($name -ne $binding[0]){throw "Shader manifest artifact path is not a leaf name: $($binding[0])"}
      $artifact=Join-Path $compiledShaderRoot $name
      if(!(Test-Path -LiteralPath $artifact -PathType Leaf)){throw "Shader manifest artifact missing: $name"}
      if((Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash -ne $binding[1].ToUpperInvariant()){
        throw "Shader manifest artifact hash mismatch: $name"
      }
    }
  }
  $shaderSourceRows=@($sourceFinalRows | Where-Object {$_.relative_path -like 'shaders/*'})
  if($shaderSourceRows.Count -ne $currentShaderFiles.Count){throw 'Fresh shader source closure count changed'}

  $objectRows=@($objects | ForEach-Object {Get-HashRow $_})
  for($index=0;$index -lt $objectRows.Count;$index++){
    if($objectRows[$index].path -ne $objectBeforeLink[$index].path -or
       $objectRows[$index].bytes -ne $objectBeforeLink[$index].bytes -or
       $objectRows[$index].sha256 -ne $objectBeforeLink[$index].sha256){
      throw 'Fresh compilation object changed during link'
    }
  }
  $generatedRows=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'generated') -File | Sort-Object Name |
    ForEach-Object {Get-HashRow $_.FullName})
  if($generatedRows.Count -ne 4){throw "Expected exactly four generated headers, got $($generatedRows.Count)"}
  $executableRow=Get-HashRow (Join-Path $outPath 'GameViewTexturedMaterialProbe.exe')
  $exitSidecars=@(Get-ChildItem -LiteralPath $outPath -File -Filter '*.log.exit.txt' | Sort-Object Name)
  $expectedCommandCount=(4*$shaderPrograms.Count)+1+4+$translationUnits.Count+1
  if($exitSidecars.Count -ne $expectedCommandCount){
    throw "Expected $expectedCommandCount command exit sidecars, got $($exitSidecars.Count)"
  }
  if($commandRows.Count -ne $expectedCommandCount){throw 'Actual command argv ledger count differs'}
  foreach($sidecar in $exitSidecars){
    if((Get-Content -Raw -LiteralPath $sidecar.FullName).Trim() -ne '0'){
      throw "Nonzero command exit sidecar: $($sidecar.Name)"
    }
  }
  $commandEvidence=@(Get-ChildItem -LiteralPath $outPath -File |
    Where-Object {$_.Name -like '*.log' -or $_.Name -like '*.log.exit.txt'} |
    Sort-Object Name | ForEach-Object {Get-HashRow $_.FullName})
  $protectedFinal=@($protectedBefore | ForEach-Object {
    $after=Get-HashRow $_.path
    [ordered]@{path=$_.path;bytes=$_.bytes;sha256=$_.sha256;after_bytes=$after.bytes;
      after_sha256=$after.sha256;unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  if(@($protectedFinal | Where-Object {-not $_.unchanged}).Count){throw 'Protected prior evidence changed'}
  $objectInventory=Get-FileTreeRows $objectPath
  if($objectInventory.Count -ne $translationUnits.Count -or
     @($objectInventory | Where-Object {$_.relative_path -notmatch '^[a-z][a-z0-9_]*\.obj$'}).Count){
    throw 'Fresh object artifact root must contain exactly the24compiled objects'
  }
  $objectBytes=Get-RowByteTotal $objectInventory
  $runtimeRowsBeforeReport=Get-FileTreeRows $outPath
  $runtimeBytesBeforeReport=Get-RowByteTotal $runtimeRowsBeforeReport

  $report=[ordered]@{
    classification='fresh explicit textured GameView probe and shaders; compilation objects and runtime artifacts separately bounded; executable not launched'
    build_label='gameview-textured-material-isolated-unversioned'
    authoritative_repository=$repoRoot
    output_root=$outPath
    object_root=$objectPath
    artifact_root_limits=[ordered]@{maximum_bytes_each=134217728;minimum_free_bytes=402653184;aggregate_limit_claimed=$false}
    object_artifact=[ordered]@{root=$objectPath;files=$objectInventory;file_count=$objectInventory.Count;total_bytes=$objectBytes}
    artifact_storage_before_report=[ordered]@{runtime_bytes=$runtimeBytesBeforeReport;object_bytes=$objectBytes;
      aggregate_bytes=($runtimeBytesBeforeReport+$objectBytes);report_and_final_preservation_excluded=$true}
    translation_unit_count=24
    internal_production_translation_unit_count=23
    translation_units=@($translationUnits.Values)
    nested_inc_closure=$incClosure
    source_provenance=$sourceRows
    source_final_binding=$sourceFinalRows
    objects=$objectRows
    objects_before_link=$objectBeforeLink
    executable=$executableRow
    tools=$toolRows
    compiler_environment=$compilerEnvironment
    generated_headers=$generatedRows
    link_libraries=$linkLibraryRows
    link_libraries_final_binding=$linkLibraryFinal
    ui_dependencies=[ordered]@{rmlui_version='6.3';static_core=$true;bulk_copied=$false;files_before=$dependencyRows;files_after=$dependencyFinal}
    external_header_tree_format='root-relative-sha256-base64-triplets-v1'
    binding_observations=[ordered]@{policy='phase-scoped read-locked hashes; fresh final observations; raw ancestors checked on reuse';maximum_retained_read_locks=8192;stream_buffer_bytes=1;cache_spans_subprocess=$false}
    external_header_trees=[ordered]@{
      trees_before=@(Convert-SdkTreeEvidence $externalHeaderTreesBefore)
      trees_after=@(Convert-SdkTreeEvidence $externalHeaderTreesAfter)}
    runtime_files=$runtimeRows
    command_evidence=$commandEvidence
    commands=@($commandRows)
    flags=$common
    protected_binding=$protectedFinal
    control_kit=[ordered]@{
      classification='separate generated diagnostic-only controls; referenced by path; not staged or promoted as catalog art'
      root=$kitPath
      files=$kitRows
      bulk_staged=$false
    }
    original_art=[ordered]@{root=$originalArtPath;files=$originalArtRows;catalog_files=$originalCatalogRows;bulk_staged=$false}
    material_collection=[ordered]@{root=$materialPath;files=$materialRows;bulk_staged=$false;production_accepted=$false}
    accepted_prerequisites=$prerequisites
    accepted_prerequisites_final=$prerequisitesFinal
    material_neutral_compatibility=[ordered]@{root=$neutralPath;files=$neutralRows;bulk_staged=$false;production_accepted=$false}
    neutral_source_inputs=[ordered]@{files_before=$neutralSourceBefore;files_after=$neutralSourceAfter}
    material_source_inputs=[ordered]@{files_before=$materialSourceBefore;files_after=$materialSourceAfter}
    material_painted_compatibility=[ordered]@{root=$paintedPath;files=$paintedRows;bulk_staged=$false;production_accepted=$false}
    painted_source_inputs=[ordered]@{files_before=$paintedSourceBefore;files_after=$paintedSourceAfter}
    ui_kit=[ordered]@{root=$uiKitPath;files=$uiKitRows;bulk_staged=$false;classification='separate original menu/HUD asset subset with original metadata; normal startup still unverified'}
    shader_compilation=[ordered]@{
      classification='fresh pinned shadercross DXIL and JSON compilation from the exact snapshotted current shader closure'
      source_closure=$shaderSourceRows
      programs=$shaderPrograms
      program_count=$shaderPrograms.Count
      stage_count=@($shaderManifest.shaders).Count
      manifest=Get-HashRow $shaderManifestPath
      artifacts=$shaderArtifactRows
      total_file_count=$shaderArtifactRows.Count
      fresh_shader_compilation=$true
    }
    execution=[ordered]@{
      gameview_textured_material_probe=$false
      game=$false
      gpu=$false
    }
    claims=[ordered]@{
      game_view_integration=$false
      full_engine=$false
      hud=$false
      production_asset_catalog=$false
      fps=$false
      version=$false
    }
  }
  } finally { Close-BindingPhase }
  Write-NewJson (Join-Path $outPath 'build-report.json') $report 12
  Test-Headroom 'after build report'
} finally {
  $protectedAfter=@($protectedBefore | ForEach-Object {
    $after=Get-HashRow $_.path
    [ordered]@{path=$_.path;before_bytes=$_.bytes;after_bytes=$after.bytes;before_sha256=$_.sha256;
      after_sha256=$after.sha256;unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  $afterPath=Join-Path $outPath 'protected-after.json'
  if(!(Test-Path -LiteralPath $afterPath)){Write-NewJson $afterPath $protectedAfter 6}
  if(@($protectedAfter | Where-Object {-not $_.unchanged}).Count){throw 'Protected prior evidence changed'}
  Test-Headroom 'after final preservation'
}

$artifactTotals=@($outPath,$objectPath | ForEach-Object {
  $rows=Get-FileTreeRows $_
  [ordered]@{root=$_;file_count=$rows.Count;total_bytes=(Get-RowByteTotal $rows)}
})
# The final storage ledger lives in the runtime artifact, whose bytes include it.
$storagePath=Join-Path $outPath 'artifact-storage.json'
$storage=[ordered]@{schema=1;artifact_roots=$artifactTotals;maximum_bytes_each=134217728;
  minimum_free_bytes=402653184;observed_free_bytes=[int64](Get-Volume -DriveLetter C).SizeRemaining;
  aggregate_bytes=(Get-RowByteTotal $artifactTotals 'total_bytes');aggregate_limit_claimed=$false}
for($iteration=0;$iteration -lt 8;$iteration++){
  $storageText=$storage | ConvertTo-Json -Depth 6 -Compress
  $storageBytes=[Text.Encoding]::UTF8.GetByteCount($storageText)
  $nextBytes=[int64](($artifactTotals[0].total_bytes)+$storageBytes)
  if($storage.artifact_roots[0].file_count -eq $artifactTotals[0].file_count+1 -and
     $storage.artifact_roots[0].total_bytes -eq $nextBytes){break}
  # Keep the unmodified pre-ledger totals separately; do not cumulatively add.
  $storage.artifact_roots=@([ordered]@{root=$outPath;file_count=$artifactTotals[0].file_count+1;total_bytes=$nextBytes},$artifactTotals[1])
  $storage.aggregate_bytes=$nextBytes+$artifactTotals[1].total_bytes
}
if($iteration -ge 8){throw 'Final artifact storage byte ledger did not settle'}
Assert-NewPath $storagePath
[IO.File]::WriteAllText($storagePath,$storageText,[Text.UTF8Encoding]::new($false))
Test-Headroom 'after final storage ledger'
foreach($root in @($storage.artifact_roots)){
  # Independently total physical FileInfo lengths instead of the hash-row ledger.
  $physicalFiles=@(Get-ChildItem -LiteralPath $root.root -File -Recurse -Force)
  [int64]$physicalBytes=0
  foreach($file in $physicalFiles){
    if(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Reparse artifact file forbidden'}
    if($file.Length -gt [int64]::MaxValue-$physicalBytes){throw 'Physical byte total exceeds Int64'}
    $physicalBytes+=[int64]$file.Length
  }
  if($physicalFiles.Count -ne $root.file_count -or $physicalBytes -ne $root.total_bytes){
    throw 'Actual artifact storage differs from final ledger'
  }
}
Write-Output "Isolated textured GameView/shader artifacts complete without execution: $outPath; objects: $objectPath"
