param(
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$ObjectRoot,
  [Parameter(Mandatory=$true)][string]$KitRoot,
  [Parameter(Mandatory=$true)][string]$UiKitRoot,
  [Parameter(Mandatory=$true)][string]$MaterialRoot,
  [Parameter(Mandatory=$true)][string]$PaintedRoot,
  [Parameter(Mandatory=$true)][ValidateSet('positive','cold')][string]$Profile,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell7 or newer is required.'}
$repo='C:\Users\fipst\Desktop\ribterra'
$buildDirectory=Join-Path $repo 'build'
$maximum=134217728L;$minimumFree=402653184L

function Same([string]$a,[string]$b){return [StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($a),[IO.Path]::GetFullPath($b))}
function No-Reparse([string]$path){
  $cursor=[IO.Path]::GetFullPath($path)
  while($true){
    if(Test-Path -LiteralPath $cursor){
      if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse path forbidden: $cursor"}
    }
    $parent=Split-Path $cursor;if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break};$cursor=$parent
  }
}
function Existing([string]$path,[bool]$immediate){
  if(-not [IO.Path]::IsPathFullyQualified($path)){throw 'Artifact/input paths must be absolute.'}
  $full=[IO.Path]::GetFullPath($path);No-Reparse $full
  if(!(Test-Path -LiteralPath $full -PathType Container)){throw "Missing root: $full"}
  if($immediate){if(!(Same (Split-Path $full) $buildDirectory)){throw 'Artifact must be one immediate build child.'}}
  elseif(!$full.StartsWith(($buildDirectory+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Kit must remain under build.'}
  return $full
}
function Row([string]$path){
  No-Reparse $path;$item=Get-Item -LiteralPath $path -Force
  if($item.PSIsContainer){throw 'Binding must be a regular file.'}
  return [pscustomobject][ordered]@{path=$item.FullName;bytes=[int64]$item.Length;sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
}
function Tree([string]$root,[hashtable]$aliases=@{}){
  No-Reparse $root;$rows=[Collections.Generic.List[object]]::new();$pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
  while($pending.Count){
    foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)){
      if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        if(!$item.PSIsContainer -or !$aliases.ContainsKey($item.FullName)){throw "Unexpected output alias: $($item.FullName)"}
        $targets=@($item.Target);if($targets.Count -ne 1 -or !(Same $targets[0] $aliases[$item.FullName])){throw 'Retained junction target differs.'}
        No-Reparse $aliases[$item.FullName];continue
      }
      if($item.PSIsContainer){$pending.Push($item.FullName);continue}
      $row=Row $item.FullName
      $rows.Add([pscustomobject][ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/');path=$row.path;bytes=$row.bytes;sha256=$row.sha256})
    }
  }
  return @($rows | Sort-Object path)
}
function Total([object[]]$rows){
  [int64]$total=0
  foreach($row in $rows){if($row.bytes -lt 0 -or $row.bytes -ne [int64]$row.bytes -or $row.bytes -gt [int64]::MaxValue-$total){throw 'Invalid byte total.'};$total += [int64]$row.bytes}
  return $total
}
function Headroom(){if([int64](Get-Volume -DriveLetter C).SizeRemaining -lt $minimumFree){throw 'Free space below384MiB.'}}
function Match-Tree([string]$root,[object[]]$expected){
  $current=@(Tree $root)
  if($current.Count -ne $expected.Count){throw "Input tree membership changed: $root"}
  for($i=0;$i -lt $current.Count;$i++){
    if(!(Same $current[$i].path $expected[$i].path) -or $current[$i].bytes -ne $expected[$i].bytes -or $current[$i].sha256 -ne $expected[$i].sha256){throw "Input tree changed: $root"}
  }
}
function Write-New([string]$path,[byte[]]$bytes){
  $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Json-Bytes([object]$value){return [Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 40 -Compress)+"`n")}

$build=Existing $BuildRoot $true;$objects=Existing $ObjectRoot $true;$kit=Existing $KitRoot $false
$ui=Existing $UiKitRoot $false;$material=Existing $MaterialRoot $false;$painted=Existing $PaintedRoot $false;$original=Join-Path $repo 'assets'
$out=[IO.Path]::GetFullPath($OutputRoot)
if(-not [IO.Path]::IsPathFullyQualified($OutputRoot) -or !(Same (Split-Path $out) $buildDirectory) -or
   (Split-Path $out -Leaf) -notmatch '^[a-z][a-z0-9-]{0,127}$' -or (Test-Path -LiteralPath $out)){throw 'OutputRoot must be a fresh absolute immediate build child.'}
No-Reparse $out
$roots=@($build,$objects,$kit,$ui,$material,$painted,$out)
for($i=0;$i -lt $roots.Count;$i++){for($j=$i+1;$j -lt $roots.Count;$j++){
  $a=$roots[$i];$b=$roots[$j]
  if((Same $a $b) -or $a.StartsWith(($b+'\'),[StringComparison]::OrdinalIgnoreCase) -or $b.StartsWith(($a+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Roots must be distinct and non-overlapping.'}
}}
Headroom
$reportPath=Join-Path $build 'build-report.json';$storagePath=Join-Path $build 'artifact-storage.json'
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$storage=Get-Content -LiteralPath $storagePath -Raw | ConvertFrom-Json
if(!(Same $report.authoritative_repository $repo) -or !(Same $report.output_root $build) -or !(Same $report.object_root $objects) -or
   $report.build_label -ne 'gameview-neutral-material-isolated-unversioned' -or $report.translation_unit_count -ne 24 -or
   $report.internal_production_translation_unit_count -ne 23 -or @($report.commands).Count -ne 122 -or
   @($report.commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Expected actual strict neutral build.'}
if($storage.schema -ne 1 -or $storage.maximum_bytes_each -ne $maximum -or $storage.minimum_free_bytes -ne $minimumFree -or
   $storage.aggregate_limit_claimed -ne $false -or $storage.observed_free_bytes -lt $minimumFree -or @($storage.artifact_roots).Count -ne 2){throw 'Invalid separate-root storage ledger.'}
$trees=[ordered]@{build=@(Tree $build);objects=@(Tree $objects);control=@(Tree $kit);ui=@(Tree $ui);material=@(Tree $material);painted=@(Tree $painted);original=@(Tree $original)}
foreach($pair in @(@('build',$build),@('objects',$objects))){
  $recorded=@($storage.artifact_roots | Where-Object {Same $_.root $pair[1]})
  $actual=@($trees[$pair[0]]);$bytes=Total $actual
  if($recorded.Count -ne 1 -or $recorded[0].file_count -ne $actual.Count -or $recorded[0].total_bytes -ne $bytes -or $bytes -gt $maximum){throw 'Actual artifact storage differs.'}
}
if($storage.aggregate_bytes -ne (Total $trees.build)+(Total $trees.objects)){throw 'Aggregate artifact sum differs.'}
foreach($section in @(@('control_kit','control',$kit),@('ui_kit','ui',$ui),@('material_collection','material',$material),@('material_painted_compatibility','painted',$painted),@('original_art','original',$original))){
  if(!(Same $report.($section[0]).root $section[2])){throw 'Build input root differs.'}
  Match-Tree $section[2] @($report.($section[0]).files)
}
$bound=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
function Bind([object]$row,[string]$path){
  if($null -eq $row.bytes -or $row.bytes -lt 0 -or [string]$row.sha256 -notmatch '^[0-9A-Fa-f]{64}$'){throw 'Invalid SHA binding.'}
  $actual=Row $path
  if($actual.bytes -ne $row.bytes -or $actual.sha256 -ne ([string]$row.sha256).ToUpperInvariant()){throw "Bound file changed: $path"}
  if($bound.ContainsKey($actual.path)){
    if($bound[$actual.path].bytes -ne $actual.bytes -or $bound[$actual.path].sha256 -ne $actual.sha256){throw 'Conflicting SHA bindings.'}
  }else{$bound.Add($actual.path,$actual)}
}
function Bind-Recursive([object]$value){
  if($null -eq $value){return}
  if($value -is [System.Management.Automation.PSCustomObject]){
    $names=@($value.psobject.Properties.Name)
    if($names -contains 'bytes' -and $names -contains 'sha256'){
      foreach($key in @('path','repository_path','snapshot_path','origin','destination')){if($names -contains $key){Bind $value ([string]$value.$key)}}
      if($names -contains 'unchanged' -and !$value.unchanged){throw 'Build reported changed input.'}
      if($names -contains 'after_sha256' -and ($value.sha256 -ne $value.after_sha256 -or $value.bytes -ne $value.after_bytes)){throw 'Build final binding differs.'}
    }
    foreach($property in $value.psobject.Properties){Bind-Recursive $property.Value}
  }elseif($value -is [Collections.IEnumerable] -and $value -isnot [string]){foreach($entry in $value){Bind-Recursive $entry}}
}
Bind-Recursive $report
foreach($sourceGroup in @(@($material,'material_source_inputs',20),@($painted,'painted_source_inputs',14))){
$producer=Get-Content -LiteralPath (Join-Path $sourceGroup[0] 'material-collection-report.json') -Raw | ConvertFrom-Json
if($producer.schema -ne 1 -or !$producer.complete -or @($producer.inputs_before).Count -ne $sourceGroup[2] -or
   [Text.Encoding]::UTF8.GetString((Json-Bytes $producer.inputs_before)) -cne [Text.Encoding]::UTF8.GetString((Json-Bytes $producer.inputs_after))){throw 'Producer source observations differ.'}
$materialSources=[Collections.Generic.List[object]]::new();$sourceNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($row in $producer.inputs_before){
  $relative=[string]$row.relative_path
  if(@(Compare-Object @('relative_path','bytes','sha256') @($row.psobject.Properties.Name)).Count -or
     [IO.Path]::IsPathRooted($relative) -or $relative -match '[:\\]' -or
     @($relative.Split('/') | Where-Object {$_ -in @('','.', '..')}).Count -or !$sourceNames.Add($relative)){throw 'Unsafe material source binding.'}
  $path=Join-Path $repo $relative;Bind $row $path;$current=Row $path
  $materialSources.Add([pscustomobject][ordered]@{relative_path=$relative;path=$current.path;bytes=$current.bytes;sha256=$current.sha256})
}
foreach($observation in @('files_before','files_after')){
  if([Text.Encoding]::UTF8.GetString((Json-Bytes @($materialSources))) -cne
     [Text.Encoding]::UTF8.GetString((Json-Bytes $report.($sourceGroup[1]).$observation))){throw 'Builder material source inputs differ from exact producer inputs.'}
}
$exporter=Row (Join-Path $repo 'tools/export_material_master.py')
if($exporter.sha256 -ne ([string]$producer.exporter_sha256).ToUpperInvariant()){throw 'Producer exporter identity changed.'}
}
if($report.external_header_tree_format -ne 'root-relative-sha256-base64-triplets-v1'){throw 'Unknown compact SDK format.'}
$expandedSdk=@{}
$expectedSdkRoots=@((Join-Path $repo '.deps/SDL3-3.4.16/include'),(Join-Path $repo '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repo 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),(Join-Path $repo 'vcpkg_installed/x64-windows-release/include'))
foreach($observation in @('trees_before','trees_after')){
  $sdk=@($report.external_header_trees.$observation);if($sdk.Count -ne 4){throw 'Four SDK trees required.'}
  if(@($sdk.root | Sort-Object -Unique).Count -ne 4 -or @(Compare-Object @($expectedSdkRoots | Sort-Object) @($sdk.root | Sort-Object)).Count){throw 'Explicit SDK roots differ.'}
  $expanded=[Collections.Generic.List[object]]::new()
  foreach($tree in $sdk){
    No-Reparse $tree.root;$records=[Collections.Generic.List[object]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($tuple in $tree.files){
      if($tuple.Count -ne 3 -or $tuple[0] -isnot [string] -or $tuple[1] -isnot [long] -and $tuple[1] -isnot [int] -or $tuple[1] -lt 0 -or $tuple[2] -isnot [string]){throw 'Invalid compact SDK tuple.'}
      $relative=[string]$tuple[0]
      if([IO.Path]::IsPathRooted($relative) -or $relative -match '[:\\]' -or @($relative.Split('/') | Where-Object {$_ -in @('','.', '..')}).Count -or !$seen.Add($relative)){throw 'Unsafe compact SDK path.'}
      $sha=[Convert]::FromBase64String($tuple[2]);if($sha.Length -ne 32 -or [Convert]::ToBase64String($sha) -cne $tuple[2]){throw 'Noncanonical compact SHA.'}
      $row=[ordered]@{relative_path=$relative;path=Join-Path $tree.root $relative;bytes=[int64]$tuple[1];sha256=[Convert]::ToHexString($sha)}
      Bind $row $row.path;$records.Add($row)
    }
    Match-Tree $tree.root @($records);$expanded.Add([ordered]@{root=$tree.root;files=@($records)})
  }
  $expandedSdk[$observation]=$expanded
}
if([Text.Encoding]::UTF8.GetString((Json-Bytes $expandedSdk.trees_before)) -cne [Text.Encoding]::UTF8.GetString((Json-Bytes $expandedSdk.trees_after))){throw 'SDK before/after differs.'}
foreach($tree in $trees.Values){foreach($row in $tree){Bind $row $row.path}}
foreach($path in @($reportPath,$storagePath,$PSCommandPath)){Bind (Row $path) $path}
$own=@($report.source_provenance | Where-Object {$_.relative_path -eq 'tools/run_gameview_neutral_material_isolated.ps1'})
if($own.Count -ne 1 -or !(Same $own[0].repository_path $PSCommandPath)){throw 'Runner is not part of the compiled source cohort.'}
$prefs=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Ribterra/Ribterra';No-Reparse $prefs
$prefsExisted=Test-Path -LiteralPath $prefs -PathType Container;$prefsBefore=if($prefsExisted){@(Tree $prefs)}else{@()}
foreach($row in $prefsBefore){Bind $row $row.path}
$before=@($bound.Values | Sort-Object path)
$exe=Join-Path $build 'GameViewNeutralMaterialProbe.exe'
if(!(Same $report.executable.path $exe)){throw 'Neutral executable binding differs.'}
$arguments=@($Profile,(Join-Path $build 'assets'),$original,$material,$painted,$ui,$out)
$started=[DateTimeOffset]::UtcNow;$lines=[Collections.Generic.List[string]]::new();$exitCode=$null;$exception=$null
try{
  $prior=$ErrorActionPreference;try{$ErrorActionPreference='Continue';$LASTEXITCODE=$null
    & $exe @arguments 2>&1 | ForEach-Object {$lines.Add($_.ToString());Write-Host $_}
    if($null -ne $LASTEXITCODE){$exitCode=[int]$LASTEXITCODE}
  }finally{$ErrorActionPreference=$prior}
}catch{$exception=$_.Exception.Message}
$ended=[DateTimeOffset]::UtcNow
$nativeCreatedOutput=Test-Path -LiteralPath $out -PathType Container
if(!$nativeCreatedOutput){
  # The subprocess already received the genuinely absent fresh root. Preserve
  # its failure evidence now, without retrying or inventing a native report.
  No-Reparse $out
  if(Test-Path -LiteralPath $out){throw 'Non-directory output prevents exclusive failure-evidence creation.'}
  New-Item -ItemType Directory -Path $out -ErrorAction Stop | Out-Null
}
No-Reparse $out
$errors=[Collections.Generic.List[string]]::new();if($exception){$errors.Add($exception)}
if(!$nativeCreatedOutput){$errors.Add('Native did not create OutputRoot; wrapper created failure evidence only.')}
if($exitCode -ne 0 -or $null -eq $exitCode){$errors.Add("Native exit=$exitCode")}
$aliases=@{}
if($Profile -eq 'cold'){
  $metadata=Get-Content -LiteralPath (Join-Path $material 'art/material-collection.json') -Raw | ConvertFrom-Json
  $house=@($metadata.items | Where-Object {$_.legacy_key -eq 'building.house.ready'})[0]
  $aliases[(Join-Path $out 'invalid-root-reparse')]=$material
  $aliases[(Join-Path $out 'invalid-notice-reparse/notice-alias')]=Split-Path (Join-Path $material $house.albedo_source.provenance.notice_path)
  $aliases[(Join-Path $out 'invalid-palette-internal-reparse/palette-alias')]=Split-Path (Join-Path $material $house.albedo_source.palette_path)
}
$nativeRows=@();$nativeInventoryComplete=$true
try{$nativeRows=@(Tree $out $aliases)}catch{$nativeInventoryComplete=$false;$errors.Add($_.Exception.Message)}
$after=[Collections.Generic.List[object]]::new();$boundInputsUnchanged=$true
foreach($row in $before){try{
  $current=Row $row.path;$same=($row.bytes -eq $current.bytes -and $row.sha256 -eq $current.sha256)
  $after.Add([ordered]@{path=$row.path;bytes=$row.bytes;sha256=$row.sha256;after_bytes=$current.bytes;after_sha256=$current.sha256;unchanged=$same})
  if(!$same){$boundInputsUnchanged=$false;$errors.Add("Input changed: $($row.path)")}
}catch{$boundInputsUnchanged=$false;$errors.Add($_.Exception.Message);$after.Add([ordered]@{path=$row.path;unchanged=$false;error=$_.Exception.Message})}}
foreach($pair in @(@('build',$build),@('objects',$objects),@('control',$kit),@('ui',$ui),@('material',$material),@('painted',$painted),@('original',$original))){
  try{Match-Tree $pair[1] @($trees[$pair[0]])}catch{$boundInputsUnchanged=$false;$errors.Add($_.Exception.Message)}
}
$prefsAfter=@();try{
  if((Test-Path -LiteralPath $prefs -PathType Container) -ne $prefsExisted){throw 'Ordinary preference directory membership changed.'}
  if($prefsExisted){$prefsAfter=@(Tree $prefs);Match-Tree $prefs $prefsBefore}
}catch{$boundInputsUnchanged=$false;$errors.Add($_.Exception.Message)}
try{Headroom}catch{$errors.Add($_.Exception.Message)}
if((Total $nativeRows) -gt $maximum){$errors.Add('Native profile exceeded128MiB.')}
$outputText=if($lines.Count){[string]::Join("`n",$lines)+"`n"}else{''}
$outputBytes=[Text.UTF8Encoding]::new($false).GetBytes($outputText)
$bindingBytes=Json-Bytes ([ordered]@{before=$before;after=@($after)})
$nativeTotal=Total $nativeRows
$payloadTotal=[int64]$outputBytes.LongLength+[int64]$bindingBytes.LongLength
function Pending-Row([string]$path,[byte[]]$bytes){
  return [pscustomobject][ordered]@{path=$path;bytes=[int64]$bytes.LongLength;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))}
}
function Make-Ledger([string]$evidenceRoot,[bool]$separate){
  $value=[ordered]@{schema=1;profile=$Profile;accepted=($errors.Count -eq 0 -and !$separate);actual_subprocess=$true;signed_evidence=$false;
  native_created_output=$nativeCreatedOutput;
  repository=$repo;build_root=$build;object_root=$objects;kit_root=$kit;ui_kit_root=$ui;original_art_root=$original;
  material_collection_root=$material;painted_compatibility_root=$painted;output_root=$out;executable=$exe;arguments=$arguments;actual_argv=@($exe)+$arguments;
  working_directory=(Get-Location).Path;started_utc=$started.ToString('o');ended_utc=$ended.ToString('o');
  elapsed_milliseconds=[int64]($ended-$started).TotalMilliseconds;exit_code=$exitCode;integrity_errors=@($errors);
  bound_inputs_unchanged=$boundInputsUnchanged;input_final_binding=Pending-Row (Join-Path $evidenceRoot 'input-final-hashes.json') $bindingBytes;
  combined_output_binding=Pending-Row (Join-Path $evidenceRoot 'execution-output.txt') $outputBytes;runner=Row $PSCommandPath;
  build_report=Row $reportPath;artifact_storage=Row $storagePath;
  native_output_before_runner_evidence=[ordered]@{file_count=$nativeRows.Count;total_bytes=$nativeTotal;files=$nativeRows};
  protected_trees=[ordered]@{build=$trees.build;objects=$trees.objects;control=$trees.control;ui=$trees.ui;original=$trees.original;material=$trees.material;painted=$trees.painted};
  ordinary_preferences=[ordered]@{root=$prefs;existed_before=$prefsExisted;files_before=$prefsBefore;existed_after=(Test-Path -LiteralPath $prefs -PathType Container);files_after=$prefsAfter};
  storage=[ordered]@{maximum_bytes_each=$maximum;minimum_free_bytes=$minimumFree;aggregate_limit_claimed=$false;
    build_bytes=(Total $trees.build);object_bytes=(Total $trees.objects);build_plus_object_bytes=(Total $trees.build)+(Total $trees.objects)};
  claims=[ordered]@{fps=$null;gpu_execution_ms=$null;production_reference=$false;all_eight_masters=$false;normal_main=$false;full_engine=$false}}
  if($separate){
    $value.evidence_output_root=$evidenceRoot;$value.native_output_root=$out;$value.separate_failure_evidence=$true
    $value.native_output_before_runner_evidence.inventory_complete=$nativeInventoryComplete
    $value.storage.native_output_inventory_complete=$nativeInventoryComplete
    $value.storage.native_output_bytes=$(if($nativeInventoryComplete){$nativeTotal}else{$null})
    $value.storage.native_output_within_limit=($nativeInventoryComplete -and $nativeTotal -le $maximum)
    if(!$nativeInventoryComplete){$value.native_output_before_runner_evidence.total_bytes=$null;$value.native_output_before_runner_evidence.file_count=$null}
    $value.storage.evidence_file_count=3;$value.storage.evidence_bytes_including_ledger=0L
  }
  return $value
}
function Ledger-Bytes([object]$value,[bool]$separate){
  # Include the ledger's own decimal byte count without omitting any bindings.
  for($attempt=0;$attempt -lt 8;$attempt++){
    [byte[]]$encoded=Json-Bytes $value
    if(!$separate){return ,$encoded}
    $total=$payloadTotal+[int64]$encoded.LongLength
    if($value.storage.evidence_bytes_including_ledger -eq $total){return ,$encoded}
    $value.storage.evidence_bytes_including_ledger=$total
  }
  throw 'Failure evidence byte count did not converge; all files retained.'
}
function Bundle-Fits([int64]$existingBytes,[int64]$pendingBytes){
  return ($existingBytes -ge 0 -and $existingBytes -le $maximum -and $pendingBytes -ge 0 -and
    $pendingBytes -le $maximum-$existingBytes -and
    [int64](Get-Volume -DriveLetter C).SizeRemaining -ge $minimumFree+$pendingBytes)
}
$evidenceRoot=$out;$separate=$false;$ledger=Make-Ledger $evidenceRoot $separate
[byte[]]$bytes=Ledger-Bytes $ledger $separate
$bundleTotal=$payloadTotal+[int64]$bytes.LongLength
# All after-checks above are complete. Decide the destination for all three
# evidence files together, before publishing even stdout or the input ledger.
if(!$nativeInventoryComplete -or !(Bundle-Fits $nativeTotal $bundleTotal)){
  $errors.Add('Native inventory is incomplete or wrapper evidence bundle does not fit profile budget/headroom; separate failure evidence required.')
  $separate=$true;$evidenceRoot=$out+'-failure-evidence'
  if(-not [IO.Path]::IsPathFullyQualified($evidenceRoot) -or !(Same (Split-Path $evidenceRoot) $buildDirectory) -or
     (Split-Path $evidenceRoot -Leaf) -notmatch '^[a-z][a-z0-9-]{0,127}$'){throw 'Failure evidence path must be a portable absolute immediate build child.'}
  No-Reparse $evidenceRoot
  foreach($protectedRoot in @($roots)+@($original,$prefs)){
    if((Same $evidenceRoot $protectedRoot) -or $evidenceRoot.StartsWith(($protectedRoot+'\'),[StringComparison]::OrdinalIgnoreCase) -or
       $protectedRoot.StartsWith(($evidenceRoot+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Failure evidence root overlaps a protected/native root.'}
  }
  if(Test-Path -LiteralPath $evidenceRoot){throw "Fresh failure evidence root already exists; all existing files retained: $evidenceRoot"}
  $ledger=Make-Ledger $evidenceRoot $separate;[byte[]]$bytes=Ledger-Bytes $ledger $separate
  $bundleTotal=$payloadTotal+[int64]$bytes.LongLength
  if(!(Bundle-Fits 0 $bundleTotal)){throw 'Separate failure evidence would exceed128MiB/headroom; all files retained.'}
  New-Item -ItemType Directory -Path $evidenceRoot -ErrorAction Stop | Out-Null
  No-Reparse $evidenceRoot
  if(@(Tree $evidenceRoot).Count){throw 'Fresh failure evidence root is not empty; all files retained.'}
}
if(!(Bundle-Fits $(if($separate){0L}else{$nativeTotal}) $bundleTotal)){throw 'Evidence bundle budget/headroom changed before publication; all files retained.'}
Write-New (Join-Path $evidenceRoot 'execution-output.txt') $outputBytes
Write-New (Join-Path $evidenceRoot 'input-final-hashes.json') $bindingBytes
Write-New (Join-Path $evidenceRoot 'native-execution.json') $bytes
foreach($expected in @($ledger.combined_output_binding,$ledger.input_final_binding)){
  $actual=Row $expected.path
  if($actual.bytes -ne $expected.bytes -or $actual.sha256 -ne $expected.sha256){throw 'Published failure/normal evidence differs from planned bytes; files retained.'}
}
$published=@(Tree $evidenceRoot $(if($separate){@{}}else{$aliases}))
if((Total $published) -gt $maximum -or ($separate -and ($published.Count -ne 3 -or (Total $published) -ne $bundleTotal))){throw 'Published evidence root exceeds its exact budget/inventory; files retained.'}
Headroom
if(!$ledger.accepted){[Console]::Error.WriteLine(($errors -join "`n")+"`nFailure evidence recorded: $evidenceRoot");exit 70}
Write-Output "Actual neutral-material $Profile execution recorded: $out"
exit 0
