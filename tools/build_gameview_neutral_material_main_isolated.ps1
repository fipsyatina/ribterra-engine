param(
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$ObjectRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 or newer is required.'}
$repoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,'C:\Users\fipst\Desktop\ribterra')){
  throw 'Authoritative repository identity mismatch.'
}
$repositoryBuildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$inputRoot=[IO.Path]::GetFullPath($BuildRoot)
$objectPath=[IO.Path]::GetFullPath($ObjectRoot)
$outRoot=[IO.Path]::GetFullPath($OutputRoot)
$commands=[Collections.Generic.List[object]]::new()
$bindings=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$maximum=134217728L;$minimumFree=402653184L

function Same-Path([string]$a,[string]$b){
  return [StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetFullPath($a),[IO.Path]::GetFullPath($b))
}
function Assert-NoReparse([string]$path){
  $cursor=[IO.Path]::GetFullPath($path)
  while($true){
    if(Test-Path -LiteralPath $cursor){
      if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        throw "Reparse path forbidden: $cursor"
      }
    }
    $parent=Split-Path $cursor
    if([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor){break}
    $cursor=$parent
  }
}
function Assert-Inside([string]$path,[string]$root){
  $full=[IO.Path]::GetFullPath($path)
  if(!$full.StartsWith(([IO.Path]::GetFullPath($root)+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw "Path must remain inside ${root}: $path"
  }
  Assert-NoReparse $full
}
function Assert-Disjoint([string]$a,[string]$b){
  $left=[IO.Path]::GetFullPath($a).TrimEnd([char[]]@('\','/'))
  $right=[IO.Path]::GetFullPath($b).TrimEnd([char[]]@('\','/'))
  if((Same-Path $left $right) -or
     $left.StartsWith(($right+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $right.StartsWith(($left+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw "Input/output roots overlap: $a ; $b"
  }
}
function Hash-Row([string]$path){
  if(![IO.Path]::IsPathFullyQualified($path)){throw "Bound file paths must be absolute: $path"}
  Assert-NoReparse $path
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing bound file: $path"}
  $item=Get-Item -LiteralPath $path -Force
  return [ordered]@{path=$item.FullName;bytes=[int64]$item.Length;
    sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
}
function Assert-Row([object]$row,[string]$label){
  if($row.sha256 -isnot [string] -or $row.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
     ($row.bytes -isnot [int] -and $row.bytes -isnot [long]) -or $row.bytes -lt 0){throw "Invalid hash row: $label"}
  $actual=Hash-Row ([string]$row.path)
  if($actual.bytes -ne [int64]$row.bytes -or $actual.sha256 -ne [string]$row.sha256){
    throw "Changed binding: $label ; $($row.path)"
  }
}
function Add-Binding([string]$path,[int64]$bytes,[string]$sha){
  if(![IO.Path]::IsPathFullyQualified($path)){throw "Accepted binding must use an absolute path: $path"}
  $full=[IO.Path]::GetFullPath($path)
  $row=[pscustomobject][ordered]@{path=$full;bytes=$bytes;sha256=$sha.ToUpperInvariant()}
  if($bindings.ContainsKey($full)){
    $old=$bindings[$full]
    if($old.bytes -ne $row.bytes -or $old.sha256 -ne $row.sha256){throw "Conflicting accepted bindings: $full"}
  } else {
    Assert-Row $row 'accepted input'
    $bindings.Add($full,$row)
  }
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
      Assert-NoReparse $tree.root
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
        $path=[IO.Path]::GetFullPath((Join-Path $tree.root $relative));Assert-Inside $path $tree.root
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
function Producer-Rows([object]$document,[ValidateSet(14,20)][int]$expectedInputs=20) {
  if($document -isnot [pscustomobject] -or
     ($document.schema -isnot [int64] -and $document.schema -isnot [int32]) -or $document.schema -ne 1 -or
     $document.complete -isnot [bool] -or $document.complete -ne $true -or
     ($document.item_count -isnot [int64] -and $document.item_count -isnot [int32]) -or $document.item_count -ne 2 -or
     $document.exporter_sha256 -isnot [string] -or $document.exporter_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
     $document.exporter_sha256.ToUpperInvariant() -cne (Hash-Row (Join-Path $repoRoot 'tools/export_material_master.py')).sha256 -or
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
    $actual=Hash-Row $path
    if($actual.bytes -ne $inputRow.bytes -or $actual.sha256 -ne $inputRow.sha256){throw 'Material source input changed'}
    $result.Add([ordered]@{relative_path=$relative;path=$actual.path;bytes=$actual.bytes;sha256=$actual.sha256})
  }
  return $result.ToArray()
}
function Collect-Bindings([object]$value){
  if($null -eq $value -or $value -is [string] -or $value.GetType().IsValueType){return}
  if($value -is [Collections.IEnumerable] -and $value -isnot [Collections.IDictionary]){
    foreach($item in $value){Collect-Bindings $item};return
  }
  $properties=@($value.PSObject.Properties.Name)
  if($properties -contains 'external_header_trees' -or $properties -contains 'external_header_tree_format'){
    $decoded=Expand-ExternalHeaders $value
    foreach($tree in @($decoded.trees_before)+@($decoded.trees_after)){
      foreach($row in $tree.files){Add-Binding $row.path $row.bytes $row.sha256}
    }
  }
  if($properties -contains 'bytes' -and $properties -contains 'sha256'){
    if($properties -contains 'unchanged' -and $value.unchanged -ne $true){throw 'Accepted report contains a changed binding.'}
    if($properties -contains 'after_bytes' -and [int64]$value.after_bytes -ne [int64]$value.bytes){throw 'Accepted final byte binding differs.'}
    if($properties -contains 'after_sha256' -and [string]$value.after_sha256 -ne [string]$value.sha256){throw 'Accepted final hash binding differs.'}
    foreach($key in @('path','repository_path','snapshot_path','origin','destination')){
      if($properties -contains $key){Add-Binding ([string]$value.$key) ([int64]$value.bytes) ([string]$value.sha256)}
    }
  }
  foreach($property in $value.PSObject.Properties){Collect-Bindings $property.Value}
}
function Tree-Rows([string]$root){
  Assert-NoReparse $root
  if(!(Test-Path -LiteralPath $root -PathType Container)){throw "Missing tree: $root"}
  $rows=[Collections.Generic.List[object]]::new()
  $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
  while($pending.Count){
    foreach($path in [IO.Directory]::EnumerateFileSystemEntries($pending.Pop())){
      $attributes=[IO.File]::GetAttributes($path)
      if(($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse tree entry: $path"}
      if(($attributes -band [IO.FileAttributes]::Directory) -ne 0){$pending.Push($path);continue}
      $hash=Hash-Row $path
      $rows.Add([pscustomobject][ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$path).Replace('\','/');
        path=$hash.path;bytes=$hash.bytes;sha256=$hash.sha256})
    }
  }
  return @($rows.ToArray() | Sort-Object path)
}
function Row-Total([object[]]$rows,[string]$property='bytes'){
  [int64]$total=0
  foreach($row in $rows){
    $value=$row.$property
    if(($value -isnot [long] -and $value -isnot [int]) -or $value -lt 0 -or $value -gt [int64]::MaxValue-$total){
      throw "Invalid or overflowing byte total: $property"
    }
    $total+=[int64]$value
  }
  return $total
}
function Tree-Metrics([string]$root){
  Assert-NoReparse $root
  $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
  [int64]$bytes=0;[int64]$count=0
  while($pending.Count){foreach($path in [IO.Directory]::EnumerateFileSystemEntries($pending.Pop())){
    $attributes=[IO.File]::GetAttributes($path)
    if(($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse metric entry: $path"}
    if(($attributes -band [IO.FileAttributes]::Directory) -ne 0){$pending.Push($path);continue}
    $length=([IO.FileInfo]::new($path)).Length
    if($length -lt 0 -or $length -gt [int64]::MaxValue-$bytes){throw 'Physical byte total overflow.'}
    $bytes+=$length;++$count
  }}
  return [ordered]@{file_count=$count;total_bytes=$bytes}
}
function Assert-Tree([string]$root,[object[]]$expected,[string]$label){
  $actual=@(Tree-Rows $root)
  if($actual.Count -ne $expected.Count){throw "$label file count changed."}
  for($i=0;$i -lt $actual.Count;$i++){
    $row=$expected[$i]
    Assert-Inside ([string]$row.path) $root
    if($actual[$i].relative_path -ne [string]$row.relative_path -or
       !(Same-Path $actual[$i].path ([string]$row.path)) -or
       $actual[$i].bytes -ne [int64]$row.bytes -or $actual[$i].sha256 -ne [string]$row.sha256){
      throw "$label tree changed: $($row.relative_path)"
    }
  }
}
function Assert-Sequence([object[]]$expected,[object[]]$actual,[string]$label){
  if($expected.Count -ne $actual.Count){throw "$label count differs."}
  for($i=0;$i -lt $expected.Count;$i++){
    if(![StringComparer]::Ordinal.Equals([string]$expected[$i],[string]$actual[$i])){throw "$label differs at $i."}
  }
}
function Assert-Headroom([string]$phase){
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below384MiB during ${phase}: $free"}
  if(Test-Path -LiteralPath $outRoot){
    $total=(Tree-Metrics $outRoot).total_bytes
    if($total -gt 134217728){throw "Main output exceeds128MiB during ${phase}: $total"}
  }
}
function Write-NewBytes([string]$path,[byte[]]$data){
  Assert-Inside $path $outRoot
  Assert-Headroom 'before exclusive evidence write'
  if((Tree-Metrics $outRoot).total_bytes+$data.LongLength -gt $maximum -or
     [int64](Get-Volume -DriveLetter C).SizeRemaining -lt $minimumFree+$data.LongLength){throw 'Pending output exceeds budget/headroom.'}
  $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($data,0,$data.Length)}finally{$stream.Dispose()}
}
function Write-NewJson([string]$path,[object]$value){
  $text=($value | ConvertTo-Json -Depth 30 -Compress)+"`n"
  Write-NewBytes $path ([Text.UTF8Encoding]::new($false).GetBytes($text))
}
function Copy-Exclusive([string]$source,[string]$destination){
  $before=Hash-Row $source
  if($before.bytes -gt 67108864){throw "Runtime/source copy exceeds64MiB: $source"}
  Write-NewBytes $destination ([IO.File]::ReadAllBytes($source))
  $after=Hash-Row $destination
  if($before.bytes -ne $after.bytes -or $before.sha256 -ne $after.sha256){throw "Copy differs: $destination"}
  return [ordered]@{origin=$before.path;destination=$after.path;bytes=$after.bytes;sha256=$after.sha256}
}
function Invoke-Recorded([string]$exe,[object[]]$arguments,[string]$name){
  Assert-Headroom "before $name"
  $log=Join-Path $outRoot "$name.log"
  if(Test-Path -LiteralPath $log){throw "Existing command log: $log"}
  $savedPreference=$ErrorActionPreference;$code=-1;$exception=$null
  $lines=[Collections.Generic.List[string]]::new();$started=[DateTimeOffset]::UtcNow
  try{
    $ErrorActionPreference='Continue'
    $global:LASTEXITCODE=$null
    & $exe @arguments 2>&1 | ForEach-Object {$lines.Add($_.ToString());Write-Host $_}
    if($null -ne $global:LASTEXITCODE){$code=[int]$global:LASTEXITCODE}
  } catch {$exception=$_.Exception.Message} finally {$ErrorActionPreference=$savedPreference}
  $ended=[DateTimeOffset]::UtcNow
  $text=if($lines.Count){[string]::Join("`n",$lines)+"`n"}else{''}
  Write-NewBytes $log ([Text.UTF8Encoding]::new($false).GetBytes($text))
  $exit=Join-Path $outRoot "$name.log.exit.txt"
  Write-NewBytes $exit ([Text.UTF8Encoding]::new($false).GetBytes("$code`n"))
  $commands.Add([ordered]@{executable=$exe;arguments=@($arguments);working_directory=(Get-Location).Path;
    exit_code=$code;started_utc=$started.ToString('o');ended_utc=$ended.ToString('o');exception=$exception;
    log=Hash-Row $log;exit_sidecar=Hash-Row $exit})
  Write-NewJson (Join-Path $outRoot ('command-'+$commands.Count+'.json')) $commands[$commands.Count-1]
  if($code -ne 0 -or $exception){throw "$exe failed($code): $exception; retained log $log"}
  Assert-Headroom "after $name"
}

if(![IO.Path]::IsPathFullyQualified($BuildRoot) -or ![IO.Path]::IsPathFullyQualified($ObjectRoot) -or
   ![IO.Path]::IsPathFullyQualified($OutputRoot)) {throw 'All artifact-root arguments must be absolute.'}
if(!(Test-Path -LiteralPath $inputRoot -PathType Container) -or !(Same-Path (Split-Path $inputRoot) $repositoryBuildRoot)){
  throw 'BuildRoot must be one existing immediate repository build child.'
}
if(!(Test-Path -LiteralPath $objectPath -PathType Container) -or !(Same-Path (Split-Path $objectPath) $repositoryBuildRoot)){
  throw 'ObjectRoot must be one existing immediate repository build child.'
}
if(!(Same-Path (Split-Path $outRoot) $repositoryBuildRoot) -or (Test-Path -LiteralPath $outRoot)){
  throw 'OutputRoot must be one fresh immediate repository build child.'
}
foreach($root in @($inputRoot,$objectPath,$outRoot)){Assert-NoReparse $root}
Assert-Disjoint $inputRoot $objectPath;Assert-Disjoint $inputRoot $outRoot;Assert-Disjoint $objectPath $outRoot
$buildReportPath=Join-Path $inputRoot 'build-report.json'
$accepted=Get-Content -Raw -LiteralPath $buildReportPath | ConvertFrom-Json
if(!(Same-Path ([string]$accepted.authoritative_repository) $repoRoot) -or !(Same-Path ([string]$accepted.output_root) $inputRoot) -or
   !(Same-Path ([string]$accepted.object_root) $objectPath) -or
   $accepted.translation_unit_count -ne 24 -or $accepted.internal_production_translation_unit_count -ne 23 -or
   $accepted.build_label -ne 'gameview-neutral-material-isolated-unversioned' -or
   $accepted.execution.gameview_neutral_material_probe -ne $false -or $accepted.execution.game -ne $false -or $accepted.execution.gpu -ne $false){
  throw 'BuildRoot is not the agreed fresh normal material probe build.'
}
$storagePath=Join-Path $inputRoot 'artifact-storage.json'
$acceptedStorage=Get-Content -LiteralPath $storagePath -Raw | ConvertFrom-Json
$acceptedTreeBefore=@(Tree-Rows $inputRoot);$acceptedObjectTreeBefore=@(Tree-Rows $objectPath)
if($acceptedStorage.schema -ne 1 -or $acceptedStorage.maximum_bytes_each -ne $maximum -or
   $acceptedStorage.minimum_free_bytes -ne $minimumFree -or $acceptedStorage.aggregate_limit_claimed -ne $false -or
   $acceptedStorage.observed_free_bytes -lt $minimumFree -or @($acceptedStorage.artifact_roots).Count -ne 2 -or
   $accepted.artifact_root_limits.maximum_bytes_each -ne $maximum -or
   $accepted.artifact_root_limits.minimum_free_bytes -ne $minimumFree -or $accepted.artifact_root_limits.aggregate_limit_claimed -ne $false){
  throw 'Accepted split storage limits/ledger differ.'
}
foreach($pair in @(@($inputRoot,$acceptedTreeBefore),@($objectPath,$acceptedObjectTreeBefore))){
  $entry=@($acceptedStorage.artifact_roots | Where-Object {Same-Path ([string]$_.root) $pair[0]})
  $rows=@($pair[1]);$bytes=Row-Total $rows
  if($entry.Count -ne 1 -or $entry[0].file_count -ne $rows.Count -or $entry[0].total_bytes -ne $bytes -or $bytes -gt $maximum){
    throw 'Accepted artifact storage does not include every current file and its final ledger.'
  }
}
$acceptedBuildBytes=Row-Total $acceptedTreeBefore;$acceptedObjectBytes=Row-Total $acceptedObjectTreeBefore
if($acceptedStorage.aggregate_bytes -ne $acceptedBuildBytes+$acceptedObjectBytes -or
   !(Same-Path ([string]$accepted.object_artifact.root) $objectPath) -or
   $accepted.object_artifact.file_count -ne $acceptedObjectTreeBefore.Count -or
   $accepted.object_artifact.total_bytes -ne $acceptedObjectBytes){throw 'Accepted aggregate/object ledger differs.'}
Assert-Tree $objectPath @($accepted.object_artifact.files | Sort-Object path) 'Complete accepted object artifact'
$prefsRoot=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Ribterra/Ribterra'
Assert-NoReparse $prefsRoot;$prefsExisted=Test-Path -LiteralPath $prefsRoot -PathType Container
$prefsBefore=if($prefsExisted){@(Tree-Rows $prefsRoot)}else{@()}
$sourceRoot=Join-Path $inputRoot 'source'
$production=[ordered]@{
  foundation='src/foundation/foundation.cpp';economy='src/foundation/economy.cpp';lobby='src/foundation/lobby.cpp'
  atomic_file='src/foundation/atomic_file.cpp';platform='src/foundation/platform.cpp';world='src/sim/world.cpp'
  weather='src/sim/weather.cpp';skills='src/sim/skills.cpp';world_save='src/sim/world_save.cpp'
  game_server='src/server/game_server.cpp';gns_transport='src/net/gns_transport.cpp';protocol='src/net/protocol.cpp'
  network_session='src/net/network_session.cpp';asset_catalog='src/assets/asset_catalog.cpp';texture_atlas='src/assets/texture_atlas.cpp'
  localization='src/ui/localization.cpp';renderer2d='src/render/renderer2d.cpp';contact_shadow='src/render/contact_shadow.cpp'
  sky_shadow='src/render/sky_shadow.cpp';view='src/client/view.cpp';client_session='src/client/client_session.cpp'
  game_art='src/client/game_art.cpp';game_ui='src/ui/game_ui.cpp'
}
Assert-Sequence (@($production.Values)+@('tools/gameview_neutral_material_probe.cpp')) @($accepted.translation_units) 'Accepted translation units'
$externalRoots=@((Join-Path $repoRoot '.deps/SDL3-3.4.16/include'),(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include'))
$expectedFlags=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
  '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/GR','/Zc:__cplusplus','/DRMLUI_STATIC_LIB',
  '/DRIBTERRA_PROBE_BUILD="gameview-neutral-material-isolated-unversioned"',
  ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $inputRoot 'generated')))+
  @($externalRoots | ForEach-Object {'/external:I'+$_})+@('/external:W0')
Assert-Sequence $expectedFlags @($accepted.flags) 'Strict accepted common flags'
if(@($accepted.commands).Count -ne 122 -or @($accepted.commands | Where-Object {$_.exit_code -ne 0}).Count){
  throw 'Accepted material command ledger must contain122 successful commands.'
}
$acceptedCompiler=@($accepted.tools | Where-Object {(Split-Path ([string]$_.path) -Leaf) -eq 'cl.exe'})
$acceptedLinker=@($accepted.tools | Where-Object {(Split-Path ([string]$_.path) -Leaf) -eq 'link.exe'})
if($acceptedCompiler.Count -ne 1 -or $acceptedLinker.Count -ne 1){throw 'Accepted compiler/linker identities are missing or ambiguous.'}
foreach($command in @($accepted.commands)){
  Assert-Inside ([string]$command.log.path) $inputRoot
  Assert-Inside ([string]$command.exit_sidecar.path) $inputRoot
  if((Get-Content -Raw -LiteralPath ([string]$command.exit_sidecar.path)).Trim() -ne '0'){
    throw 'Accepted actual command exit sidecar is nonzero.'
  }
  if(!(Same-Path ([string]$command.working_directory) $repoRoot) -or
     !(Same-Path ([string]$command.exit_sidecar.path) ([string]$command.log.path+'.exit.txt')) -or
     @($accepted.tools | Where-Object {Same-Path ([string]$_.path) ([string]$command.executable)}).Count -ne 1){
    throw 'Accepted argv working directory, sidecar or tool binding differs.'
  }
}
if(@($accepted.commands.log.path | Sort-Object -Unique).Count -ne 122 -or
   @(Get-ChildItem -LiteralPath $inputRoot -File -Filter '*.log.exit.txt').Count -ne 122){
  throw 'Accepted122 distinct actual command logs/exit sidecars are required.'
}
if(@($accepted.source_provenance).Count -eq 0 -or @($accepted.source_provenance).Count -ne @($accepted.source_final_binding).Count){
  throw 'Accepted source provenance/final binding is absent or incomplete.'
}
$snapshotRows=@($accepted.source_provenance | Sort-Object snapshot_path)
$actualSnapshot=@(Tree-Rows $sourceRoot)
if($snapshotRows.Count -ne $actualSnapshot.Count){throw 'Accepted source snapshot inventory differs.'}
$sourceNames=@($snapshotRows.relative_path | Sort-Object)
if(@($sourceNames | Sort-Object -Unique).Count -ne $sourceNames.Count){throw 'Duplicate accepted source identity.'}
$currentIncludes=@(Tree-Rows (Join-Path $repoRoot 'include'))
$acceptedIncludeNames=@($sourceNames | Where-Object {$_ -clike 'include/*'})
Assert-Sequence @($currentIncludes | ForEach-Object {'include/'+$_.relative_path} | Sort-Object) $acceptedIncludeNames 'Current whole include membership'
foreach($row in $currentIncludes){Add-Binding $row.path $row.bytes $row.sha256}
for($i=0;$i -lt $snapshotRows.Count;$i++){
  $row=$snapshotRows[$i]
  Assert-Inside ([string]$row.repository_path) $repoRoot;Assert-Inside ([string]$row.snapshot_path) $sourceRoot
  if(!(Same-Path $row.repository_path (Join-Path $repoRoot ([string]$row.relative_path))) -or
     !(Same-Path $row.snapshot_path (Join-Path $sourceRoot ([string]$row.relative_path))) -or
     !(Same-Path $row.snapshot_path $actualSnapshot[$i].path)){
    throw 'Accepted source path identity differs.'
  }
  $final=@($accepted.source_final_binding | Where-Object {$_.relative_path -eq $row.relative_path})
  if($final.Count -ne 1 -or $final[0].unchanged -ne $true -or
     !(Same-Path $final[0].repository_path $row.repository_path) -or !(Same-Path $final[0].snapshot_path $row.snapshot_path) -or
     $final[0].bytes -ne $row.bytes -or $final[0].sha256 -ne $row.sha256){throw 'Accepted final source binding differs.'}
}
$decodedHeaders=Expand-ExternalHeaders $accepted
$headerTreesBefore=@($decodedHeaders.trees_before)
$headerTreesAfter=@($decodedHeaders.trees_after)
if($headerTreesBefore.Count -ne 4 -or $headerTreesAfter.Count -ne 4){throw 'All four compiled external SDK header trees must be bound.'}
for($i=0;$i -lt 4;$i++){
  if(!(Same-Path ([string]$headerTreesBefore[$i].root) $externalRoots[$i]) -or
     !(Same-Path ([string]$headerTreesAfter[$i].root) $externalRoots[$i])){throw 'External SDK include identity differs.'}
  Assert-Tree $externalRoots[$i] @($headerTreesBefore[$i].files) 'Accepted before SDK'
  Assert-Tree $externalRoots[$i] @($headerTreesAfter[$i].files) 'Accepted after SDK'
}
$objectNames=@($production.Keys)+@('gameview_neutral_material_probe')
$objects=@($objectNames | ForEach-Object {Join-Path $objectPath "$_.obj"})
Assert-Sequence $objects @($accepted.objects | ForEach-Object {[string]$_.path}) 'Accepted object identity'
if($acceptedObjectTreeBefore.Count -ne 24 -or @($accepted.objects_before_link).Count -ne 24){throw 'All24 fresh objects and prelink bindings are required.'}
Assert-Sequence $objects @($accepted.objects_before_link | ForEach-Object {[string]$_.path}) 'Prelink object identity'
for($i=0;$i -lt 24;$i++){
  if($accepted.objects[$i].bytes -ne $accepted.objects_before_link[$i].bytes -or
     $accepted.objects[$i].sha256 -ne $accepted.objects_before_link[$i].sha256){throw 'Compiled object changed through link.'}
}
$expectedGenerated=@('network_frame_generated.h','network_generated.h','skill_balance_generated.h','world_save_generated.h')
Assert-Sequence $expectedGenerated @($accepted.generated_headers | ForEach-Object {Split-Path ([string]$_.path) -Leaf}) 'Generated header names'
if(@(Tree-Rows (Join-Path $inputRoot 'generated')).Count -ne 4){throw 'Generated header tree must contain exactly4 files.'}
foreach($row in @($accepted.generated_headers)){
  Assert-Inside ([string]$row.path) (Join-Path $inputRoot 'generated')
  if(!(Same-Path ([string]$row.path) (Join-Path (Join-Path $inputRoot 'generated') (Split-Path ([string]$row.path) -Leaf)))){
    throw 'Generated header must be the exact accepted top-level file.'
  }
}
$allUnits=[ordered]@{};foreach($entry in $production.GetEnumerator()){$allUnits.Add($entry.Key,$entry.Value)}
$allUnits.Add('gameview_neutral_material_probe','tools/gameview_neutral_material_probe.cpp')
foreach($entry in $allUnits.GetEnumerator()){
  $command=@($accepted.commands | Where-Object {(Split-Path ([string]$_.log.path) -Leaf) -eq "compile-$($entry.Key).log"})
  if($command.Count -ne 1 -or !(Same-Path ([string]$command[0].executable) ([string]$acceptedCompiler[0].path))){
    throw "Missing/wrong actual production compiler: $($entry.Key)"
  }
  $options=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}
    elseif($entry.Key -eq 'gameview_neutral_material_probe'){@('/Zc:inline')}else{@()}
  $argv=$expectedFlags+$options+@('/c',(Join-Path $sourceRoot $entry.Value),('/Fo'+(Join-Path $objectPath "$($entry.Key).obj")))
  Assert-Sequence $argv @($command[0].arguments) "Production argv $($entry.Key)"
}
if(@($accepted.commands | Where-Object {(Split-Path ([string]$_.executable) -Leaf) -ieq 'cl.exe'}).Count -ne 24){
  throw 'Accepted compiler command count differs.'
}
$acceptedLink=@($accepted.commands | Where-Object {(Split-Path ([string]$_.log.path) -Leaf) -eq 'link-GameViewNeutralMaterialProbe.log'})
if($acceptedLink.Count -ne 1 -or !(Same-Path ([string]$acceptedLink[0].executable) ([string]$acceptedLinker[0].path))){
  throw 'Missing/wrong accepted actual link command.'
}
$libraries=@($accepted.link_libraries | ForEach-Object {[string]$_.path})
if(!$libraries.Count -or @($libraries | Sort-Object -Unique).Count -ne $libraries.Count){throw 'Accepted libraries must be unique and nonempty.'}
Assert-Sequence (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
  ('/out:'+(Join-Path $inputRoot 'GameViewNeutralMaterialProbe.exe')))+$objects+$libraries) @($acceptedLink[0].arguments) 'Accepted link argv'
if($accepted.shader_compilation.fresh_shader_compilation -ne $true -or $accepted.shader_compilation.program_count -ne 23 -or
   $accepted.shader_compilation.stage_count -ne 46 -or $accepted.shader_compilation.total_file_count -ne 93){
  throw 'Accepted fresh shader cohort must contain23 programs/46 stages/93 files.'
}
$shaderPrograms=@('sprite','surface_lit','surface_hdr','world_linear','hdr_composite','material_sprite',
  'ground_gbuffer','ground_lighting','material_forward','ground_contact','ground_contact_gbuffer',
  'ground_contact_lighting','ground_sky_lighting','material_sky_forward','ground_shadow_lighting',
  'material_shadow_forward','sky_shadow_depth','water_ground_gbuffer','water_ground_lighting','water_reflection',
  'atmosphere_ground_lighting','atmosphere_material_forward','atmosphere_water_reflection')
Assert-Sequence $shaderPrograms @($accepted.shader_compilation.programs) 'Accepted shader programs'
$python='C:\Python314\python.exe'
$shadercross=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe'
$dxc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe'
$flatc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe'
$otherCommands=[ordered]@{}
$snapshotShaders=Join-Path $sourceRoot 'shaders';$compiledShaders=Join-Path $inputRoot 'assets/shaders'
foreach($program in $shaderPrograms){foreach($stage in @('vert','frag')){
  $stageName=if($stage -eq 'vert'){'vertex'}else{'fragment'}
  foreach($format in @('DXIL','JSON')){
    $extension=if($format -eq 'DXIL'){'dxil'}else{'json'}
    $otherCommands.Add("shader-$program-$stage-$extension.log",[ordered]@{tool=$shadercross;argv=@(
      (Join-Path $snapshotShaders "$program.$stage.hlsl"),'-s','HLSL','-d',$format,'-t',$stageName,
      '-e','main','-I',$snapshotShaders,'-o',(Join-Path $compiledShaders "$program.$stage.$extension"))})
  }
}}
$otherCommands.Add('shader-contract.log',[ordered]@{tool=$python;argv=@((Join-Path $sourceRoot 'tests/shader_contract.py'),
  $snapshotShaders,$compiledShaders,'--dxc',$dxc,'--shadercross',$shadercross,'--write-manifest')})
$otherCommands.Add('generate-skill-balance.log',[ordered]@{tool=$python;argv=@((Join-Path $sourceRoot 'tools/compile_skill_balance.py'),
  (Join-Path $sourceRoot 'data/skills.json'),(Join-Path $inputRoot 'generated/skill_balance_generated.h'))})
foreach($schema in @('world_save','network','network_frame')){
  $otherCommands.Add("generate-$schema.log",[ordered]@{tool=$flatc;argv=@('--cpp','--scoped-enums','--gen-object-api','-o',
    (Join-Path $inputRoot 'generated'),(Join-Path $sourceRoot "schemas/$schema.fbs"))})
}
$actualOther=@($accepted.commands | Where-Object {(Split-Path ([string]$_.executable) -Leaf) -notin @('cl.exe','link.exe')})
if($actualOther.Count -ne 97 -or $otherCommands.Count -ne 97 -or
   @($accepted.commands | Where-Object {(Split-Path ([string]$_.executable) -Leaf) -ieq 'link.exe'}).Count -ne 1){
  throw 'Exact97 generator/shader plus24 compiler/one linker commands are required.'
}
foreach($command in $actualOther){
  $name=Split-Path ([string]$command.log.path) -Leaf
  if(!$otherCommands.Contains($name)){throw "Unexpected actual command: $name"}
  $expected=$otherCommands[$name]
  if(!(Same-Path ([string]$command.executable) ([string]$expected.tool))){throw "Wrong actual generator/shader tool: $name"}
  Assert-Sequence @($expected.argv) @($command.arguments) "Actual generator/shader argv $name"
}
$shaderRoot=Join-Path $inputRoot 'assets/shaders'
$shaderRows=@($accepted.shader_compilation.artifacts | Sort-Object path)
$actualShaders=@(Tree-Rows $shaderRoot)
if($shaderRows.Count -ne 93 -or $actualShaders.Count -ne 93){throw 'Fresh shader inventory differs.'}
for($i=0;$i -lt 93;$i++){
  $row=$shaderRows[$i]
  Assert-Inside ([string]$row.path) $shaderRoot;Assert-Row $row 'fresh shader'
  if(!(Same-Path ([string]$row.path) $actualShaders[$i].path) -or $actualShaders[$i].relative_path.Contains('/')){
    throw 'Shader artifacts must bind the exact93 top-level files without duplicates.'
  }
}
$runtimeRelative=@('SDL3.dll','SDL3_ttf.dll','assets/fonts/NotoSans-Regular.ttf')
$runtimeOrigins=@((Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.dll'),
  (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll'),(Join-Path $repoRoot 'assets/fonts/NotoSans-Regular.ttf'))
if(@($accepted.runtime_files).Count -ne 3){throw 'Only the exact accepted2SDL DLLs and Noto font may be staged.'}
for($i=0;$i -lt 3;$i++){
  if(!(Same-Path ([string]$accepted.runtime_files[$i].destination) (Join-Path $inputRoot $runtimeRelative[$i])) -or
     !(Same-Path ([string]$accepted.runtime_files[$i].origin) $runtimeOrigins[$i])){throw 'Accepted runtime identity differs.'}
}
$referencedRoots=@($accepted.control_kit,$accepted.ui_kit,$accepted.original_art,$accepted.material_collection,$accepted.material_painted_compatibility)
if(!(Same-Path ([string]$accepted.original_art.root) (Join-Path $repoRoot 'assets'))){throw 'Original catalog identity differs.'}
foreach($root in $referencedRoots){
  if(![IO.Path]::IsPathFullyQualified([string]$root.root)){throw 'Referenced catalog roots must be absolute.'}
  Assert-Tree ([string]$root.root) @($root.files | Sort-Object path) 'Referenced complete asset inventory'
  Assert-Disjoint $outRoot ([string]$root.root);Assert-Disjoint $objectPath ([string]$root.root)
  Assert-Disjoint $inputRoot ([string]$root.root)
}
foreach($section in @(@('material_collection','material_source_inputs',20),@('material_painted_compatibility','painted_source_inputs',14))){
  $root=[string]$accepted.($section[0]).root
  $producer=Get-Content -LiteralPath (Join-Path $root 'material-collection-report.json') -Raw | ConvertFrom-Json
  $rows=@(Producer-Rows $producer ([int]$section[2]))
  foreach($side in @('files_before','files_after')){
    $recorded=@($accepted.($section[1]).$side)
    if($recorded.Count -ne $rows.Count){throw 'Producer-source closure count differs.'}
    for($i=0;$i -lt $rows.Count;$i++){
      if($recorded[$i].relative_path -cne $rows[$i].relative_path -or !(Same-Path $recorded[$i].path $rows[$i].path) -or
         $recorded[$i].bytes -ne $rows[$i].bytes -or $recorded[$i].sha256 -ne $rows[$i].sha256){throw 'Producer-source exact row differs.'}
    }
  }
  foreach($row in $rows){Add-Binding $row.path $row.bytes $row.sha256}
}
Collect-Bindings $accepted
if(@($accepted.protected_binding).Count -ne 5 -or
   @($accepted.protected_binding | Where-Object {$_.unchanged -ne $true}).Count){throw 'Exact five launcher/prior executable protections are required.'}
foreach($relative in @('Play Ribterra.cmd','Play Ribterra Rework.cmd','dist/current-build.txt')){
  if(@($accepted.protected_binding | Where-Object {Same-Path ([string]$_.path) (Join-Path $repoRoot $relative)}).Count -ne 1){
    throw "Missing protected launcher/pointer: $relative"
  }
}
$reportBinding=Hash-Row $buildReportPath;Add-Binding $reportBinding.path $reportBinding.bytes $reportBinding.sha256
$storageBinding=Hash-Row $storagePath;Add-Binding $storageBinding.path $storageBinding.bytes $storageBinding.sha256
foreach($row in @($acceptedTreeBefore)+@($acceptedObjectTreeBefore)+@($prefsBefore)){Add-Binding $row.path $row.bytes $row.sha256}
$mainPath=Join-Path $repoRoot 'src/client/main.cpp'
$mainRow=Hash-Row $mainPath
if($mainRow.sha256 -ne 'E0F4C8FE69DEB7691A4945A95654FFBF08FFAA7BEFD8F3CB186C45004D8415ED'){
  throw 'Actual main.cpp differs from the reviewed leaf1.42 optional diagnostic House main source.'
}
Add-Binding $mainRow.path $mainRow.bytes $mainRow.sha256
$helperRow=Hash-Row $PSCommandPath;Add-Binding $helperRow.path $helperRow.bytes $helperRow.sha256
$devShell='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1'
$devShellRow=Hash-Row $devShell;Add-Binding $devShellRow.path $devShellRow.bytes $devShellRow.sha256
Assert-Headroom 'before fresh output'
New-Item -ItemType Directory -Path $outRoot | Out-Null
foreach($directory in @('source','source/src','source/src/client','source/tools','obj','compiler-temp','assets','assets/fonts','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outRoot $directory) | Out-Null
}
$savedTemp=$env:TEMP;$savedTmp=$env:TMP;$savedLocation=Get-Location
$inputRows=@($bindings.Values | Sort-Object path)
$completed=$false
try{
  $mainSnapshot=Copy-Exclusive $mainPath (Join-Path $outRoot 'source/src/client/main.cpp')
  $helperSnapshot=Copy-Exclusive $PSCommandPath (Join-Path $outRoot 'source/tools/build_gameview_neutral_material_main_isolated.ps1')
  . $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:TEMP=Join-Path $outRoot 'compiler-temp';$env:TMP=$env:TEMP
  Set-Location -LiteralPath $outRoot
  $compiler=(Get-Command cl.exe -ErrorAction Stop).Source
  $linker=(Get-Command link.exe -ErrorAction Stop).Source
  foreach($tool in @($compiler,$linker)){
    $bound=@($accepted.tools | Where-Object {Same-Path ([string]$_.path) $tool})
    if($bound.Count -ne 1){throw "Selected tool is not the accepted exact compiler/linker: $tool"}
    Assert-Row $bound[0] 'selected tool'
  }
  $toolchain=[ordered]@{compiler=Hash-Row $compiler;linker=Hash-Row $linker;
    compiler_product_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).ProductVersion;
    linker_product_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($linker).ProductVersion;
    visual_studio=$env:VSINSTALLDIR;vc_tools_version=$env:VCToolsVersion;
    windows_sdk_directory=$env:WindowsSdkDir;windows_sdk_version=$env:WindowsSDKVersion;
    include_environment=$env:INCLUDE;lib_environment=$env:LIB;dev_shell=$devShellRow;
    header_scope='four explicit external include trees content-bound; implicit MSVC/WindowsSDK include path/version identity only'}
  if(!$env:VCToolsVersion -or !$env:WindowsSDKVersion -or !$env:INCLUDE -or !$env:LIB){throw 'Incomplete actual VS/SDK identity.'}
  $actualEnvironment=[ordered]@{include=$env:INCLUDE;lib=$env:LIB;vs_install_dir=$env:VSINSTALLDIR;
    vc_tools_version=$env:VCToolsVersion;windows_sdk_dir=$env:WindowsSdkDir;windows_sdk_version=$env:WindowsSDKVersion}
  foreach($key in $actualEnvironment.Keys){
    if(![StringComparer]::OrdinalIgnoreCase.Equals([string]$accepted.compiler_environment.$key,[string]$actualEnvironment[$key])){
      throw "Compiler/SDK search environment differs from accepted objects: $key"
    }
  }
  foreach($row in $inputRows){Assert-Row $row 'immediate precompile input'}
  Write-NewJson (Join-Path $outRoot 'input-bindings.json') $inputRows
  Write-NewJson (Join-Path $outRoot 'accepted-build-tree-before.json') $acceptedTreeBefore
  Write-NewJson (Join-Path $outRoot 'accepted-object-tree-before.json') $acceptedObjectTreeBefore
  Write-NewJson (Join-Path $outRoot 'ordinary-preferences-before.json') ([ordered]@{root=$prefsRoot;existed=$prefsExisted;files=$prefsBefore})
  $mainObject=Join-Path $outRoot 'obj/main.obj'
  if(Test-Path -LiteralPath $mainObject){throw 'Refusing a preexisting main object.'}
  $compileArguments=@($accepted.flags)+@('/c',$mainSnapshot.destination,('/Fo'+$mainObject))
  Invoke-Recorded $compiler $compileArguments 'compile-main'
  $productionObjects=@($production.Keys | ForEach-Object {Join-Path $objectPath "$_.obj"})
  $executable=Join-Path $outRoot 'Ribterra.exe'
  if(Test-Path -LiteralPath $executable){throw 'Refusing a preexisting main executable.'}
  foreach($row in $inputRows){Assert-Row $row 'immediate prelink input'}
  $linkArguments=@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',('/out:'+$executable))+
    @($mainObject)+$productionObjects+$libraries
  Invoke-Recorded $linker $linkArguments 'link-Ribterra'
  $runtimeCopies=@()
  for($i=0;$i -lt 3;$i++){
    $runtimeCopies += Copy-Exclusive ([string]$accepted.runtime_files[$i].destination) (Join-Path $outRoot $runtimeRelative[$i])
  }
  $shaderCopies=@()
  foreach($row in $shaderRows){
    $shaderCopies += Copy-Exclusive ([string]$row.path) (Join-Path $outRoot ('assets/shaders/'+(Split-Path ([string]$row.path) -Leaf)))
  }
  if(@(Tree-Rows (Join-Path $outRoot 'assets/shaders')).Count -ne 93){throw 'Copied shader count differs.'}
  $finalRows=@($inputRows | ForEach-Object {
    $after=Hash-Row $_.path
    [ordered]@{path=$_.path;bytes=$_.bytes;sha256=$_.sha256;after_bytes=$after.bytes;after_sha256=$after.sha256;
      unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  Write-NewJson (Join-Path $outRoot 'input-final-hashes.json') $finalRows
  if(@($finalRows | Where-Object {-not $_.unchanged}).Count){throw 'Accepted build/current main/tool/library/runtime inputs changed.'}
  Assert-Tree $inputRoot $acceptedTreeBefore 'Complete accepted BuildRoot'
  Assert-Tree $objectPath $acceptedObjectTreeBefore 'Complete accepted ObjectRoot'
  Assert-Tree (Join-Path $repoRoot 'include') $currentIncludes 'Current whole include closure'
  for($i=0;$i -lt 4;$i++){Assert-Tree $externalRoots[$i] @($headerTreesBefore[$i].files) 'Postcompile SDK'}
  foreach($root in $referencedRoots){
    Assert-Tree ([string]$root.root) @($root.files | Sort-Object path) 'Referenced asset root'
  }
  foreach($copy in @($mainSnapshot,$helperSnapshot)+$runtimeCopies+$shaderCopies){
    Assert-Row ([ordered]@{path=$copy.destination;bytes=$copy.bytes;sha256=$copy.sha256}) 'Output copy'
  }
  if($commands.Count -ne 2 -or @($commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Actual main compile/link ledger is incomplete.'}
  Assert-Headroom 'before successful main report'
  $outputRows=@(Tree-Rows $outRoot)
  $report=[ordered]@{complete=$true;
    classification='bounded actual main compile/link from accepted23 production objects; executable not launched';
    authoritative_repository=$repoRoot;output_root=$outRoot;accepted_build_root=$inputRoot;accepted_object_root=$objectPath;
    accepted_build_report=$reportBinding;accepted_build_tree=Hash-Row (Join-Path $outRoot 'accepted-build-tree-before.json');
    accepted_artifact_storage=$storageBinding;accepted_object_tree=Hash-Row (Join-Path $outRoot 'accepted-object-tree-before.json');
    source_provenance=@($mainSnapshot,$helperSnapshot);main_source=$mainRow;
    immutable_input_bindings=Hash-Row (Join-Path $outRoot 'input-bindings.json');
    immutable_input_final_binding=Hash-Row (Join-Path $outRoot 'input-final-hashes.json');
    compiled_translation_unit_count=1;reused_production_object_count=23;excluded_object=(Join-Path $objectPath 'gameview_neutral_material_probe.obj');
    compiled_object=Hash-Row $mainObject;reused_objects=@($accepted.objects | Where-Object {(Split-Path ([string]$_.path) -Leaf) -ne 'gameview_neutral_material_probe.obj'});
    link_libraries=$accepted.link_libraries;flags=@($accepted.flags);commands=$commands.ToArray();toolchain=$toolchain;
    compiler_environment=$actualEnvironment;
    dependency_closure=[ordered]@{accepted_source_count=@($accepted.source_provenance).Count;whole_current_include_count=$currentIncludes.Count;
      external_sdk_tree_count=4;accepted_actual_command_count=122;accepted_fresh_object_count=24;main_compiled_fresh=$true;
      source_snapshot_current_exact=$true;probe_only_inline_on_main=$false;implicit_sdk_identity_only=$true};
    referenced_assets=[ordered]@{control=$accepted.control_kit;ui=$accepted.ui_kit;original=$accepted.original_art;
      neutral=$accepted.material_collection;painted=$accepted.material_painted_compatibility};
    producer_source_inputs=[ordered]@{neutral=$accepted.material_source_inputs;painted=$accepted.painted_source_inputs};
    accepted_protected_bindings=$accepted.protected_binding;
    ordinary_preferences_before=Hash-Row (Join-Path $outRoot 'ordinary-preferences-before.json');
    runtime_files=$runtimeCopies;shader_files=$shaderCopies;shader_file_count=93;shader_compilation=$false;
    executable=Hash-Row $executable;output_files_before_report=$outputRows;
    execution=[ordered]@{game=$false;gpu=$false};
    claims=[ordered]@{normal_startup=$false;production_art=$false;reference_fidelity=$false;full_engine=$false;fps=$false};
    storage=[ordered]@{maximum_output_bytes=134217728;minimum_free_bytes=402653184;bulk_art_copied=$false;
      sdk_libraries_copied=$false;objects_copied=$false;failed_roots_retained=$true;maximum_bytes_each=$maximum;
      accepted_build_bytes=$acceptedBuildBytes;accepted_object_bytes=$acceptedObjectBytes;aggregate_limit_claimed=$false;
      output_bytes=0L;output_file_count=0;aggregate_bytes=0L;report_included=$true;observed_free_bytes=[int64](Get-Volume -DriveLetter C).SizeRemaining}}
  $completed=$true
} catch {
  $failure=[ordered]@{complete=$false;classification='retained failed actual main build; no native execution';error=$_.Exception.Message;
    accepted_build_report=$reportBinding;main_source=$mainRow;commands=$commands.ToArray();output_root=$outRoot}
  $failurePath=Join-Path $outRoot 'main-build-failure.json'
  if(!(Test-Path -LiteralPath $failurePath)){Write-NewJson $failurePath $failure}
  throw
} finally {
  $env:TEMP=$savedTemp;$env:TMP=$savedTmp
  Set-Location -LiteralPath $savedLocation.Path
  # Failed compiler/linker runs retain the same input-preservation evidence.
  # This executes no tool and never repairs or replaces an accepted input.
  $preservationErrors=[Collections.Generic.List[string]]::new()
  $finalPath=Join-Path $outRoot 'input-final-hashes.json'
  if(!(Test-Path -LiteralPath $finalPath)){
    $failureFinalRows=@($inputRows | ForEach-Object {
      $row=$_
      try{
        $after=Hash-Row $row.path
        [ordered]@{path=$row.path;bytes=$row.bytes;sha256=$row.sha256;after_bytes=$after.bytes;after_sha256=$after.sha256;
          unchanged=($row.bytes -eq $after.bytes -and $row.sha256 -eq $after.sha256)}
      } catch {
        [ordered]@{path=$row.path;bytes=$row.bytes;sha256=$row.sha256;unchanged=$false;error=$_.Exception.Message}
      }
    })
    Write-NewJson $finalPath $failureFinalRows
  }
  foreach($row in $inputRows){
    try{Assert-Row $row 'final preservation'}catch{$preservationErrors.Add($_.Exception.Message)}
  }
  try{Assert-Tree $inputRoot $acceptedTreeBefore 'Final accepted BuildRoot'}catch{$preservationErrors.Add($_.Exception.Message)}
  try{Assert-Tree $objectPath $acceptedObjectTreeBefore 'Final accepted ObjectRoot'}catch{$preservationErrors.Add($_.Exception.Message)}
  try{Assert-Tree (Join-Path $repoRoot 'include') $currentIncludes 'Final current whole include closure'}catch{$preservationErrors.Add($_.Exception.Message)}
  for($i=0;$i -lt 4;$i++){
    try{Assert-Tree $externalRoots[$i] @($headerTreesBefore[$i].files) 'Final SDK membership'}catch{$preservationErrors.Add($_.Exception.Message)}
  }
  foreach($root in $referencedRoots){
    try{Assert-Tree ([string]$root.root) @($root.files | Sort-Object path) 'Final asset membership'}catch{$preservationErrors.Add($_.Exception.Message)}
  }
  $prefsAfter=@()
  try{
    if((Test-Path -LiteralPath $prefsRoot -PathType Container) -ne $prefsExisted){throw 'Ordinary preference directory membership changed.'}
    if($prefsExisted){$prefsAfter=@(Tree-Rows $prefsRoot);Assert-Tree $prefsRoot $prefsBefore 'Ordinary preferences'}
  }catch{$preservationErrors.Add($_.Exception.Message)}
  Write-NewJson (Join-Path $outRoot 'preservation-after.json') ([ordered]@{
    complete=($preservationErrors.Count -eq 0);input_final_binding=Hash-Row $finalPath;
    accepted_build_tree_unchanged=($preservationErrors.Count -eq 0);
    accepted_object_tree_unchanged=($preservationErrors.Count -eq 0);current_whole_include_membership_checked=$true;
    ordinary_preferences=[ordered]@{root=$prefsRoot;existed_before=$prefsExisted;files_before=$prefsBefore;
      existed_after=(Test-Path -LiteralPath $prefsRoot -PathType Container);files_after=$prefsAfter};
    external_headers_and_asset_membership_checked=$true;errors=$preservationErrors.ToArray();
    temporary_environment_restored=($env:TEMP -eq $savedTemp -and $env:TMP -eq $savedTmp)})
  if($preservationErrors.Count){throw ('Input preservation failed: '+($preservationErrors -join '; '))}
}
if($completed){
  $report.preservation_after=Hash-Row (Join-Path $outRoot 'preservation-after.json')
  $report.output_files_before_report=@(Tree-Rows $outRoot)
  Assert-Headroom 'before final publication'
  $used=Row-Total @($report.output_files_before_report)
  $report.storage.output_file_count=$report.output_files_before_report.Count+1
  $settled=$false
  for($iteration=0;$iteration -lt 16;$iteration++){
    $reportBytes=[Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 30 -Compress)+"`n")
    $next=$used+$reportBytes.LongLength
    if($report.storage.output_bytes -eq $next -and $report.storage.aggregate_bytes -eq $acceptedBuildBytes+$acceptedObjectBytes+$next){
      $settled=$true;break
    }
    $report.storage.output_bytes=$next;$report.storage.aggregate_bytes=$acceptedBuildBytes+$acceptedObjectBytes+$next
  }
  if(!$settled -or $next -gt $maximum){throw 'Self-inclusive main report did not settle within128MiB.'}
  Write-NewBytes (Join-Path $outRoot 'main-build-report.json') $reportBytes
  $actualFinal=Tree-Metrics $outRoot
  if($actualFinal.file_count -ne $report.storage.output_file_count -or $actualFinal.total_bytes -ne $report.storage.output_bytes){
    throw 'Physical main output does not match self-inclusive final storage ledger.'
  }
  Assert-Headroom 'after final self-inclusive report'
  Write-Output "Actual main compile/link complete without execution: $outRoot"
}
