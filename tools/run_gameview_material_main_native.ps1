param(
  [Parameter(Mandatory=$true)][string]$MainBuildRoot,
  [Parameter(Mandatory=$true)][string]$MaterialRoot,
  [Parameter(Mandatory=$true)][string]$OutputRoot
)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 or newer is required.'}
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if($repo -ne 'C:\Users\fipst\Desktop\ribterra'){throw 'Authoritative repository identity differs.'}
$build=Join-Path $repo 'build'
$mainRoot=(Resolve-Path -LiteralPath $MainBuildRoot).Path
$material=(Resolve-Path -LiteralPath $MaterialRoot).Path
$out=[IO.Path]::GetFullPath($OutputRoot)
$inputs=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$trees=[Collections.Generic.List[object]]::new()
$commands=[Collections.Generic.List[object]]::new()
$errors=[Collections.Generic.List[string]]::new()
$utf8=[Text.UTF8Encoding]::new($false)
$minimumObservedFree=[int64]::MaxValue

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
  return [ordered]@{path=$item.FullName;bytes=[int64]$item.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
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
function Inventory([string]$root){
  No-Reparse $root;$rows=[Collections.Generic.List[object]]::new()
  foreach($item in @(Get-ChildItem -LiteralPath $root -Force -Recurse | Sort-Object FullName)){
    No-Reparse $item.FullName;if($item.PSIsContainer){continue};$r=Row $item.FullName
    $rows.Add([pscustomobject][ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/');path=$r.path;bytes=$r.bytes;sha256=$r.sha256})
  };return $rows.ToArray()
}
function Match-Tree([string]$root,[object[]]$expected){
  $actual=@(Inventory $root);$ordered=@($expected | Sort-Object path)
  if($actual.Count -ne $ordered.Count){throw "Tree membership changed: $root"}
  for($i=0;$i -lt $actual.Count;$i++){
    $r=$ordered[$i];Inside ([string]$r.path) $root
    if(!(Same $actual[$i].path $r.path) -or $actual[$i].relative_path -ne $r.relative_path -or
       $actual[$i].bytes -ne $r.bytes -or $actual[$i].sha256 -ne $r.sha256){throw "Tree payload changed: $root"}
  }
}
function Protect-Tree([string]$root,[object[]]$expected){
  No-Reparse $root
  if((Same $root $out) -or $out.StartsWith(($root+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Output overlaps protected input tree.'
  }
  Match-Tree $root $expected
  if(@($trees | Where-Object {Same $_.root $root}).Count -eq 0){$trees.Add([ordered]@{root=$root;files=@($expected | Sort-Object path)})}
  foreach($r in $expected){Bind $r}
}
function Assert-Sequence([object[]]$a,[object[]]$b,[string]$label){
  if($a.Count -ne $b.Count){throw "$label count differs."}
  for($i=0;$i -lt $a.Count;$i++){if([string]$a[$i] -cne [string]$b[$i]){throw "$label differs at $i."}}
}
function Headroom{
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  $script:minimumObservedFree=[Math]::Min($script:minimumObservedFree,$free)
  if($free -lt 402653184){throw 'Headroom below384MiB.'}
  if(Test-Path -LiteralPath $out){$bytes=[int64]0;foreach($r in @(Inventory $out)){$bytes += $r.bytes};if($bytes -gt 134217728){throw 'Output exceeds128MiB.'}}
}
function Write-New([string]$path,[byte[]]$bytes){
  Inside $path $out;$stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
}
function Json-New([string]$path,[object]$value){Write-New $path $utf8.GetBytes(($value | ConvertTo-Json -Depth 40)+"`n")}
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

foreach($root in @($mainRoot,$material)){
  if(!(Same (Split-Path $root) $build)){throw 'Input roots must be immediate repository build children.'};No-Reparse $root
}
if(!(Same (Split-Path $out) $build) -or (Test-Path -LiteralPath $out)){throw 'Output must be a fresh immediate repository build child.'}
No-Reparse $out
if((Same $mainRoot $material) -or (Same $out $mainRoot) -or (Same $out $material)){throw 'Input/output roots must be distinct.'}
$reportPath=Join-Path $mainRoot 'main-build-report.json'
$report=Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
if($report.complete -ne $true -or !(Same $report.output_root $mainRoot) -or !(Same $report.authoritative_repository $repo) -or
   $report.compiled_translation_unit_count -ne 1 -or $report.reused_production_object_count -ne 23 -or
   $report.shader_file_count -ne 93 -or $report.shader_compilation -ne $false -or $report.execution.game -ne $false -or $report.execution.gpu -ne $false){
  throw 'Main build is not the reviewed strict one-main/23-object source cohort.'
}
$acceptedRoot=[string]$report.accepted_build_root
if(!(Same (Split-Path $acceptedRoot) $build)){throw 'Accepted production build root escapes repository build.'}
if(!(Same $report.accepted_build_report.path (Join-Path $acceptedRoot 'build-report.json'))){throw 'Referenced production report path differs.'}
$accepted=Get-Content -Raw -LiteralPath $report.accepted_build_report.path | ConvertFrom-Json
if(!(Same $accepted.output_root $acceptedRoot) -or $accepted.translation_unit_count -ne 24 -or
   $accepted.internal_production_translation_unit_count -ne 23 -or @($accepted.commands).Count -ne 122 -or
   @($accepted.commands | Where-Object {$_.exit_code -ne 0}).Count -ne 0 -or $accepted.build_label -ne 'gameview-material-isolated-unversioned'){
  throw 'Referenced production cohort is not the strict material build.'
}
if(!(Same $accepted.material_collection.root $material) -or !(Same $accepted.original_art.root (Join-Path $repo 'assets')) -or
   @($accepted.material_collection.files).Count -ne 18 -or @($accepted.original_art.files).Count -ne 181){throw 'Exact original181/material18 asset bindings differ.'}
$exe=Join-Path $mainRoot 'Ribterra.exe'
if(!(Same $report.executable.path $exe) -or $report.main_source.sha256 -ne '7A7018A3E62D1076C662E166EF12228ED5EEFADACCA3BF74ACB5ED76BC2E4439'){
  throw 'Executable or reviewed actual main source identity differs.'
}
$helper=@($report.source_provenance | Where-Object {(Split-Path $_.origin -Leaf) -eq 'build_gameview_material_main_isolated.ps1'})
if($helper.Count -ne 1 -or $helper[0].sha256 -ne 'A6A289415D8457C978C0B15EBC05FD60C377E568128E2EDC213A4FDF6072641D'){throw 'Main build helper is not the independently reviewed source.'}
$compile=@($report.commands | Where-Object {(Split-Path $_.log.path -Leaf) -eq 'compile-main.log'})
$link=@($report.commands | Where-Object {(Split-Path $_.log.path -Leaf) -eq 'link-Ribterra.log'})
if(@($report.commands).Count -ne 2 -or $compile.Count -ne 1 -or $link.Count -ne 1 -or @($report.commands | Where-Object {$_.exit_code -ne 0}).Count){throw 'Actual main compile/link ledger differs.'}
Assert-Sequence @($accepted.flags) @($report.flags) 'Common flags'
if(@($report.flags | Where-Object {$_ -in @('/Zc:inline','/GL')}).Count -or '/std:c++20' -notin $report.flags -or '/W4' -notin $report.flags -or '/WX' -notin $report.flags){throw 'Main flags are not the strict common flags.'}
$mainSnapshot=Join-Path $mainRoot 'source/src/client/main.cpp';$mainObject=Join-Path $mainRoot 'obj/main.obj'
$externalRoots=@((Join-Path $repo '.deps/SDL3-3.4.16/include'),(Join-Path $repo '.deps/SDL3_ttf-3.2.2/include'),
  (Join-Path $repo 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets'),(Join-Path $repo 'vcpkg_installed/x64-windows-release/include'))
$expectedFlags=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX',
  '/permissive-','/GR','/Zc:__cplusplus','/DRMLUI_STATIC_LIB','/DRIBTERRA_PROBE_BUILD="gameview-material-isolated-unversioned"',
  ('/I'+(Join-Path $acceptedRoot 'source/include')),('/I'+(Join-Path $acceptedRoot 'generated')))+
  @($externalRoots | ForEach-Object {'/external:I'+$_})+@('/external:W0')
Assert-Sequence $expectedFlags @($report.flags) 'Source-defined strict flags'
if(!(Same $compile[0].executable $report.toolchain.compiler.path) -or !(Same $link[0].executable $report.toolchain.linker.path) -or
   !(Same $compile[0].working_directory $mainRoot) -or !(Same $link[0].working_directory $mainRoot)){
  throw 'Actual compile/link tool or working directory differs.'
}
$acceptedCompiler=@($accepted.tools | Where-Object {(Split-Path $_.path -Leaf) -eq 'cl.exe'})
$acceptedLinker=@($accepted.tools | Where-Object {(Split-Path $_.path -Leaf) -eq 'link.exe'})
if($acceptedCompiler.Count -ne 1 -or $acceptedLinker.Count -ne 1 -or
   !(Same $acceptedCompiler[0].path $report.toolchain.compiler.path) -or !(Same $acceptedLinker[0].path $report.toolchain.linker.path) -or
   $acceptedCompiler[0].sha256 -ne $report.toolchain.compiler.sha256 -or $acceptedLinker[0].sha256 -ne $report.toolchain.linker.sha256){
  throw 'Actual main tools differ from accepted production object tools.'
}
foreach($command in $report.commands){
  if((Get-Content -Raw -LiteralPath $command.exit_sidecar.path).Trim() -ne '0'){throw 'Main actual exit sidecar is nonzero.'}
}
Assert-Sequence (@($report.flags)+@('/c',$mainSnapshot,('/Fo'+$mainObject))) @($compile[0].arguments) 'Main compile argv'
$production=@($accepted.objects | Where-Object {(Split-Path $_.path -Leaf) -ne 'gameview_material_probe.obj'})
if($production.Count -ne 23 -or @($report.reused_objects).Count -ne 23){throw 'Exact23 reused objects missing.'}
$objectNames=@('foundation','economy','lobby','atomic_file','platform','world','weather','skills','world_save','game_server',
  'gns_transport','protocol','network_session','asset_catalog','texture_atlas','localization','renderer2d','contact_shadow','sky_shadow',
  'view','client_session','game_art','game_ui')
Assert-Sequence @($objectNames | ForEach-Object {Join-Path $acceptedRoot "obj/$_.obj"}) @($production | ForEach-Object {$_.path}) 'Exact production objects'
Assert-Sequence @($production | ForEach-Object {$_.path}) @($report.reused_objects | ForEach-Object {$_.path}) 'Reused production objects'
Assert-Sequence (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',('/out:'+$exe),$mainObject)+
  @($production | ForEach-Object {$_.path})+@($accepted.link_libraries | ForEach-Object {$_.path})) @($link[0].arguments) 'Main link argv'
foreach($key in @('include','lib','vs_install_dir','vc_tools_version','windows_sdk_dir','windows_sdk_version')){
  if(![string]$report.compiler_environment.$key -or [string]$report.compiler_environment.$key -ne [string]$accepted.compiler_environment.$key){throw 'Compiler/SDK search environment differs.'}
}
Collect $report;Collect $accepted
foreach($path in @($reportPath,$report.immutable_input_bindings.path,$report.immutable_input_final_binding.path,$report.accepted_build_tree.path,$report.preservation_after.path)){
  Bind (Row $path);Collect (Get-Content -Raw -LiteralPath $path | ConvertFrom-Json)
}
$preserved=Get-Content -Raw -LiteralPath $report.preservation_after.path | ConvertFrom-Json
if($preserved.complete -ne $true -or @($preserved.errors).Count -ne 0){throw 'Main build input preservation failed.'}
Protect-Tree $mainRoot @($report.output_files_before_report+(Row $reportPath | ForEach-Object {
  [pscustomobject][ordered]@{path=$_.path;relative_path='main-build-report.json';bytes=$_.bytes;sha256=$_.sha256}}))
Protect-Tree $acceptedRoot @(Get-Content -Raw -LiteralPath $report.accepted_build_tree.path | ConvertFrom-Json)
foreach($tree in @($accepted.control_kit,$accepted.ui_kit,$accepted.original_art,$accepted.material_collection)){Protect-Tree $tree.root @($tree.files)}
$decodedHeaders=Expand-ExternalHeaders $accepted
Assert-Sequence $externalRoots @($decodedHeaders.trees_before | ForEach-Object {$_.root}) 'External SDK roots before'
Assert-Sequence $externalRoots @($decodedHeaders.trees_after | ForEach-Object {$_.root}) 'External SDK roots after'
foreach($tree in @($decodedHeaders.trees_before)+@($decodedHeaders.trees_after)){Protect-Tree $tree.root @($tree.files)}
if(@(Inventory (Join-Path $mainRoot 'assets/shaders')).Count -ne 93 -or @($report.runtime_files).Count -ne 3){throw 'Pinned shader/runtime closure differs.'}
$runtimeRelative=@('SDL3.dll','SDL3_ttf.dll','assets/fonts/NotoSans-Regular.ttf')
Assert-Sequence @($runtimeRelative | ForEach-Object {Join-Path $mainRoot $_}) @($report.runtime_files | ForEach-Object {$_.destination}) 'Runtime destinations'
$ordinaryPrefs=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Ribterra/Ribterra'
No-Reparse $ordinaryPrefs;$prefsExisted=Test-Path -LiteralPath $ordinaryPrefs -PathType Container
$prefsBefore=if($prefsExisted){@(Inventory $ordinaryPrefs)}else{@()}
foreach($r in $prefsBefore){Bind $r}
Headroom
New-Item -ItemType Directory -Path $out | Out-Null
New-Item -ItemType Directory -Path (Join-Path $out 'source') | Out-Null
$snapshots=@(Copy-Code $PSCommandPath 'run_gameview_material_main_native.ps1';Copy-Code (Join-Path $repo 'tests/gameview_material_main_evidence_tests.py') 'gameview_material_main_evidence_tests.py')
$before=@($inputs.Values | Sort-Object path)
Json-New (Join-Path $out 'input-bindings.json') $before
Json-New (Join-Path $out 'input-trees-before.json') $trees.ToArray()
$profiles=[Collections.Generic.List[object]]::new();$completed=$false
try{
  foreach($id in @('main-menu','main-material-menu','main-world','main-material-world')){
    Headroom;$capture=Join-Path $out "$id.png";$info=Join-Path $out "$id.renderer-info.json"
    $argv=@('--catalog-assets',(Join-Path $repo 'assets'),'--renderer-info',$info)
    if($id.EndsWith('menu')){$argv+=@('--menu-capture',$capture)}else{$argv+=@('--demo','--capture',$capture)}
    if($id.StartsWith('main-material-')){$argv+=@('--material-collection',$material)}
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$exe;$start.WorkingDirectory=$mainRoot
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($argument in $argv){$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start;$started=[DateTimeOffset]::UtcNow;$timer=[Diagnostics.Stopwatch]::StartNew()
    $code=$null;$timedOut=$false;$stdout='';$stderr='';$launchError='';$pidRecorded=$null
    try{
      if(!$process.Start()){throw 'Process.Start returned false.'};$pidRecorded=$process.Id
      $readOut=$process.StandardOutput.ReadToEndAsync();$readError=$process.StandardError.ReadToEndAsync()
      if(!$process.WaitForExit(120000)){$timedOut=$true;$process.Kill();$process.WaitForExit()}
      $stdout=$readOut.GetAwaiter().GetResult();$stderr=$readError.GetAwaiter().GetResult();$code=$process.ExitCode
    }catch{
      $launchError=$_.Exception.Message
      if($null -ne $pidRecorded){try{if(!$process.HasExited){$process.Kill();$process.WaitForExit()}}catch{$launchError+=' ; owned process termination failed: '+$_.Exception.Message}}
    }finally{$timer.Stop();$process.Dispose()}
    $log=Join-Path $out "$id.log";$sidecar=Join-Path $out "$id.log.exit.txt"
    Write-New $log $utf8.GetBytes("STDOUT`n$stdout`nSTDERR`n$stderr")
    Write-New $sidecar $utf8.GetBytes($(if($null -eq $code){"unavailable`n"}else{"$code`n"}))
    $row=[ordered]@{id=$id;executable=Row $exe;arguments=$argv;actual_argv=@($exe)+$argv;working_directory=$mainRoot;
      started_utc=$started.ToString('o');ended_utc=[DateTimeOffset]::UtcNow.ToString('o');elapsed_milliseconds=$timer.Elapsed.TotalMilliseconds;
      diagnostic_duration_scope='whole ordinary process; not FPS/GPU timing';process_id=$pidRecorded;actual_subprocess=($null -ne $pidRecorded);
      exit_code=$code;timed_out=$timedOut;launch_error=$launchError;log=Row $log;exit_sidecar=Row $sidecar}
    $commands.Add($row);Json-New (Join-Path $out "case-$id.json") $row
    if($null -eq $code -or $code -ne 0 -or $timedOut -or $launchError){throw "Actual main case failed: $id ; exit=$code ; $launchError"}
    $row.capture=Row $capture;$row.renderer_info=Row $info;$row.profile=Profile $id;$profiles.Add($row.profile)
    Headroom
  };$completed=$true
}catch{$errors.Add($_.Exception.Message)}finally{
  $after=@(foreach($r in $before){
    try{$a=Row $r.path;[ordered]@{path=$r.path;bytes=$r.bytes;sha256=$r.sha256;after_bytes=$a.bytes;after_sha256=$a.sha256;unchanged=($r.bytes -eq $a.bytes -and $r.sha256 -eq $a.sha256)}}
    catch{[ordered]@{path=$r.path;bytes=$r.bytes;sha256=$r.sha256;unchanged=$false;error=$_.Exception.Message}}
  })
  Json-New (Join-Path $out 'input-final-hashes.json') $after
  if(@($after | Where-Object {$_.unchanged -ne $true}).Count){$errors.Add('Protected input bytes changed during actual main execution.')}
  foreach($tree in $trees){try{Match-Tree $tree.root @($tree.files)}catch{$errors.Add($_.Exception.Message)}}
  $prefsAfter=@();$prefsExistAfter=Test-Path -LiteralPath $ordinaryPrefs -PathType Container
  try{
    if((Test-Path -LiteralPath $ordinaryPrefs -PathType Container) -ne $prefsExisted){throw 'Ordinary preference directory membership changed.'}
    if($prefsExistAfter){$prefsAfter=@(Inventory $ordinaryPrefs);Match-Tree $ordinaryPrefs $prefsBefore}
    Headroom
  }catch{$errors.Add($_.Exception.Message)}
  Json-New (Join-Path $out 'preservation-after.json') ([ordered]@{complete=($errors.Count -eq 0);errors=$errors.ToArray();
    input_final_binding=Row (Join-Path $out 'input-final-hashes.json');protected_tree_count=$trees.Count;
    ordinary_preferences=[ordered]@{root=$ordinaryPrefs;existed_before=$prefsExisted;files_before=$prefsBefore;
      existed_after=$prefsExistAfter;files_after=$prefsAfter}})
}
$nativeRows=@(Inventory $out)
$ledger=[ordered]@{schema=1;complete=($completed -and $errors.Count -eq 0);accepted=($completed -and $errors.Count -eq 0);
  classification='actual ordinary main automatic startup/capture by reference to original assets; no package/material visibility/FPS acceptance';
  repository=$repo;main_build_root=$mainRoot;accepted_build_root=$acceptedRoot;material_root=$material;output_root=$out;
  main_build_report=Row $reportPath;source_provenance=$snapshots;input_bindings=Row (Join-Path $out 'input-bindings.json');
  input_trees=Row (Join-Path $out 'input-trees-before.json');input_final_binding=Row (Join-Path $out 'input-final-hashes.json');
  preservation=Row (Join-Path $out 'preservation-after.json');commands=$commands.ToArray();process_count=$commands.Count;
  automatic_contract=[ordered]@{window=@(1440,900);capture_frame_index=2;exit_after_frames=3;seed=70421;demo_preadvances=1700;
    profiles='capture filename + .test-profile- + secure_token(16), retained empty climate-v1';user_profile_scope='Windows APPDATA/Ribterra/Ribterra hash/membership before/after; no contents retained'};
  native_output_before_ledger=$nativeRows;integrity_errors=$errors.ToArray();
  claims=[ordered]@{ordinary_startup_captures=($completed -and $errors.Count -eq 0);material_draw_count=$null;fps=$null;gpu_execution_ms=$null;
    default_package=$false;production_neutral_art=$false;reference_fidelity=$false;full_engine=$false};
  storage=[ordered]@{maximum_output_bytes=134217728;minimum_free_bytes=402653184;minimum_observed_free_bytes=$minimumObservedFree;
    bulk_art_copied=$false;failed_roots_retained=$true}}
$ledgerBytes=$utf8.GetBytes(($ledger | ConvertTo-Json -Depth 40)+"`n");$used=[int64]0;foreach($r in $nativeRows){$used += $r.bytes}
if($used+$ledgerBytes.LongLength -gt 134217728){throw 'Final ledger would exceed128MiB; existing outputs retained.'}
if([int64](Get-Volume -DriveLetter C).SizeRemaining -lt 402653184+$ledgerBytes.LongLength){throw 'Final ledger would consume required384MiB headroom.'}
Write-New (Join-Path $out 'main-native-execution.json') $ledgerBytes
Headroom
if(!$ledger.complete){[Console]::Error.WriteLine($errors -join [Environment]::NewLine);exit 70}
Write-Output "Actual four-case ordinary main capture completed: $out"
exit 0
