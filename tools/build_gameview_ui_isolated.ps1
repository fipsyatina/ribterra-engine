param(
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [Parameter(Mandatory=$true)][string]$KitRoot,
  [Parameter(Mandatory=$true)][string]$UiKitRoot,
  [string]$ProbeSource='tools/gameview_ui_probe.cpp')

$ErrorActionPreference='Stop'
$commandRows=[Collections.Generic.List[object]]::new()
if($PSVersionTable.PSVersion.Major -lt 7){
  throw 'PowerShell 7 or newer is required; invoke this helper with pwsh, not Windows PowerShell.'
}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
$outPath=[IO.Path]::GetFullPath($OutputRoot)
$kitPath=[IO.Path]::GetFullPath($KitRoot)
$uiKitPath=[IO.Path]::GetFullPath($UiKitRoot)
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $outPath),$buildRoot)){
  throw 'OutputRoot must be one new immediate child of the authoritative repository build directory'
}
if(Test-Path -LiteralPath $outPath){throw 'Refusing to reuse an existing output root'}
if(!(Test-Path -LiteralPath $kitPath -PathType Container)){throw "Missing separate generated engine control kit: $kitPath"}
if($kitPath.StartsWith(($outPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
   $outPath.StartsWith(($kitPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
   [StringComparer]::OrdinalIgnoreCase.Equals($kitPath,$outPath)){
  throw 'KitRoot must be separate from OutputRoot; this helper points to the kit and does not bulk-stage it'
}

# Both referenced kits are confined, distinct descendants; no reparse traversal.
foreach($root in @($buildRoot,$kitPath,$uiKitPath)){
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
foreach($pair in @(@($outPath,$uiKitPath),@($kitPath,$uiKitPath))){
  $a=$pair[0];$b=$pair[1]
  if([StringComparer]::OrdinalIgnoreCase.Equals($a,$b) -or
     $a.StartsWith(($b+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase) -or
     $b.StartsWith(($a+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw 'OutputRoot and both kits must be distinct non-overlapping paths'
  }
}

function Test-Headroom([string]$phase) {
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below 384MiB during ${phase}: $free"}
  if(Test-Path -LiteralPath $outPath){
    $measurement=Get-ChildItem -LiteralPath $outPath -File -Recurse | Measure-Object Length -Sum
    $used=if($null -eq $measurement.Sum){[int64]0}else{[int64]$measurement.Sum}
    if($used -gt 134217728){throw "Isolated output exceeds 128MiB during ${phase}: $used"}
  }
}

function Assert-NewPath([string]$path) {
  if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing path: $path"}
}

function Write-NewJson([string]$path,[object]$value,[int]$depth=10) {
  Assert-NewPath $path
  [IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth $depth))
}

function Invoke-Checked([string]$exe,[object[]]$arguments,[string]$log) {
  Test-Headroom "before $log"
  $logPath=Join-Path $outPath $log
  Assert-NewPath $logPath
  $savedErrorActionPreference=$ErrorActionPreference
  $exitCode=-1
  try {
    $ErrorActionPreference='Continue'
    & $exe @arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
    $exitCode=[int]$LASTEXITCODE
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

function Get-HashRow([string]$path) {
  $item=Get-Item -LiteralPath $path
  return [ordered]@{path=$item.FullName;bytes=[int64]$item.Length;
    sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash}
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
  return @(Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName | ForEach-Object {
    $row=Get-HashRow $_.FullName
    if(($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw "Reparse file forbidden: $($_.FullName)"}
    [ordered]@{relative_path=[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/');
      path=$row.path;bytes=$row.bytes;sha256=$row.sha256}
  })
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

# The bounded UI probe is supplied separately. Bind only a repository source.
$probePath=[IO.Path]::GetFullPath((Join-Path $repoRoot $ProbeSource))
if(!$probePath.StartsWith(($repoRoot+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
  throw 'ProbeSource must remain inside the authoritative repository'
}
if(!(Test-Path -LiteralPath $probePath -PathType Leaf)){
  throw "GameView UI probe source is required: $probePath"
}
$probeRelative=[IO.Path]::GetRelativePath($repoRoot,$probePath).Replace('\','/')

# Validate and bind the separately generated diagnostic-only control kit. Nothing is copied.
$kitManifestPath=Join-Path $kitPath 'manifest.json'
$kitNoticePath=Join-Path $kitPath 'NOTICE.txt'
foreach($required in @($kitManifestPath,$kitNoticePath)){
  if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Invalid KitRoot; missing: $required"}
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
  $assetPath=[IO.Path]::GetFullPath((Join-Path $kitPath ([string]$asset.path)))
  if(!$assetPath.StartsWith(($kitPath+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){
    throw "Kit manifest path escapes KitRoot: $($asset.path)"
  }
  if(!(Test-Path -LiteralPath $assetPath -PathType Leaf)){throw "Missing kit asset: $assetPath"}
  $assetItem=Get-Item -LiteralPath $assetPath
  if([int64]$assetItem.Length -ne [int64]$asset.bytes){throw "Kit asset byte mismatch: $($asset.path)"}
  if((Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash -ne ([string]$asset.sha256).ToUpperInvariant()){
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
  $path=[IO.Path]::GetFullPath((Join-Path $uiKitPath ([string]$asset.path)))
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

# Bind installed static SDK/header/target metadata by reference, never copy its libraries.
$dependencyRelative=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/RmlUi') -File -Recurse | ForEach-Object {$_.FullName})+
  @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/rmlui') -File -Filter '*.cmake' | ForEach-Object {$_.FullName})+
  @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/freetype') -File -Filter '*.cmake' | ForEach-Object {$_.FullName})
$dependencyRows=@($dependencyRelative | Sort-Object -Unique | ForEach-Object {Get-HashRow $_})
$versionText=Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/share/rmlui/RmlUiConfigVersion.cmake')
if($versionText -notmatch 'set\(PACKAGE_VERSION "6\.3"\)'){throw 'The exact installed RmlUi6.3 SDK is required'}

# Bind the current launcher selection. The fresh shader cohort below never reads
# or copies an accepted/retained shader directory.
$currentBuildPointer=Join-Path $repoRoot 'dist/current-build.txt'
$launcherPath=Join-Path $repoRoot 'Play Ribterra.cmd'
$reworkLauncherPath=Join-Path $repoRoot 'Play Ribterra Rework.cmd'
foreach($required in @($currentBuildPointer,$launcherPath,$reworkLauncherPath)){
  if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing protected launcher input: $required"}
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
$currentShaderFiles=@(Get-ChildItem -LiteralPath $currentShaderRoot -File -Recurse | Sort-Object FullName)
if($currentShaderFiles.Count -eq 0){throw 'Current shader source closure is empty'}
$shaderSourceRelativePaths=@($currentShaderFiles | ForEach-Object {
  [IO.Path]::GetRelativePath($repoRoot,$_.FullName).Replace('\','/')
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
  gameview_ui_probe=$probeRelative
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
  'tools/build_gameview_ui_isolated.ps1','tools/engine_control_probe.hpp',
  'tools/export_engine_control_kit.py','tools/export_material_master.py',
  'tools/export_engine_ui_kit.py','tools/asset_tool.py','content/asset_sources.json',
  'licenses/Cormorant-OFL.txt','licenses/Noto-OFL.txt','licenses/PTSerif-OFL.txt','licenses/Ribterra-Assets.txt',
  'tools/run_gameview_ui_native.ps1','tests/engine_control_kit_tests.py',
  'tests/gameview_cumulative_evidence_tests.py',
  'tests/gameview_session_time_evidence_tests.py',
  'tests/gameview_opacity_evidence_tests.py',
  'tests/gameview_ui_evidence_tests.py',
  'tests/gameview_ui_scene_evidence_tests.py',
  'tests/weather_fixture.hpp')+$shaderSourceRelativePaths

Test-Headroom 'before output creation'
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($directory in @('source','obj','compiler-temp','generated','assets','assets/fonts','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outPath $directory) | Out-Null
}
Write-NewJson (Join-Path $outPath 'protected-before.json') $protectedBefore 6

try {
  foreach($relative in $snapshotFiles){
    $source=Join-Path $repoRoot $relative
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing snapshot input: $source"}
    $destination=Join-Path (Join-Path $outPath 'source') $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
  }
  Copy-Item -LiteralPath (Join-Path $repoRoot 'include') -Destination (Join-Path $outPath 'source/include') -Recurse
  $sourceRoot=Join-Path $outPath 'source'
  $sourceRows=@(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($sourceRoot,$_.FullName)
    $origin=Join-Path $repoRoot $relative
    if(!(Test-Path -LiteralPath $origin -PathType Leaf)){throw "Snapshot has no authoritative origin: $relative"}
    $originRow=Get-HashRow $origin
    $snapshotRow=Get-HashRow $_.FullName
    if($originRow.sha256 -ne $snapshotRow.sha256){throw "Snapshot hash mismatch: $relative"}
    [ordered]@{relative_path=$relative.Replace('\','/');repository_path=$originRow.path;
      snapshot_path=$snapshotRow.path;bytes=$snapshotRow.bytes;sha256=$snapshotRow.sha256}
  })
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
    '/DRIBTERRA_PROBE_BUILD="gameview-ui-isolated-unversioned"',
    ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $outPath 'generated')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
  foreach($entry in $translationUnits.GetEnumerator()){
    $unitOptions=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}
      elseif($entry.Key -eq 'gameview_ui_probe'){@('/Zc:inline')}
      else{@()}
    Invoke-Checked 'cl.exe' ($common+$unitOptions+@('/c',(Join-Path $sourceRoot $entry.Value),
      ('/Fo'+(Join-Path $outPath ('obj/'+$entry.Key+'.obj'))))) ('compile-'+$entry.Key+'.log')
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
  $objects=@($translationUnits.Keys | ForEach-Object {Join-Path $outPath ('obj/'+$_.ToString()+'.obj')})
  Invoke-Checked 'link.exe' (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
    ('/out:'+(Join-Path $outPath 'GameViewUiProbe.exe')))+$objects+$linkLibraries) 'link-GameViewUiProbe.log'

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
    Copy-Item -LiteralPath $origin -Destination $destination
    $originRow=Get-HashRow $origin
    $destinationRow=Get-HashRow $destination
    if($originRow.sha256 -ne $destinationRow.sha256){throw "Runtime copy hash mismatch: $($runtime[1])"}
    $runtimeRows += [ordered]@{origin=$originRow.path;destination=$destinationRow.path;
      bytes=$destinationRow.bytes;sha256=$destinationRow.sha256}
  }

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
  Test-FileTreeRows $kitPath $kitRows 'KitRoot'
  Test-FileTreeRows $uiKitPath $uiKitRows 'UiKitRoot'
  $dependencyFinal=@($dependencyRows | ForEach-Object {
    $after=Get-HashRow $_.path
    [ordered]@{path=$_.path;bytes=$_.bytes;sha256=$_.sha256;after_bytes=$after.bytes;
      after_sha256=$after.sha256;unchanged=($_.bytes -eq $after.bytes -and $_.sha256 -eq $after.sha256)}
  })
  if(@($dependencyFinal | Where-Object {-not $_.unchanged}).Count){throw 'Installed RmlUi dependency cohort changed'}
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
  $generatedRows=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'generated') -File | Sort-Object Name |
    ForEach-Object {Get-HashRow $_.FullName})
  if($generatedRows.Count -ne 4){throw "Expected exactly four generated headers, got $($generatedRows.Count)"}
  $executableRow=Get-HashRow (Join-Path $outPath 'GameViewUiProbe.exe')
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

  $report=[ordered]@{
    classification='fresh bounded standalone GameView UI probe and shader build; executable not launched; diagnostic controls only'
    build_label='gameview-ui-isolated-unversioned'
    authoritative_repository=$repoRoot
    output_root=$outPath
    translation_unit_count=24
    internal_production_translation_unit_count=23
    translation_units=@($translationUnits.Values)
    nested_inc_closure=$incClosure
    source_provenance=$sourceRows
    source_final_binding=$sourceFinalRows
    objects=$objectRows
    executable=$executableRow
    tools=$toolRows
    generated_headers=$generatedRows
    link_libraries=$linkLibraryRows
    link_libraries_final_binding=$linkLibraryFinal
    ui_dependencies=[ordered]@{rmlui_version='6.3';static_core=$true;bulk_copied=$false;files_before=$dependencyRows;files_after=$dependencyFinal}
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
      gameview_ui_probe=$false
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
}

Write-Output "Isolated GameView UI probe and fresh shader build complete without execution: $outPath"
