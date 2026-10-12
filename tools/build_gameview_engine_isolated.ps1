param(
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [Parameter(Mandatory=$true)][string]$KitRoot)

$ErrorActionPreference='Stop'
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

# The probe is supplied by the GameView change. Fail before creating output when it is absent.
$probeSource=Join-Path $repoRoot 'tools/gameview_engine_probe.cpp'
if(!(Test-Path -LiteralPath $probeSource -PathType Leaf)){
  throw "GameView engine probe source is required: $probeSource"
}

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

# Bind the current launcher selection and prior accepted authored-sky evidence.
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

$acceptedRoot=Join-Path $repoRoot 'build/sky-authored-2026-10-08-attempt10'
$acceptedReportPath=Join-Path $acceptedRoot 'build-report.json'
$acceptedShaderRoot=Join-Path $acceptedRoot 'assets/shaders'
$acceptedShaderManifest=Join-Path $acceptedShaderRoot 'manifest.json'
$acceptedNativeProbe=Join-Path $acceptedRoot 'AuthoredSkyNativeProbe.exe'
$protectedPaths=@($launcherPath,$reworkLauncherPath,$currentBuildPointer,$launcherExecutable,
  $reworkExecutable,$acceptedNativeProbe,$acceptedReportPath,$acceptedShaderManifest)
$protectedBefore=@($protectedPaths | ForEach-Object {
  if(!(Test-Path -LiteralPath $_ -PathType Leaf)){throw "Missing protected input: $_"}
  Get-HashRow $_
})

# Reuse only the accepted attempt10 shader cohort after report, manifest, artifact,
# and current shader-source closure validation.
$acceptedReport=Get-Content -Raw -LiteralPath $acceptedReportPath | ConvertFrom-Json
$reportedArtifacts=@($acceptedReport.shader_reuse.artifacts)
if($reportedArtifacts.Count -ne 68){throw "Attempt10 report must anchor exactly 68 shader artifacts, got $($reportedArtifacts.Count)"}
$acceptedShaderFiles=@(Get-ChildItem -LiteralPath $acceptedShaderRoot -File | Sort-Object Name)
if($acceptedShaderFiles.Count -ne 69){throw "Attempt10 shader directory must contain 68 artifacts plus manifest, got $($acceptedShaderFiles.Count)"}
if((Get-FileHash -LiteralPath $acceptedShaderManifest -Algorithm SHA256).Hash -ne
   ([string]$acceptedReport.shader_reuse.manifest.sha256).ToUpperInvariant()){
  throw 'Attempt10 shader manifest no longer matches its build report'
}
foreach($artifact in $reportedArtifacts){
  $name=[IO.Path]::GetFileName([string]$artifact.file)
  if($name -ne [string]$artifact.file){throw "Invalid shader artifact name in attempt10 report: $($artifact.file)"}
  $artifactPath=Join-Path $acceptedShaderRoot $name
  if(!(Test-Path -LiteralPath $artifactPath -PathType Leaf)){throw "Missing attempt10 shader artifact: $name"}
  if((Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash -ne ([string]$artifact.sha256).ToUpperInvariant()){
    throw "Attempt10 shader artifact changed: $name"
  }
}
$shaderClosure=@($acceptedReport.shader_reuse.current_source_closure)
$currentShaderRoot=Join-Path $repoRoot 'shaders'
$currentShaderFiles=@(Get-ChildItem -LiteralPath $currentShaderRoot -File -Recurse | Sort-Object FullName)
if($currentShaderFiles.Count -ne $shaderClosure.Count){throw 'Current shader inventory differs from attempt10 source closure'}
$currentShaderClosure=@()
foreach($shader in $shaderClosure){
  $current=Join-Path $currentShaderRoot ([string]$shader.relative_path)
  if(!(Test-Path -LiteralPath $current -PathType Leaf)){throw "Current shader source missing: $($shader.relative_path)"}
  if((Get-FileHash -LiteralPath $current -Algorithm SHA256).Hash -ne ([string]$shader.sha256).ToUpperInvariant()){
    throw "Current shader source changed since attempt10: $($shader.relative_path)"
  }
  $row=Get-HashRow $current
  $currentShaderClosure += [ordered]@{relative_path=[string]$shader.relative_path;path=$row.path;
    bytes=$row.bytes;sha256=$row.sha256}
}

$translationUnits=[ordered]@{
  foundation='src/foundation/foundation.cpp'
  economy='src/foundation/economy.cpp'
  lobby='src/foundation/lobby.cpp'
  atomic_file='src/foundation/atomic_file.cpp'
  platform='src/foundation/platform.cpp'
  world='src/sim/world.cpp'
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
  gameview_engine_probe='tools/gameview_engine_probe.cpp'
}
$incClosure=@(
  'src/sim/world_settlement.inc','src/sim/world_economy.inc','src/sim/world_wilderness.inc',
  'src/sim/world_nations.inc','src/sim/world_logistics.inc','src/sim/world_logistics_validation.inc',
  'src/sim/world_manual.inc','src/sim/world_manual_validation.inc','src/sim/world_autonomy.inc',
  'src/sim/world_professions.inc','src/sim/world_professions_validation.inc','src/sim/world_work.inc',
  'src/sim/world_work_validation.inc','src/sim/world_economy_api.inc','src/sim/world_wilderness_api.inc',
  'src/sim/world_nations_api.inc','src/server/game_server_lobby.inc','src/client/client_production_group.inc')
$snapshotFiles=@($translationUnits.Values)+$incClosure+@(
  'tools/compile_skill_balance.py','data/skills.json','schemas/world_save.fbs','schemas/network.fbs',
  'schemas/network_frame.fbs','CMakeLists.txt','tools/build_gameview_engine_isolated.ps1')

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

  $shaderReuseRows=@()
  foreach($file in $acceptedShaderFiles){
    $destination=Join-Path $outPath ('assets/shaders/'+$file.Name)
    Assert-NewPath $destination
    Copy-Item -LiteralPath $file.FullName -Destination $destination
    $originRow=Get-HashRow $file.FullName
    $destinationRow=Get-HashRow $destination
    if($originRow.sha256 -ne $destinationRow.sha256){throw "Copied shader hash mismatch: $($file.Name)"}
    $shaderReuseRows += [ordered]@{file=$file.Name;origin=$originRow.path;destination=$destinationRow.path;
      bytes=$destinationRow.bytes;sha256=$destinationRow.sha256}
  }
  Test-Headroom 'after source and shader snapshot'

  . 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:TEMP=Join-Path $outPath 'compiler-temp'
  $env:TMP=$env:TEMP
  $env:PYTHONDONTWRITEBYTECODE='1'

  $python='C:/Python314/python.exe'
  $flatc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe'
  foreach($tool in @('cl.exe','link.exe')){
    if($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)){throw "Missing VS18 amd64 tool: $tool"}
  }
  foreach($tool in @($python,$flatc)){
    if(!(Test-Path -LiteralPath $tool -PathType Leaf)){throw "Missing pinned build tool: $tool"}
  }
  $toolRows=@(Get-HashRow (Get-Command cl.exe).Source; Get-HashRow (Get-Command link.exe).Source;
    Get-HashRow $python; Get-HashRow $flatc)

  Invoke-Checked $python @((Join-Path $sourceRoot 'tools/compile_skill_balance.py'),
    (Join-Path $sourceRoot 'data/skills.json'),(Join-Path $outPath 'generated/skill_balance_generated.h')) 'generate-skill-balance.log'
  foreach($schema in @('world_save','network','network_frame')){
    Invoke-Checked $flatc @('--cpp','--scoped-enums','--gen-object-api','-o',(Join-Path $outPath 'generated'),
      (Join-Path $sourceRoot "schemas/$schema.fbs")) "generate-$schema.log"
  }

  $common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
    '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/GR','/Zc:__cplusplus',
    '/DRIBTERRA_PROBE_BUILD="gameview-engine-isolated-unversioned"',
    ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $outPath 'generated')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include/GameNetworkingSockets')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
  foreach($entry in $translationUnits.GetEnumerator()){
    $unitOptions=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}else{@()}
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
    'libcrypto.lib','icuin.lib','icuuc.lib','icudt.lib','libpng16.lib','zs.lib')
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
    ('/out:'+(Join-Path $outPath 'GameViewEngineProbe.exe')))+$objects+$linkLibraries) 'link-GameViewEngineProbe.log'

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
  Test-FileTreeRows $kitPath $kitRows 'KitRoot'
  foreach($shader in $shaderClosure){
    $current=Join-Path $currentShaderRoot ([string]$shader.relative_path)
    if((Get-FileHash -LiteralPath $current -Algorithm SHA256).Hash -ne ([string]$shader.sha256).ToUpperInvariant()){
      throw "Shader source closure changed during isolated build: $($shader.relative_path)"
    }
  }
  Write-NewJson (Join-Path $outPath 'source-final-hashes.json') $sourceFinalRows 7

  $objectRows=@($objects | ForEach-Object {Get-HashRow $_})
  $generatedRows=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'generated') -File | Sort-Object Name |
    ForEach-Object {Get-HashRow $_.FullName})
  if($generatedRows.Count -ne 4){throw "Expected exactly four generated headers, got $($generatedRows.Count)"}
  $executableRow=Get-HashRow (Join-Path $outPath 'GameViewEngineProbe.exe')
  $exitSidecars=@(Get-ChildItem -LiteralPath $outPath -File -Filter '*.log.exit.txt' | Sort-Object Name)
  if($exitSidecars.Count -ne 27){throw "Expected 27 command exit sidecars, got $($exitSidecars.Count)"}
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
    classification='fresh bounded standalone GameView engine probe build; executable not launched; diagnostic controls only'
    build_label='gameview-engine-isolated-unversioned'
    authoritative_repository=$repoRoot
    output_root=$outPath
    translation_unit_count=22
    internal_production_translation_unit_count=21
    translation_units=@($translationUnits.Values)
    nested_inc_closure=$incClosure
    source_provenance=$sourceRows
    source_final_binding=$sourceFinalRows
    objects=$objectRows
    executable=$executableRow
    tools=$toolRows
    generated_headers=$generatedRows
    link_libraries=$linkLibraryRows
    runtime_files=$runtimeRows
    command_evidence=$commandEvidence
    protected_binding=$protectedFinal
    control_kit=[ordered]@{
      classification='separate generated diagnostic-only controls; referenced by path; not staged or promoted as catalog art'
      root=$kitPath
      files=$kitRows
      bulk_staged=$false
    }
    shader_reuse=[ordered]@{
      classification='accepted attempt10 shader reuse only; no fresh shader compilation or shader acceptance claim'
      origin_root=$acceptedShaderRoot
      accepted_build_report=Get-HashRow $acceptedReportPath
      current_source_closure=$currentShaderClosure
      files=$shaderReuseRows
      total_file_count=$shaderReuseRows.Count
      fresh_shader_compilation=$false
    }
    execution=[ordered]@{
      gameview_engine_probe=$false
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

Write-Output "Isolated GameView engine probe build complete without execution: $outPath"
