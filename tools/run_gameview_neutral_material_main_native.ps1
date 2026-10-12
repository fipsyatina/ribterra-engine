param(
  [Parameter(Mandatory=$true)][string]$MainBuildRoot,
  [Parameter(Mandatory=$true)][string]$MaterialRoot,
  [Parameter(Mandatory=$true)][string]$PositiveFirstRoot,
  [Parameter(Mandatory=$true)][string]$PositiveSecondRoot,
  [Parameter(Mandatory=$true)][string]$ColdFirstRoot,
  [Parameter(Mandatory=$true)][string]$ColdSecondRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell7 or newer is required.'}
$repo='C:\Users\fipst\Desktop\ribterra';$build=Join-Path $repo 'build'
$inputs=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$trees=[Collections.Generic.List[object]]::new();$commands=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[string]]::new()
$pendingCase=[Collections.Generic.List[object]]::new();$preservationErrors=[Collections.Generic.List[string]]::new()
$utf8=[Text.UTF8Encoding]::new($false);$minimumObservedFree=[int64]::MaxValue
# Raw path spelling is checked before normalization or reading an input.
foreach($path in @($MainBuildRoot,$MaterialRoot,$PositiveFirstRoot,$PositiveSecondRoot,$ColdFirstRoot,$ColdSecondRoot,$OutputRoot)){
  if(![IO.Path]::IsPathFullyQualified($path) -or $path -cnotmatch '^[A-Za-z]:\\' -or $path.Substring(3).Contains(':') -or
     @($path.Substring(3).Split([char[]]@('\','/')) | Where-Object {$_ -in @('','.','..')}).Count){throw 'Roots must use absolute local canonical path components.'}
}
$mainRoot=[IO.Path]::GetFullPath($MainBuildRoot);$material=[IO.Path]::GetFullPath($MaterialRoot);$out=[IO.Path]::GetFullPath($OutputRoot)
$nativeRoots=@($PositiveFirstRoot,$PositiveSecondRoot,$ColdFirstRoot,$ColdSecondRoot) | ForEach-Object {[IO.Path]::GetFullPath($_)}
function Same([string]$a,[string]$b){return [StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($a),[IO.Path]::GetFullPath($b))}
function No-Reparse([string]$path){
  if(![IO.Path]::IsPathFullyQualified($path)){throw "Path must be absolute: $path"}
  $cursor=[IO.Path]::GetFullPath($path)
  while($true){
    if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
      throw "Reparse path forbidden: $cursor"
    }
    $parent=Split-Path $cursor;if(!$parent -or $parent -eq $cursor){break};$cursor=$parent
  }
}
function Inside([string]$path,[string]$root){
  if(![IO.Path]::GetFullPath($path).StartsWith(([IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw "Path escapes ${root}: $path"
  };No-Reparse $path
}
function Row([string]$path){
  No-Reparse $path;$item=Get-Item -LiteralPath $path -Force
  if($item.PSIsContainer){throw "Expected bound file: $path"}
  return [pscustomobject][ordered]@{path=$item.FullName;bytes=[int64]$item.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
}
function Bind([object]$row){
  if([string]$row.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or [int64]$row.bytes -lt 0){throw 'Invalid byte/hash binding.'}
  $actual=Row ([string]$row.path)
  if($actual.bytes -ne $row.bytes -or $actual.sha256 -ne $row.sha256){throw "Changed input: $($row.path)"}
  if($inputs.ContainsKey($actual.path)){
    if($inputs[$actual.path].sha256 -ne $actual.sha256 -or $inputs[$actual.path].bytes -ne $actual.bytes){throw 'Conflicting input binding.'}
  }else{$inputs.Add($actual.path,$actual)}
}
function Expand-ExternalHeaders([object]$report){
  if($report.external_header_tree_format -cne 'root-relative-sha256-base64-triplets-v1'){
    throw 'Unknown or missing external SDK header format.'
  }
  $result=[ordered]@{}
  foreach($side in @('trees_before','trees_after')){
    $encoded=@($report.external_header_trees.$side)
    if($encoded.Count -ne 4){throw 'Four external SDK trees required per observation.'}
    $expanded=[Collections.Generic.List[object]]::new()
    foreach($tree in $encoded){
      if($tree.root -isnot [string] -or ![IO.Path]::IsPathFullyQualified($tree.root) -or $tree.files -isnot [array]){
        throw 'Invalid external SDK tree root or file array.'
      }
      No-Reparse $tree.root
      $rows=[Collections.Generic.List[object]]::new()
      $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
      foreach($tuple in $tree.files){
        if($tuple -isnot [array] -or $tuple.Count -ne 3 -or $tuple[0] -isnot [string] -or
           ($tuple[1] -isnot [int] -and $tuple[1] -isnot [long]) -or $tuple[1] -lt 0 -or $tuple[2] -isnot [string]){
          throw 'SDK header tuple must contain relative path, nonnegative integer bytes and canonical base64 SHA256.'
        }
        $relative=$tuple[0]
        if(!$relative -or $relative.Contains('\') -or $relative.Contains(':') -or [IO.Path]::IsPathRooted($relative) -or
           @($relative.Split('/') | Where-Object {$_ -in @('','.','..')}).Count -or !$seen.Add($relative)){
          throw 'Unsafe or duplicate SDK relative path.'
        }
        $path=[IO.Path]::GetFullPath((Join-Path $tree.root $relative));Inside $path $tree.root
        if([IO.Path]::GetRelativePath($tree.root,$path).Replace('\','/') -cne $relative){throw 'Noncanonical SDK relative path.'}
        if($tuple[2] -cnotmatch '^[A-Za-z0-9+/]{43}=$'){throw 'Invalid SDK base64 SHA256 spelling.'}
        $hash=[Convert]::FromBase64String($tuple[2])
        if($hash.Length -ne 32 -or [Convert]::ToBase64String($hash) -cne $tuple[2]){throw 'Noncanonical SDK SHA256 bytes.'}
        $rows.Add([pscustomobject]@{relative_path=$relative;path=$path;bytes=[int64]$tuple[1];sha256=[Convert]::ToHexString($hash)})
      }
      $expanded.Add([pscustomobject]@{root=$tree.root;files=$rows.ToArray()})
    }
    $result[$side]=$expanded.ToArray()
  }
  return [pscustomobject]$result
}
function Collect([object]$value){
  if($null -eq $value -or $value -is [string] -or $value.GetType().IsValueType){return}
  if($value -is [Collections.IEnumerable] -and $value -isnot [Collections.IDictionary]){foreach($item in $value){Collect $item};return}
  $keys=@($value.PSObject.Properties.Name)
  if($keys -contains 'external_header_trees' -or $keys -contains 'external_header_tree_format'){
    $decoded=Expand-ExternalHeaders $value
    foreach($tree in @($decoded.trees_before)+@($decoded.trees_after)){
      foreach($row in $tree.files){Bind $row}
    }
  }
  if($keys -contains 'bytes' -and $keys -contains 'sha256'){
    if($keys -contains 'unchanged' -and $value.unchanged -ne $true){throw 'Recorded preservation failed.'}
    if($keys -contains 'after_bytes' -and $value.after_bytes -ne $value.bytes){throw 'Recorded bytes changed.'}
    if($keys -contains 'after_sha256' -and $value.after_sha256 -ne $value.sha256){throw 'Recorded hash changed.'}
    foreach($key in @('path','repository_path','snapshot_path','origin','destination')){
      if($keys -contains $key){Bind ([ordered]@{path=$value.$key;bytes=$value.bytes;sha256=$value.sha256})}
    }
  }
  foreach($property in $value.PSObject.Properties){Collect $property.Value}
}
function Inventory([string]$root,[object]$aliases=@{}){
  No-Reparse $root;$rows=[Collections.Generic.List[object]]::new();$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
  while($pending.Count){foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
    $relative=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')
    if($aliases.Contains($relative)){
      if(!($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !$item.PSIsContainer){throw 'Owned cold alias lost junction identity.'}
      $target=$item.ResolveLinkTarget($false);No-Reparse $aliases[$relative]
      if(!$target -or !(Same $target.FullName $aliases[$relative])){throw 'Owned cold alias target differs.'};continue
    }
    No-Reparse $item.FullName;if($item.PSIsContainer){$pending.Push($item.FullName);continue};$r=Row $item.FullName
    $rows.Add([pscustomobject][ordered]@{relative_path=$relative;path=$r.path;bytes=$r.bytes;sha256=$r.sha256})
  }};return @($rows | Sort-Object path)
}
function Match-Tree([string]$root,[object[]]$expected,[object]$aliases=@{}){
  $actual=@(Inventory $root $aliases);$ordered=@($expected | Sort-Object path)
  if($actual.Count -ne $ordered.Count){throw "Tree membership changed: $root"}
  for($i=0;$i -lt $actual.Count;$i++){
    $r=$ordered[$i];Inside ([string]$r.path) $root
    if(!(Same $actual[$i].path $r.path) -or $actual[$i].relative_path -ne $r.relative_path -or
       $actual[$i].bytes -ne $r.bytes -or $actual[$i].sha256 -ne $r.sha256){throw "Tree payload changed: $root"}
  }
}
function Protect-Tree([string]$root,[object[]]$expected,[object]$aliases=@{}){
  No-Reparse $root
  if((Same $root $out) -or $out.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Output overlaps protected input tree.'
  }
  Match-Tree $root $expected $aliases
  if(@($trees | Where-Object {Same $_.root $root}).Count -eq 0){$trees.Add([pscustomobject]@{root=$root;files=@($expected | Sort-Object path);aliases=$aliases})}
  foreach($r in $expected){Bind $r}
}
function Assert-Sequence([object[]]$a,[object[]]$b,[string]$label){
  if($a.Count -ne $b.Count){throw "$label count differs."}
  for($i=0;$i -lt $a.Count;$i++){if([string]$a[$i] -cne [string]$b[$i]){throw "$label differs at $i."}}
}
function Assert-Preference-Rows([string]$root,[object[]]$rows){
  if($rows.Count -ne 36){throw 'Exactly36 ordinary preference files required.'}
  $exact=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  $folded=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $exactPaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  $foldedPaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach($row in $rows){
    Inside ([string]$row.path) $root
    if(!$exact.Add([string]$row.relative_path) -or !$folded.Add([string]$row.relative_path) -or
       !$exactPaths.Add([string]$row.path) -or !$foldedPaths.Add([string]$row.path)){
      throw 'Ordinary preference filenames must be exact and casefold unique.'
    }
  }
}
function Headroom{
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  $script:minimumObservedFree=[Math]::Min($script:minimumObservedFree,$free)
  if($free -lt 402653184){throw 'Headroom below384MiB.'}
  if(Test-Path -LiteralPath $out){$bytes=[int64]0;foreach($r in @(Inventory $out)){$bytes += $r.bytes};if($bytes -gt 134217728){throw 'Output exceeds128MiB.'}}
}
function Write-New([string]$path,[byte[]]$bytes){
  Headroom;$used=[int64]0;foreach($r in @(Inventory $out)){$used += $r.bytes}
  if($bytes.LongLength -gt 134217728-$used -or [int64](Get-Volume -DriveLetter C).SizeRemaining -lt 402653184+$bytes.LongLength){throw 'Pending evidence exceeds128MiB/384MiB guards.'}
  Inside $path $out;$stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Json-Bytes([object]$value){return ,$utf8.GetBytes(($value | ConvertTo-Json -Depth 40 -Compress)+"`n")}
function Json-New([string]$path,[object]$value){Write-New $path (Json-Bytes $value)}
function Pending-Row([string]$path,[byte[]]$bytes){return [pscustomobject][ordered]@{path=$path;bytes=[int64]$bytes.LongLength;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))}}
function Copy-Code([string]$path,[string]$name){
  $before=Row $path;Bind $before;$destination=Join-Path $out "source/$name"
  Write-New $destination ([IO.File]::ReadAllBytes($path));$after=Row $destination
  if($before.sha256 -ne $after.sha256){throw 'Source snapshot differs.'}
  return [ordered]@{origin=$path;destination=$destination;bytes=$after.bytes;sha256=$after.sha256}
}
function Profile([string]$id){
  $directories=@(Get-ChildItem -LiteralPath $out -Directory -Force | Where-Object {$_.Name -like "$id.png.test-profile-*"})
  if($directories.Count -ne 1 -or $directories[0].Name -cnotmatch ('^'+[regex]::Escape($id)+ '\.png\.test-profile-[0-9a-f]{32}$')){throw "Expected one source-defined profile for $id"}
  $root=$directories[0].FullName;No-Reparse $root
  $files=@(Inventory $root);$children=@(Get-ChildItem -LiteralPath $root -Force)
  if($files.Count -ne 0 -or $children.Count -ne 1 -or !$children[0].PSIsContainer -or $children[0].Name -ne 'climate-v1' -or
     @(Get-ChildItem -LiteralPath $children[0].FullName -Force).Count -ne 0){throw "Automatic case wrote unexpected settings/save/profile files: $id"}
  No-Reparse $children[0].FullName
  return [ordered]@{root=$root;relative_path=$directories[0].Name;files=$files;directories=@('climate-v1')}
}


foreach($root in @($mainRoot,$material,$out)+$nativeRoots){
  if(!(Same (Split-Path $root) $build) -or (Split-Path $root -Leaf) -cnotmatch '^[a-z][a-z0-9-]{0,127}$'){throw 'Each caller root must be a portable immediate repository build child.'};No-Reparse $root
}
$rootSet=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($root in @($mainRoot,$material,$out)+$nativeRoots){if(!$rootSet.Add($root)){throw 'Caller roots must be distinct.'}}
if(Test-Path -LiteralPath $out){throw 'Fresh output root must not exist.'}
$reportPath=Join-Path $mainRoot 'main-build-report.json';$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if($report.complete -ne $true -or !(Same $report.output_root $mainRoot) -or !(Same $report.authoritative_repository $repo) -or
   $report.compiled_translation_unit_count -ne 1 -or $report.reused_production_object_count -ne 23 -or $report.shader_file_count -ne 93 -or
   $report.shader_compilation -ne $false -or $report.execution.game -ne $false -or $report.execution.gpu -ne $false){throw 'Reviewed main compile/link cohort is required.'}
$acceptedRoot=[string]$report.accepted_build_root;$objectRoot=[string]$report.accepted_object_root
foreach($root in @($acceptedRoot,$objectRoot)){
  No-Reparse $root;if(!(Same (Split-Path $root) $build) -or $rootSet.Contains($root)){throw 'Referenced build/object root conflicts with caller inputs.'}
}
if(Same $acceptedRoot $objectRoot){throw 'Build/Object roots must be distinct.'}
if(!(Same $report.accepted_build_report.path (Join-Path $acceptedRoot 'build-report.json'))){throw 'Accepted report path differs.'}
$accepted=Get-Content -LiteralPath $report.accepted_build_report.path -Raw | ConvertFrom-Json
if(!(Same $accepted.output_root $acceptedRoot) -or !(Same $accepted.object_root $objectRoot) -or
   $accepted.build_label -ne 'gameview-neutral-material-isolated-unversioned' -or $accepted.translation_unit_count -ne 24 -or
   $accepted.internal_production_translation_unit_count -ne 23 -or @($accepted.commands).Count -ne 122 -or
   @($accepted.commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Fresh strict24TU/122-command neutral source cohort is required.'}
if(!(Same $accepted.material_collection.root $material) -or !(Same $accepted.original_art.root (Join-Path $repo 'assets'))){throw 'Original/neutral asset identity differs.'}
$exe=Join-Path $mainRoot 'Ribterra.exe'
if(!(Same $report.executable.path $exe) -or !(Same $report.main_source.path (Join-Path $repo 'src/client/main.cpp')) -or
   $report.main_source.sha256 -ne 'E0F4C8FE69DEB7691A4945A95654FFBF08FFAA7BEFD8F3CB186C45004D8415ED'){throw 'Actual reviewed diagnostic main source/EXE differs.'}
$helper=@($report.source_provenance | Where-Object {(Split-Path $_.origin -Leaf) -eq 'build_gameview_neutral_material_main_isolated.ps1'})
if($helper.Count -ne 1 -or $helper[0].sha256 -ne 'D8DD89D638766C5C3AD97BF9D2495108188201ECBD5AC00A55DB1BB657B151CE'){throw 'Reviewed actual main helper differs.'}
$objectNames=@('foundation','economy','lobby','atomic_file','platform','world','weather','skills','world_save','game_server','gns_transport',
  'protocol','network_session','asset_catalog','texture_atlas','localization','renderer2d','contact_shadow','sky_shadow','view','client_session','game_art','game_ui')
$production=@($objectNames | ForEach-Object {Join-Path $objectRoot "$_.obj"})
Assert-Sequence $production @($report.reused_objects.path) 'Exact23 production object reuse'
if(!(Same $report.excluded_object (Join-Path $objectRoot 'gameview_neutral_material_probe.obj')) -or
   !(Same $report.compiled_object.path (Join-Path $mainRoot 'obj/main.obj'))){throw 'Main/probe object identity differs.'}
$externalRoots=@((Join-Path $repo '.deps/SDL3-3.4.16/include'),(Join-Path $repo '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repo 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),(Join-Path $repo 'vcpkg_installed/x64-windows-release/include'))
$flags=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-',
  '/GR','/Zc:__cplusplus','/DRMLUI_STATIC_LIB','/DRIBTERRA_PROBE_BUILD="gameview-neutral-material-isolated-unversioned"',
  ('/I'+(Join-Path $acceptedRoot 'source/include')),('/I'+(Join-Path $acceptedRoot 'generated')))+@($externalRoots | ForEach-Object {'/external:I'+$_})+@('/external:W0')
Assert-Sequence $flags @($accepted.flags) 'Accepted strict flags';Assert-Sequence $flags @($report.flags) 'Actual main strict flags'
if(@($report.commands).Count -ne 2 -or @($report.commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Actual main compile/link commands differ.'}
$compile=$report.commands[0];$link=$report.commands[1]
Assert-Sequence ($flags+@('/c',(Join-Path $mainRoot 'source/src/client/main.cpp'),('/Fo'+(Join-Path $mainRoot 'obj/main.obj')))) @($compile.arguments) 'Main compile argv'
Assert-Sequence (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',('/out:'+$exe),(Join-Path $mainRoot 'obj/main.obj'))+
  $production+@($accepted.link_libraries.path)) @($link.arguments) 'Main link argv'
foreach($pair in @(@($compile,$report.toolchain.compiler,'compile-main'),@($link,$report.toolchain.linker,'link-Ribterra'))){
  if(!(Same $pair[0].executable $pair[1].path) -or !(Same $pair[0].working_directory $mainRoot) -or
     !(Same $pair[0].log.path (Join-Path $mainRoot ($pair[2]+'.log'))) -or
     (Get-Content -LiteralPath $pair[0].exit_sidecar.path -Raw).Trim() -ne '0'){throw 'Actual main command tool/cwd/log/exit differs.'}
}
foreach($key in @('include','lib','vs_install_dir','vc_tools_version','windows_sdk_dir','windows_sdk_version')){
  if(![string]$report.compiler_environment.$key -or [string]$report.compiler_environment.$key -ne [string]$accepted.compiler_environment.$key){throw 'Compiler search environment changed.'}
}
Collect $report;Collect $accepted
# The accepted 9A post-build reader fixes ordering/cache witnesses without replacing
# the original compiled 8A reader or any accepted build/native artifact.
$reviewReader=Row (Join-Path $repo 'tests/gameview_neutral_material_evidence_review_tests.py')
if($reviewReader.sha256 -ne '9A134D1A23CDF986E1CB6D20F229B9427AE21ADB483325341B191435DFB4A1DD'){throw 'Reviewed post-build neutral reader differs.'}
Bind $reviewReader
foreach($path in @($reportPath,$report.immutable_input_bindings.path,$report.immutable_input_final_binding.path,$report.accepted_build_tree.path,
  $report.accepted_object_tree.path,$report.preservation_after.path,$report.accepted_artifact_storage.path)){
  Bind (Row $path);Collect (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}
$preserved=Get-Content -LiteralPath $report.preservation_after.path -Raw | ConvertFrom-Json
if($preserved.complete -ne $true -or @($preserved.errors).Count){throw 'Actual main build preservation failed.'}
Protect-Tree $mainRoot @($report.output_files_before_report+[pscustomobject]@{path=$reportPath;relative_path='main-build-report.json';bytes=(Row $reportPath).bytes;sha256=(Row $reportPath).sha256})
Protect-Tree $acceptedRoot @(Get-Content -LiteralPath $report.accepted_build_tree.path -Raw | ConvertFrom-Json)
Protect-Tree $objectRoot @(Get-Content -LiteralPath $report.accepted_object_tree.path -Raw | ConvertFrom-Json)
foreach($tree in @($accepted.control_kit,$accepted.ui_kit,$accepted.original_art,$accepted.material_collection,$accepted.material_painted_compatibility)){Protect-Tree $tree.root @($tree.files)}
$currentInclude=Join-Path $repo 'include'
$includeRows=@(foreach($row in $accepted.source_provenance){if(([string]$row.relative_path).StartsWith('include/')){
  [pscustomobject]@{relative_path=([string]$row.relative_path).Substring(8);path=$row.repository_path;bytes=$row.bytes;sha256=$row.sha256}
}})
if(!$includeRows.Count){throw 'Accepted whole current include membership is missing.'}
Protect-Tree $currentInclude $includeRows
$decoded=Expand-ExternalHeaders $accepted
Assert-Sequence $externalRoots @($decoded.trees_before.root) 'External SDK before roots';Assert-Sequence $externalRoots @($decoded.trees_after.root) 'External SDK after roots'
foreach($tree in @($decoded.trees_before)+@($decoded.trees_after)){Protect-Tree $tree.root @($tree.files)}
foreach($source in @('neutral','painted')){
  foreach($side in @('files_before','files_after')){foreach($row in $report.producer_source_inputs.$source.$side){Bind $row}}
}
$nativeEvidence=[Collections.Generic.List[object]]::new()
$materialMetadata=Get-Content -LiteralPath (Join-Path $material 'art/material-collection.json') -Raw | ConvertFrom-Json
$house=@($materialMetadata.items | Where-Object {$_.legacy_key -eq 'building.house.ready'})
if($house.Count -ne 1){throw 'One authored neutral ready-house source is required.'}
for($i=0;$i -lt 4;$i++){
  $root=$nativeRoots[$i];$profile=if($i -lt 2){'positive'}else{'cold'}
  $executionPath=Join-Path $root 'native-execution.json';$nativeReport=Join-Path $root 'neutral-material-report.json'
  $execution=Get-Content -LiteralPath $executionPath -Raw | ConvertFrom-Json;$native=Get-Content -LiteralPath $nativeReport -Raw | ConvertFrom-Json
  if($execution.accepted -ne $true -or $execution.exit_code -ne 0 -or $execution.actual_subprocess -ne $true -or
     $execution.native_created_output -ne $true -or $execution.bound_inputs_unchanged -ne $true -or @($execution.integrity_errors).Count -ne 0 -or
     !(Same $execution.build_root $acceptedRoot) -or !(Same $execution.object_root $objectRoot) -or !(Same $execution.material_collection_root $material) -or
     $execution.profile -cne $profile -or $native.complete -ne $true -or $native.schema -ne 1 -or $native.profile -cne $profile){throw 'Accepted actual neutral repeat linkage differs.'}
  $aliases=@{}
  if($profile -eq 'cold'){
    $aliases['invalid-root-reparse']=$material
    $aliases['invalid-notice-reparse/notice-alias']=Split-Path (Join-Path $material $house[0].albedo_source.provenance.notice_path)
    $aliases['invalid-palette-internal-reparse/palette-alias']=Split-Path (Join-Path $material $house[0].albedo_source.palette_path)
    if(@($native.deterministic.retained_junctions).Count -ne 3){throw 'Exact three owned cold aliases required.'}
    foreach($alias in $native.deterministic.retained_junctions){if(!$aliases.Contains($alias.path) -or !(Same $alias.target $aliases[$alias.path]) -or
      $alias.owned -ne $true -or $alias.target_exact -ne $true -or $alias.followed_in_inventory -ne $false){throw 'Cold alias declaration differs.'}}
  }
  Collect $execution;Bind (Row $executionPath);Bind (Row $nativeReport)
  $final=Get-Content -LiteralPath $execution.input_final_binding.path -Raw | ConvertFrom-Json
  Collect $final;Protect-Tree $root @(Inventory $root $aliases) $aliases
  $nativeEvidence.Add([pscustomobject]@{profile=$profile;root=$root;execution=Row $executionPath;report=Row $nativeReport;aliases=$aliases})
}
foreach($root in @($mainRoot,$acceptedRoot,$objectRoot)+$nativeRoots){$sum=[int64]0;foreach($row in @(Inventory $root $(if($root -in $nativeRoots){($nativeEvidence | Where-Object {$_.root -eq $root}).aliases}else{@{}}))){$sum += $row.bytes};if($sum -gt 134217728){throw 'Referenced individual artifact root exceeds128MiB.'}}
$ordinaryPrefs=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Ribterra/Ribterra';No-Reparse $ordinaryPrefs
$prefsExisted=Test-Path -LiteralPath $ordinaryPrefs -PathType Container
if(!$prefsExisted){throw 'Existing ordinary preference root required.'}
$prefsBefore=@(Inventory $ordinaryPrefs);Assert-Preference-Rows $ordinaryPrefs $prefsBefore;foreach($row in $prefsBefore){Bind $row}
Headroom;New-Item -ItemType Directory -Path $out | Out-Null;New-Item -ItemType Directory -Path (Join-Path $out 'source') | Out-Null
$snapshots=@(Copy-Code $PSCommandPath 'run_gameview_neutral_material_main_native.ps1';Copy-Code (Join-Path $repo 'tests/gameview_neutral_material_main_evidence_tests.py') 'gameview_neutral_material_main_evidence_tests.py')
$before=@($inputs.Values | Sort-Object path);Json-New (Join-Path $out 'input-bindings.json') $before;Json-New (Join-Path $out 'input-trees-before.json') $trees.ToArray()
$completed=$false;$boundInputsUnchanged=$true
try{
  foreach($id in @('main-menu','main-material-menu','main-world','main-material-world','main-neutral-house')){
    Headroom;$capture=Join-Path $out "$id.png";$info=Join-Path $out "$id.renderer-info.json"
    $argv=@('--catalog-assets',(Join-Path $repo 'assets'),'--renderer-info',$info)
    if($id -eq 'main-neutral-house'){$argv+=@('--diagnostic-house-capture','--capture',$capture,'--material-collection',$material)}
    else{if($id.EndsWith('menu')){$argv+=@('--menu-capture',$capture)}else{$argv+=@('--demo','--capture',$capture)}
      if($id.StartsWith('main-material-')){$argv+=@('--material-collection',$material)}}
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.WorkingDirectory=$mainRoot;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true;foreach($argument in $argv){$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start;$began=[DateTimeOffset]::UtcNow;$timer=[Diagnostics.Stopwatch]::StartNew()
    $code=$null;$timedOut=$false;$stdout='';$stderr='';$launchError='';$pidRecorded=$null
    try{
      if(!$process.Start()){throw 'Process.Start returned false.'};$pidRecorded=$process.Id
      $readOut=$process.StandardOutput.ReadToEndAsync();$readError=$process.StandardError.ReadToEndAsync()
      if(!$process.WaitForExit(120000)){$timedOut=$true;$process.Kill();$process.WaitForExit()}
      $stdout=$readOut.GetAwaiter().GetResult();$stderr=$readError.GetAwaiter().GetResult();$code=$process.ExitCode
    }catch{$launchError=$_.Exception.Message;if($null -ne $pidRecorded){try{if(!$process.HasExited){$process.Kill();$process.WaitForExit()}}catch{$launchError+=' ; owned process termination failed: '+$_.Exception.Message}}}
    finally{$timer.Stop();$process.Dispose()}
    $log=Join-Path $out "$id.log";$sidecar=Join-Path $out "$id.log.exit.txt"
    [byte[]]$logBytes=$utf8.GetBytes("STDOUT`n$stdout`nSTDERR`n$stderr")
    [byte[]]$exitBytes=$utf8.GetBytes($(if($null -eq $code){"unavailable`n"}else{"$code`n"}))
    $row=[ordered]@{id=$id;executable=Row $exe;arguments=$argv;actual_argv=@($exe)+$argv;working_directory=$mainRoot;
      started_utc=$began.ToString('o');ended_utc=[DateTimeOffset]::UtcNow.ToString('o');elapsed_milliseconds=$timer.Elapsed.TotalMilliseconds;
      diagnostic_duration_scope='whole actual ordinary process; not FPS/GPU timing';process_id=$pidRecorded;actual_subprocess=($null -ne $pidRecorded);
      exit_code=$code;timed_out=$timedOut;launch_error=$launchError;log=Pending-Row $log $logBytes;exit_sidecar=Pending-Row $sidecar $exitBytes}
    $commands.Add($row)
    $pendingCase.Add([pscustomobject]@{name="$id.log";bytes=$logBytes});$pendingCase.Add([pscustomobject]@{name="$id.log.exit.txt";bytes=$exitBytes})
    $pendingCase.Add([pscustomobject]@{name="case-$id.json";bytes=[byte[]](Json-Bytes $row)})
    foreach($file in $pendingCase){Write-New (Join-Path $out $file.name) $file.bytes};$pendingCase.Clear()
    if($null -eq $code -or $code -ne 0 -or $timedOut -or $launchError){throw "Actual main case failed: $id ; exit=$code ; $launchError"}
    $row.capture=Row $capture;$row.renderer_info=Row $info;$row.profile=Profile $id
    if($id -eq 'main-neutral-house'){
      $row.removed_capture=Row (Join-Path $out "$id.house-removed.png");$row.house_report=Row (Join-Path $out "$id.house-diagnostic.json")
      $diagnostic=Get-Content -LiteralPath $row.house_report.path -Raw | ConvertFrom-Json
      if($diagnostic.complete -ne $true -or $diagnostic.schema -ne 1 -or $diagnostic.mode -cne 'diagnostic-house-capture'){throw 'Actual House diagnostic did not complete.'}
    }
    Headroom
  };$completed=$true
}catch{$errors.Add($_.Exception.Message)}finally{
  $after=@(foreach($r in $before){try{$a=Row $r.path;[pscustomobject]@{path=$r.path;bytes=$r.bytes;sha256=$r.sha256;after_bytes=$a.bytes;after_sha256=$a.sha256;unchanged=($r.bytes -eq $a.bytes -and $r.sha256 -eq $a.sha256)}}catch{[pscustomobject]@{path=$r.path;bytes=$r.bytes;sha256=$r.sha256;unchanged=$false;error=$_.Exception.Message}}})
  if(@($after | Where-Object {$_.unchanged -ne $true}).Count){$boundInputsUnchanged=$false;$preservationErrors.Add('Protected input bytes changed.')}
  foreach($tree in $trees){try{Match-Tree $tree.root @($tree.files) $tree.aliases}catch{$boundInputsUnchanged=$false;$preservationErrors.Add($_.Exception.Message)}}
  $prefsAfter=@();$prefsExistAfter=Test-Path -LiteralPath $ordinaryPrefs -PathType Container
  try{if(!$prefsExisted -or !$prefsExistAfter){throw 'Ordinary preference root must exist before and after.'};$prefsAfter=@(Inventory $ordinaryPrefs);Assert-Preference-Rows $ordinaryPrefs $prefsAfter;Match-Tree $ordinaryPrefs $prefsBefore}
  catch{$boundInputsUnchanged=$false;$preservationErrors.Add($_.Exception.Message)}
  foreach($message in $preservationErrors){$errors.Add($message)}
  try{Headroom}catch{$errors.Add($_.Exception.Message)}
}
function Total([object[]]$rows){[int64]$value=0;foreach($r in $rows){if($r.bytes -lt 0 -or $r.bytes -ne [int64]$r.bytes -or $r.bytes -gt [int64]::MaxValue-$value){throw 'Invalid storage byte total.'};$value += [int64]$r.bytes};return $value}
$nativeInventoryComplete=$true;$nativeRows=@()
try{$nativeRows=@(Inventory $out)}catch{$nativeInventoryComplete=$false;$errors.Add($_.Exception.Message)}
$nativeBytes=Total $nativeRows
function Planned-Preservation([string]$evidenceRoot,[byte[]]$finalBytes){
  return [ordered]@{complete=$boundInputsUnchanged;errors=@($preservationErrors);input_final_binding=Pending-Row (Join-Path $evidenceRoot 'input-final-hashes.json') $finalBytes;
    protected_tree_count=$trees.Count;ordinary_preferences=[ordered]@{root=$ordinaryPrefs;existed_before=$prefsExisted;files_before=$prefsBefore;existed_after=$prefsExistAfter;files_after=$prefsAfter}}
}
function Planned-Ledger([string]$evidenceRoot,[bool]$separate,[object[]]$rows,[byte[]]$finalBytes,[byte[]]$preservedBytes){
  $value=[ordered]@{schema=1;complete=($completed -and $errors.Count -eq 0 -and !$separate);accepted=($completed -and $errors.Count -eq 0 -and !$separate);bound_inputs_unchanged=$boundInputsUnchanged;
  classification='actual ordinary main automatic controls plus instrumented real House construction/body removal; no untouched desktop-input/reference/FPS acceptance';
  repository=$repo;main_build_root=$mainRoot;accepted_build_root=$acceptedRoot;accepted_object_root=$objectRoot;material_root=$material;output_root=$out;
  main_build_report=Row $reportPath;accepted_neutral_repeats=$nativeEvidence.ToArray();source_provenance=$snapshots;input_bindings=Row (Join-Path $out 'input-bindings.json');
  input_trees=Row (Join-Path $out 'input-trees-before.json');input_final_binding=Pending-Row (Join-Path $evidenceRoot 'input-final-hashes.json') $finalBytes;preservation=Pending-Row (Join-Path $evidenceRoot 'preservation-after.json') $preservedBytes;
  commands=$commands.ToArray();process_count=$commands.Count;automatic_contract=[ordered]@{window=@(1440,900);capture_frame_index=2;exit_after_frames=3;seed=70421;demo_preadvances=1700;
    house_advance_limit=12000;profiles='capture filename + .test-profile- + secure_token(16), retained empty climate-v1';world_frame_repeat_claimed=$false;house_png_repeat_claimed=$false};
  native_output_before_ledger=$rows;integrity_errors=$errors.ToArray();claims=[ordered]@{ordinary_startup_captures=($completed -and $errors.Count -eq 0 -and !$separate);
    instrumented_house_visibility=($completed -and $errors.Count -eq 0 -and !$separate);untouched_startup_visibility=$false;fps=$null;gpu_execution_ms=$null;
    default_package=$false;production_neutral_art=$false;reference_fidelity=$false;full_engine=$false};
  storage=[ordered]@{maximum_output_bytes=134217728;minimum_free_bytes=402653184;minimum_observed_free_bytes=$minimumObservedFree;output_bytes_before_ledger=(Total $rows);
    aggregate_limit_claimed=$false;bulk_art_copied=$false;failed_roots_retained=$true}}
  if($separate){
    $value.evidence_output_root=$evidenceRoot;$value.separate_failure_evidence=$true
    $value.native_output_by_reference=[ordered]@{root=$out;inventory_complete=$nativeInventoryComplete;
      file_count=$(if($nativeInventoryComplete){$nativeRows.Count}else{$null});total_bytes=$(if($nativeInventoryComplete){$nativeBytes}else{$null});
      within_limit=($nativeInventoryComplete -and $nativeBytes -le 134217728);files=$nativeRows}
  }
  return $value
}
function Bundle([string]$evidenceRoot,[bool]$separate){
  [byte[]]$finalBytes=Json-Bytes $after;[byte[]]$preservedBytes=Json-Bytes (Planned-Preservation $evidenceRoot $finalBytes)
  $planned=[Collections.Generic.List[object]]::new()
  foreach($file in $pendingCase){$planned.Add([pscustomobject]@{name=$file.name;bytes=$file.bytes})}
  $planned.Add([pscustomobject]@{name='input-final-hashes.json';bytes=$finalBytes});$planned.Add([pscustomobject]@{name='preservation-after.json';bytes=$preservedBytes})
  $rows=[Collections.Generic.List[object]]::new();if(!$separate){foreach($row in $nativeRows){$rows.Add($row)}}
  foreach($file in $planned){$row=Pending-Row (Join-Path $evidenceRoot $file.name) $file.bytes;$rows.Add([pscustomobject]@{relative_path=$file.name;path=$row.path;bytes=$row.bytes;sha256=$row.sha256})}
  $ledger=Planned-Ledger $evidenceRoot $separate @($rows | Sort-Object path) $finalBytes $preservedBytes
  $planned.Add([pscustomobject]@{name='main-native-execution.json';bytes=[byte[]](Json-Bytes $ledger)})
  [int64]$total=0;foreach($file in $planned){$total += [int64]$file.bytes.LongLength}
  return [pscustomobject]@{files=$planned.ToArray();total_bytes=$total;ledger=$ledger}
}
function Fits([int64]$existing,[int64]$pending){return ($existing -ge 0 -and $existing -le 134217728 -and $pending -ge 0 -and $pending -le 134217728-$existing -and [int64](Get-Volume -DriveLetter C).SizeRemaining -ge 402653184+$pending)}
$evidenceRoot=$out;$separate=$false;$bundle=Bundle $evidenceRoot $separate
if(!$nativeInventoryComplete -or $pendingCase.Count -or !(Fits $nativeBytes $bundle.total_bytes)){
  $errors.Add('Native inventory/case publication/budget failed; separate bounded failure evidence required.')
  $separate=$true;$evidenceRoot=$out+'-failure-evidence';No-Reparse $evidenceRoot
  if(!(Same (Split-Path $evidenceRoot) $build) -or (Split-Path $evidenceRoot -Leaf) -cnotmatch '^[a-z][a-z0-9-]{0,127}$' -or (Test-Path -LiteralPath $evidenceRoot)){throw 'Failure root must be a fresh portable immediate build child; all files retained.'}
  foreach($tree in $trees){if((Same $tree.root $evidenceRoot) -or $tree.root.StartsWith(($evidenceRoot+'\'),[StringComparison]::OrdinalIgnoreCase) -or $evidenceRoot.StartsWith(($tree.root+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Failure root overlaps protected input.'}}
  if($pendingCase.Count){
    if($pendingCase.Count -ne 3 -or !$commands.Count){throw 'Pending actual case evidence shape differs; all files retained.'}
    $last=$commands[$commands.Count-1]
    $last.log=Pending-Row (Join-Path $evidenceRoot $pendingCase[0].name) $pendingCase[0].bytes
    $last.exit_sidecar=Pending-Row (Join-Path $evidenceRoot $pendingCase[1].name) $pendingCase[1].bytes
    $pendingCase[2].bytes=[byte[]](Json-Bytes $last)
  }
  $bundle=Bundle $evidenceRoot $separate;if(!(Fits 0 $bundle.total_bytes)){throw 'Failure evidence exceeds128MiB/free384MiB; all files retained.'}
  New-Item -ItemType Directory -Path $evidenceRoot -ErrorAction Stop | Out-Null;No-Reparse $evidenceRoot
  if(@(Inventory $evidenceRoot).Count){throw 'Failure root is not empty; all files retained.'}
}
if(!(Fits $(if($separate){0L}else{$nativeBytes}) $bundle.total_bytes)){throw 'Evidence budget changed before exclusive publication; all files retained.'}
foreach($file in $bundle.files){
  $path=Join-Path $evidenceRoot $file.name;Inside $path $evidenceRoot
  $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($file.bytes,0,$file.bytes.Length)}finally{$stream.Dispose()}
  $actual=Row $path;$expected=Pending-Row $path $file.bytes;if($actual.bytes -ne $expected.bytes -or $actual.sha256 -ne $expected.sha256){throw 'Published evidence differs; files retained.'}
}
$published=@(Inventory $evidenceRoot);if((Total $published) -gt 134217728 -or ($separate -and ($published.Count -ne $bundle.files.Count -or (Total $published) -ne $bundle.total_bytes))){throw 'Final evidence inventory/budget differs; files retained.'}
$ledger=$bundle.ledger
if([int64](Get-Volume -DriveLetter C).SizeRemaining -lt 402653184){throw 'Headroom below384MiB after publication; all files retained.'}
if(!$ledger.complete){[Console]::Error.WriteLine($errors -join [Environment]::NewLine);exit 70}
Write-Output "Actual five-case neutral main capture completed: $out";exit 0
