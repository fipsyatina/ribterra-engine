param(
  [Parameter(Mandatory=$true)][string]$BuildRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 or newer is required.'}
$repoRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,'C:\Users\fipst\Desktop\ribterra')){
  throw 'Authoritative repository identity mismatch.'
}
$repositoryBuildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$inputRoot=(Resolve-Path -LiteralPath $BuildRoot).Path
$outRoot=[IO.Path]::GetFullPath($OutputRoot)
$commands=[Collections.Generic.List[object]]::new()
$bindings=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)

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
  if([string]$row.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or [int64]$row.bytes -lt 0){throw "Invalid hash row: $label"}
  $actual=Hash-Row ([string]$row.path)
  if($actual.bytes -ne [int64]$row.bytes -or $actual.sha256 -ne [string]$row.sha256){
    throw "Changed binding: $label ; $($row.path)"
  }
}
function Add-Binding([string]$path,[int64]$bytes,[string]$sha){
  if(![IO.Path]::IsPathFullyQualified($path)){throw "Accepted binding must use an absolute path: $path"}
  $full=[IO.Path]::GetFullPath($path)
  $row=[ordered]@{path=$full;bytes=$bytes;sha256=$sha.ToUpperInvariant()}
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
  foreach($item in @(Get-ChildItem -LiteralPath $root -Force -Recurse | Sort-Object FullName)){
    if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse tree entry: $($item.FullName)"}
    if($item.PSIsContainer){continue}
    $hash=Hash-Row $item.FullName
    $rows.Add([ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/');
      path=$hash.path;bytes=$hash.bytes;sha256=$hash.sha256})
  }
  return $rows.ToArray()
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
    $total=[int64]0
    foreach($row in @(Tree-Rows $outRoot)){$total += [int64]$row.bytes}
    if($total -gt 134217728){throw "Main output exceeds128MiB during ${phase}: $total"}
  }
}
function Write-NewBytes([string]$path,[byte[]]$data){
  Assert-Inside $path $outRoot
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
  $savedPreference=$ErrorActionPreference;$code=-1
  try{
    $ErrorActionPreference='Continue'
    & $exe @arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
    $code=[int]$LASTEXITCODE
  } finally {$ErrorActionPreference=$savedPreference}
  if(!(Test-Path -LiteralPath $log)){Write-NewBytes $log ([byte[]]@())}
  $exit=Join-Path $outRoot "$name.log.exit.txt"
  Write-NewBytes $exit ([Text.UTF8Encoding]::new($false).GetBytes("$code`n"))
  $commands.Add([ordered]@{executable=$exe;arguments=@($arguments);working_directory=(Get-Location).Path;
    exit_code=$code;log=Hash-Row $log;exit_sidecar=Hash-Row $exit})
  Write-NewJson (Join-Path $outRoot ('command-'+$commands.Count+'.json')) $commands[$commands.Count-1]
  if($code -ne 0){throw "$exe failed($code); retained log $log"}
  Assert-Headroom "after $name"
}

if(!(Test-Path -LiteralPath $inputRoot -PathType Container) -or !(Same-Path (Split-Path $inputRoot) $repositoryBuildRoot)){
  throw 'BuildRoot must be one existing immediate repository build child.'
}
if(!(Same-Path (Split-Path $outRoot) $repositoryBuildRoot) -or (Test-Path -LiteralPath $outRoot)){
  throw 'OutputRoot must be one fresh immediate repository build child.'
}
Assert-NoReparse $inputRoot;Assert-NoReparse $outRoot;Assert-Disjoint $inputRoot $outRoot
$buildReportPath=Join-Path $inputRoot 'build-report.json'
$accepted=Get-Content -Raw -LiteralPath $buildReportPath | ConvertFrom-Json
if(!(Same-Path ([string]$accepted.authoritative_repository) $repoRoot) -or !(Same-Path ([string]$accepted.output_root) $inputRoot) -or
   $accepted.translation_unit_count -ne 24 -or $accepted.internal_production_translation_unit_count -ne 23 -or
   $accepted.build_label -ne 'gameview-material-isolated-unversioned' -or
   $accepted.execution.gameview_material_probe -ne $false -or $accepted.execution.game -ne $false -or $accepted.execution.gpu -ne $false){
  throw 'BuildRoot is not the agreed fresh normal material probe build.'
}
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
Assert-Sequence (@($production.Values)+@('tools/gameview_material_probe.cpp')) @($accepted.translation_units) 'Accepted translation units'
$externalRoots=@((Join-Path $repoRoot '.deps/SDL3-3.4.16/include'),(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),
  (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include'))
$expectedFlags=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
  '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/GR','/Zc:__cplusplus','/DRMLUI_STATIC_LIB',
  '/DRIBTERRA_PROBE_BUILD="gameview-material-isolated-unversioned"',
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
}
if(@($accepted.source_provenance).Count -eq 0 -or @($accepted.source_provenance).Count -ne @($accepted.source_final_binding).Count){
  throw 'Accepted source provenance/final binding is absent or incomplete.'
}
$snapshotRows=@($accepted.source_provenance | Sort-Object snapshot_path)
$actualSnapshot=@(Tree-Rows $sourceRoot)
if($snapshotRows.Count -ne $actualSnapshot.Count){throw 'Accepted source snapshot inventory differs.'}
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
$objectNames=@($production.Keys)+@('gameview_material_probe')
$objects=@($objectNames | ForEach-Object {Join-Path $inputRoot "obj/$_.obj"})
Assert-Sequence $objects @($accepted.objects | ForEach-Object {[string]$_.path}) 'Accepted object identity'
$expectedGenerated=@('network_frame_generated.h','network_generated.h','skill_balance_generated.h','world_save_generated.h')
Assert-Sequence $expectedGenerated @($accepted.generated_headers | ForEach-Object {Split-Path ([string]$_.path) -Leaf}) 'Generated header names'
if(@(Tree-Rows (Join-Path $inputRoot 'generated')).Count -ne 4){throw 'Generated header tree must contain exactly4 files.'}
foreach($row in @($accepted.generated_headers)){
  Assert-Inside ([string]$row.path) (Join-Path $inputRoot 'generated')
  if(!(Same-Path ([string]$row.path) (Join-Path (Join-Path $inputRoot 'generated') (Split-Path ([string]$row.path) -Leaf)))){
    throw 'Generated header must be the exact accepted top-level file.'
  }
}
foreach($entry in $production.GetEnumerator()){
  $command=@($accepted.commands | Where-Object {(Split-Path ([string]$_.log.path) -Leaf) -eq "compile-$($entry.Key).log"})
  if($command.Count -ne 1 -or !(Same-Path ([string]$command[0].executable) ([string]$acceptedCompiler[0].path))){
    throw "Missing/wrong actual production compiler: $($entry.Key)"
  }
  $options=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}else{@()}
  $argv=$expectedFlags+$options+@('/c',(Join-Path $sourceRoot $entry.Value),('/Fo'+(Join-Path $inputRoot "obj/$($entry.Key).obj")))
  Assert-Sequence $argv @($command[0].arguments) "Production argv $($entry.Key)"
}
$acceptedLink=@($accepted.commands | Where-Object {(Split-Path ([string]$_.log.path) -Leaf) -eq 'link-GameViewMaterialProbe.log'})
if($acceptedLink.Count -ne 1 -or !(Same-Path ([string]$acceptedLink[0].executable) ([string]$acceptedLinker[0].path))){
  throw 'Missing/wrong accepted actual link command.'
}
$libraries=@($accepted.link_libraries | ForEach-Object {[string]$_.path})
if(!$libraries.Count -or @($libraries | Sort-Object -Unique).Count -ne $libraries.Count){throw 'Accepted libraries must be unique and nonempty.'}
Assert-Sequence (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
  ('/out:'+(Join-Path $inputRoot 'GameViewMaterialProbe.exe')))+$objects+$libraries) @($acceptedLink[0].arguments) 'Accepted link argv'
if($accepted.shader_compilation.fresh_shader_compilation -ne $true -or $accepted.shader_compilation.program_count -ne 23 -or
   $accepted.shader_compilation.stage_count -ne 46 -or $accepted.shader_compilation.total_file_count -ne 93){
  throw 'Accepted fresh shader cohort must contain23 programs/46 stages/93 files.'
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
foreach($root in @($accepted.control_kit.root,$accepted.ui_kit.root,$accepted.original_art.root,$accepted.material_collection.root)){
  Assert-Disjoint $outRoot ([string]$root)
}
Collect-Bindings $accepted
$reportBinding=Hash-Row $buildReportPath;Add-Binding $reportBinding.path $reportBinding.bytes $reportBinding.sha256
$acceptedTreeBefore=@(Tree-Rows $inputRoot)
foreach($row in $acceptedTreeBefore){Add-Binding $row.path $row.bytes $row.sha256}
$mainPath=Join-Path $repoRoot 'src/client/main.cpp'
$mainRow=Hash-Row $mainPath
if($mainRow.sha256 -ne '7A7018A3E62D1076C662E166EF12228ED5EEFADACCA3BF74ACB5ED76BC2E4439'){
  throw 'Actual main.cpp differs from the reviewed leaf1.29 source.'
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
  $helperSnapshot=Copy-Exclusive $PSCommandPath (Join-Path $outRoot 'source/tools/build_gameview_material_main_isolated.ps1')
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
  $mainObject=Join-Path $outRoot 'obj/main.obj'
  $compileArguments=@($accepted.flags)+@('/c',$mainSnapshot.destination,('/Fo'+$mainObject))
  Invoke-Recorded $compiler $compileArguments 'compile-main'
  $productionObjects=@($production.Keys | ForEach-Object {Join-Path $inputRoot "obj/$_.obj"})
  $executable=Join-Path $outRoot 'Ribterra.exe'
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
  for($i=0;$i -lt 4;$i++){Assert-Tree $externalRoots[$i] @($headerTreesBefore[$i].files) 'Postcompile SDK'}
  foreach($root in @($accepted.control_kit,$accepted.ui_kit,$accepted.original_art,$accepted.material_collection)){
    Assert-Tree ([string]$root.root) @($root.files) 'Referenced asset root'
  }
  foreach($copy in @($mainSnapshot,$helperSnapshot)+$runtimeCopies+$shaderCopies){
    Assert-Row ([ordered]@{path=$copy.destination;bytes=$copy.bytes;sha256=$copy.sha256}) 'Output copy'
  }
  if($commands.Count -ne 2 -or @($commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Actual main compile/link ledger is incomplete.'}
  Assert-Headroom 'before successful main report'
  $outputRows=@(Tree-Rows $outRoot)
  $report=[ordered]@{complete=$true;
    classification='bounded actual main compile/link from accepted23 production objects; executable not launched';
    authoritative_repository=$repoRoot;output_root=$outRoot;accepted_build_root=$inputRoot;
    accepted_build_report=$reportBinding;accepted_build_tree=Hash-Row (Join-Path $outRoot 'accepted-build-tree-before.json');
    source_provenance=@($mainSnapshot,$helperSnapshot);main_source=$mainRow;
    immutable_input_bindings=Hash-Row (Join-Path $outRoot 'input-bindings.json');
    immutable_input_final_binding=Hash-Row (Join-Path $outRoot 'input-final-hashes.json');
    compiled_translation_unit_count=1;reused_production_object_count=23;excluded_object=(Join-Path $inputRoot 'obj/gameview_material_probe.obj');
    compiled_object=Hash-Row $mainObject;reused_objects=@($accepted.objects | Where-Object {(Split-Path ([string]$_.path) -Leaf) -ne 'gameview_material_probe.obj'});
    link_libraries=$accepted.link_libraries;flags=@($accepted.flags);commands=$commands.ToArray();toolchain=$toolchain;
    compiler_environment=$actualEnvironment;
    runtime_files=$runtimeCopies;shader_files=$shaderCopies;shader_file_count=93;shader_compilation=$false;
    executable=Hash-Row $executable;output_files_before_report=$outputRows;
    execution=[ordered]@{game=$false;gpu=$false};
    claims=[ordered]@{normal_startup=$false;production_art=$false;reference_fidelity=$false;full_engine=$false;fps=$false};
    storage=[ordered]@{maximum_output_bytes=134217728;minimum_free_bytes=402653184;bulk_art_copied=$false;
      sdk_libraries_copied=$false;failed_roots_retained=$true}}
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
  for($i=0;$i -lt 4;$i++){
    try{Assert-Tree $externalRoots[$i] @($headerTreesBefore[$i].files) 'Final SDK membership'}catch{$preservationErrors.Add($_.Exception.Message)}
  }
  foreach($root in @($accepted.control_kit,$accepted.ui_kit,$accepted.original_art,$accepted.material_collection)){
    try{Assert-Tree ([string]$root.root) @($root.files) 'Final asset membership'}catch{$preservationErrors.Add($_.Exception.Message)}
  }
  Write-NewJson (Join-Path $outRoot 'preservation-after.json') ([ordered]@{
    complete=($preservationErrors.Count -eq 0);input_final_binding=Hash-Row $finalPath;
    accepted_build_tree_unchanged=($preservationErrors.Count -eq 0);
    external_headers_and_asset_membership_checked=$true;errors=$preservationErrors.ToArray();
    temporary_environment_restored=($env:TEMP -eq $savedTemp -and $env:TMP -eq $savedTmp)})
  if($preservationErrors.Count){throw ('Input preservation failed: '+($preservationErrors -join '; '))}
}
if($completed){
  $report.preservation_after=Hash-Row (Join-Path $outRoot 'preservation-after.json')
  $report.output_files_before_report=@(Tree-Rows $outRoot)
  Assert-Headroom 'before final publication'
  $reportBytes=[Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 30 -Compress)+"`n")
  $used=[int64]0
  foreach($row in @($report.output_files_before_report)){$used += [int64]$row.bytes}
  if($used+$reportBytes.LongLength -gt 134217728){throw 'Final report would exceed the128MiB output bound.'}
  Write-NewBytes (Join-Path $outRoot 'main-build-report.json') $reportBytes
  Write-Output "Actual main compile/link complete without execution: $outRoot"
}
