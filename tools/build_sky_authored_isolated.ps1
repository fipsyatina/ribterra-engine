param(
  [Parameter(Mandatory=$true)][string]$OutputRoot,
  [switch]$IncludeNativeProbe)
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
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $outPath),$buildRoot)){
  throw 'OutputRoot must be one new immediate child of the authoritative repository build directory'
}
if(Test-Path -LiteralPath $outPath){throw 'Refusing to reuse an existing output root'}

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

function Write-NewJson([string]$path,[object]$value,[int]$depth=8) {
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
    # Native stderr is retained evidence; the real process exit code is authoritative.
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

$protected=@(
  'build/concept-rebuild-validation/LocalOcclusionProbe.exe',
  'build/concept-rebuild-validation/GroundForwardProbe.exe',
  'build/concept-rebuild-validation/compiled-shaders/manifest.json',
  'build/contact-targeted-2026-10-07-attempt03/ContactShadowProbe.exe',
  'build/contact-targeted-2026-10-07-attempt03/assets/shaders/manifest.json',
  'build/sky-direct-2026-10-07-attempt03/SkyDirectProbe.exe',
  'build/sky-direct-2026-10-07-attempt03/assets/shaders/manifest.json',
  'build/sky-shadow-2026-10-07-attempt09/SkyShadowProbe.exe',
  'build/sky-shadow-2026-10-07-attempt09/assets/shaders/manifest.json',
  'build/sky-shadow-2026-10-07-attempt09/build-report.json',
  'build/sky-shadow-2026-10-07-attempt09/shader-contract-positive.log',
  'build/sky-shadow-2026-10-07-attempt09/shader-contract-positive.log.exit.txt')
$protectedBefore=@($protected | ForEach-Object {
  $path=Join-Path $repoRoot $_
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing protected input: $path"}
  Get-HashRow $path
})

if($IncludeNativeProbe){
  $nativeProbeSource=Join-Path $repoRoot 'tools/authored_sky_native_probe.cpp'
  if(!(Test-Path -LiteralPath $nativeProbeSource -PathType Leaf)){
    throw "Native probe source is required when -IncludeNativeProbe is set: $nativeProbeSource"
  }
}

Test-Headroom 'before output creation'
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($directory in @('source','obj','compiler-temp','generated','assets','assets/fonts','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outPath $directory) | Out-Null
}
Write-NewJson (Join-Path $outPath 'protected-before.json') $protectedBefore 6

try {
  $snapshotFiles=@(
    'src/render/renderer2d.cpp',
    'src/render/contact_shadow.cpp',
    'src/render/sky_shadow.cpp',
    'src/client/game_art.cpp',
    'src/assets/asset_catalog.cpp',
    'src/foundation/foundation.cpp',
    'tools/material_bridge_probe.cpp',
    'tools/authored_sky_bridge_probe.cpp',
    'tools/export_material_master.py',
    'tests/material_master_export_tests.py',
    'art-source/engine-calibration/material-master.json',
    'tools/compile_skill_balance.py',
    'data/skills.json',
    'CMakeLists.txt',
    'tools/verify_sky_authored_bridge_isolated.ps1',
    'tools/build_sky_authored_isolated.ps1')
  if($IncludeNativeProbe){
    $snapshotFiles += 'tools/authored_sky_native_probe.cpp'
  }
  foreach($relative in $snapshotFiles){
    $source=Join-Path $repoRoot $relative
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing snapshot input: $source"}
    $destination=Join-Path (Join-Path $outPath 'source') $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
  }
  Copy-Item -LiteralPath (Join-Path $repoRoot 'include') -Destination (Join-Path $outPath 'source/include') -Recurse
  Copy-Item -LiteralPath (Join-Path $repoRoot 'shaders') -Destination (Join-Path $outPath 'source/shaders') -Recurse

  $sourceRoot=Join-Path $outPath 'source'
  $sourceRows=@(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($sourceRoot,$_.FullName)
    $origin=Join-Path $repoRoot $relative
    if(!(Test-Path -LiteralPath $origin -PathType Leaf)){throw "Snapshot has no authoritative origin: $relative"}
    $originHash=(Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash
    $snapshotHash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    if($originHash -ne $snapshotHash){throw "Snapshot hash mismatch: $relative"}
    [ordered]@{relative_path=$relative.Replace('\','/');repository_path=$origin;
      snapshot_path=$_.FullName;bytes=[int64]$_.Length;sha256=$snapshotHash}
  })
  Write-NewJson (Join-Path $outPath 'source-provenance.json') $sourceRows 7
  Test-Headroom 'after source snapshot'

  # Reuse only the already verified attempt-09 shader cohort. No shader compiler or
  # negative shader suite is launched by this helper.
  $shaderReuseRoot=Join-Path $repoRoot 'build/sky-shadow-2026-10-07-attempt09'
  $shaderOrigin=Join-Path $shaderReuseRoot 'assets/shaders'
  $priorBuildReportPath=Join-Path $shaderReuseRoot 'build-report.json'
  $positiveExitPath=Join-Path $shaderReuseRoot 'shader-contract-positive.log.exit.txt'
  $positiveLogPath=Join-Path $shaderReuseRoot 'shader-contract-positive.log'
  foreach($required in @($priorBuildReportPath,$positiveExitPath,$positiveLogPath)){
    if(!(Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing verified shader evidence: $required"}
  }
  if((Get-Content -Raw -LiteralPath $positiveExitPath).Trim() -ne '0'){
    throw 'Attempt-09 strict-positive shader contract did not record exit 0'
  }
  $priorBuildReport=Get-Content -Raw -LiteralPath $priorBuildReportPath | ConvertFrom-Json
  $priorArtifacts=@($priorBuildReport.shader_artifacts)
  if($priorArtifacts.Count -ne 68){throw "Attempt-09 report must anchor exactly 68 shader artifacts, got $($priorArtifacts.Count)"}
  $originShaderFiles=@(Get-ChildItem -LiteralPath $shaderOrigin -File)
  if($originShaderFiles.Count -ne 69){throw "Attempt-09 shader directory must contain exactly 68 artifacts plus manifest, got $($originShaderFiles.Count)"}
  $priorManifest=Join-Path $shaderOrigin 'manifest.json'
  if((Get-FileHash -LiteralPath $priorManifest -Algorithm SHA256).Hash -ne $priorBuildReport.shader_manifest.sha256){
    throw 'Attempt-09 manifest no longer matches its build report'
  }

  $shaderClosure=@()
  $currentShaderRoot=Join-Path $repoRoot 'shaders'
  $frozenShaderRoot=Join-Path $shaderReuseRoot 'source/shaders'
  $currentShaderFiles=@(Get-ChildItem -LiteralPath $currentShaderRoot -File -Recurse | Sort-Object FullName)
  $frozenShaderFiles=@(Get-ChildItem -LiteralPath $frozenShaderRoot -File -Recurse | Sort-Object FullName)
  if($currentShaderFiles.Count -ne $frozenShaderFiles.Count){
    throw 'Current shader-source inventory differs from the verified attempt-09 closure'
  }
  foreach($current in $currentShaderFiles){
    $relative=[IO.Path]::GetRelativePath($currentShaderRoot,$current.FullName)
    $frozen=Join-Path $frozenShaderRoot $relative
    if(!(Test-Path -LiteralPath $frozen -PathType Leaf)){throw "Shader source missing from attempt-09 closure: $relative"}
    $currentHash=(Get-FileHash -LiteralPath $current.FullName -Algorithm SHA256).Hash
    $frozenHash=(Get-FileHash -LiteralPath $frozen -Algorithm SHA256).Hash
    if($currentHash -ne $frozenHash){throw "Current shader source changed since attempt-09: $relative"}
    $shaderClosure += [ordered]@{relative_path=$relative.Replace('\','/');sha256=$currentHash}
  }

  $shaderReuseRows=@()
  foreach($row in ($priorArtifacts | Sort-Object file)){
    if([IO.Path]::GetFileName([string]$row.file) -ne [string]$row.file){throw 'Invalid attempt-09 shader artifact name'}
    $origin=Join-Path $shaderOrigin ([string]$row.file)
    if(!(Test-Path -LiteralPath $origin -PathType Leaf)){throw "Missing attempt-09 shader artifact: $($row.file)"}
    $originHash=(Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash
    if($originHash -ne [string]$row.sha256){throw "Attempt-09 shader artifact changed: $($row.file)"}
    $destination=Join-Path $outPath ('assets/shaders/'+[string]$row.file)
    Assert-NewPath $destination
    Copy-Item -LiteralPath $origin -Destination $destination
    $destinationHash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if($destinationHash -ne $originHash){throw "Copied shader hash mismatch: $($row.file)"}
    $shaderReuseRows += [ordered]@{file=[string]$row.file;origin=$origin;destination=$destination;
      bytes=[int64](Get-Item -LiteralPath $destination).Length;sha256=$destinationHash}
  }
  $manifestDestination=Join-Path $outPath 'assets/shaders/manifest.json'
  Assert-NewPath $manifestDestination
  Copy-Item -LiteralPath $priorManifest -Destination $manifestDestination
  $manifestHash=(Get-FileHash -LiteralPath $manifestDestination -Algorithm SHA256).Hash
  if($manifestHash -ne $priorBuildReport.shader_manifest.sha256){throw 'Copied shader manifest hash mismatch'}
  Test-Headroom 'after verified shader reuse'

  . 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:TEMP=Join-Path $outPath 'compiler-temp'
  $env:TMP=$env:TEMP
  $env:PYTHONDONTWRITEBYTECODE='1'

  $python='C:/Python314/python.exe'
  foreach($tool in @('cl.exe','link.exe')){
    if($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)){throw "Missing VS18 amd64 tool: $tool"}
  }
  if(!(Test-Path -LiteralPath $python -PathType Leaf)){throw "Missing pinned Python: $python"}
  $compilerRows=@(
    Get-HashRow (Get-Command cl.exe).Source
    Get-HashRow (Get-Command link.exe).Source
    Get-HashRow $python)

  Invoke-Checked $python @((Join-Path $sourceRoot 'tools/compile_skill_balance.py'),
    (Join-Path $sourceRoot 'data/skills.json'),
    (Join-Path $outPath 'generated/skill_balance_generated.h')) 'generate-skill-balance.log'

  $common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
    '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-',
    '/DRIBTERRA_PROBE_BUILD="sky-authored-isolated-unversioned"',
    ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $outPath 'generated')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
  $translationUnits=[ordered]@{
    renderer2d='src/render/renderer2d.cpp'
    contact_shadow='src/render/contact_shadow.cpp'
    sky_shadow='src/render/sky_shadow.cpp'
    game_art='src/client/game_art.cpp'
    asset_catalog='src/assets/asset_catalog.cpp'
    foundation='src/foundation/foundation.cpp'
    material_bridge_probe='tools/material_bridge_probe.cpp'
    authored_sky_bridge_probe='tools/authored_sky_bridge_probe.cpp'
  }
  if($IncludeNativeProbe){
    $translationUnits['authored_sky_native_probe']='tools/authored_sky_native_probe.cpp'
  }
  foreach($entry in $translationUnits.GetEnumerator()){
    Invoke-Checked 'cl.exe' ($common+@('/c',(Join-Path $sourceRoot $entry.Value),
      ('/Fo'+(Join-Path $outPath ('obj/'+$entry.Key+'.obj'))))) ('compile-'+$entry.Key+'.log')
  }

  $explicitLibraries=@(
    (Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),
    (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'),
    (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/lib/libcrypto.lib'),
    (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/lib/libpng16.lib'),
    (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/lib/zs.lib'))
  foreach($library in $explicitLibraries){
    if(!(Test-Path -LiteralPath $library -PathType Leaf)){throw "Missing pinned link library: $library"}
  }
  $systemLibraryNames=@('kernel32.lib','user32.lib','gdi32.lib','winspool.lib','shell32.lib',
    'ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib','ws2_32.lib',
    'crypt32.lib','iphlpapi.lib','bcrypt.lib')
  $systemLibraries=@($systemLibraryNames | ForEach-Object { Resolve-SystemLibrary $_ })
  $linkLibraries=@($explicitLibraries+$systemLibraries)
  $linkLibraryRows=@($linkLibraries | ForEach-Object { Get-HashRow $_ })
  $productionObjects=@('renderer2d','contact_shadow','sky_shadow','game_art','asset_catalog','foundation') |
    ForEach-Object { Join-Path $outPath ('obj/'+$_.ToString()+'.obj') }
  $linkBase=@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console')
  $probeLinks=@(
    @('MaterialBridgeProbe','material_bridge_probe'),
    @('AuthoredSkyBridgeProbe','authored_sky_bridge_probe'))
  if($IncludeNativeProbe){
    $probeLinks += ,@('AuthoredSkyNativeProbe','authored_sky_native_probe')
  }
  foreach($probe in $probeLinks){
    Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath ($probe[0]+'.exe'))))+
      $productionObjects+@((Join-Path $outPath ('obj/'+$probe[1]+'.obj')))+$linkLibraries) ('link-'+$probe[0]+'.log')
  }

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
    $originHash=(Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash
    $destinationHash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if($originHash -ne $destinationHash){throw "Runtime copy hash mismatch: $($runtime[1])"}
    $runtimeRows += [ordered]@{origin=$origin;destination=$destination;
      bytes=[int64](Get-Item -LiteralPath $destination).Length;sha256=$destinationHash}
  }

  # Export from the frozen cohort snapshot, not from mutable working-tree paths.
  Invoke-Checked $python @((Join-Path $sourceRoot 'tools/export_material_master.py'),
    (Join-Path $sourceRoot 'art-source/engine-calibration/material-master.json'),
    (Join-Path $outPath 'kit')) 'export-material-master.log'

  $sourceFinalRows=@($sourceRows | ForEach-Object {
    $originHash=(Get-FileHash -LiteralPath $_.repository_path -Algorithm SHA256).Hash
    $snapshotHash=(Get-FileHash -LiteralPath $_.snapshot_path -Algorithm SHA256).Hash
    $unchanged=($originHash -eq $_.sha256 -and $snapshotHash -eq $_.sha256)
    [ordered]@{relative_path=$_.relative_path;repository_path=$_.repository_path;
      snapshot_path=$_.snapshot_path;sha256=$_.sha256;unchanged=$unchanged}
  })
  if(@($sourceFinalRows | Where-Object {-not $_.unchanged}).Count){throw 'Source cohort changed during isolated build'}
  foreach($shader in $shaderClosure){
    $currentShader=Join-Path (Join-Path $repoRoot 'shaders') ([string]$shader.relative_path)
    if((Get-FileHash -LiteralPath $currentShader -Algorithm SHA256).Hash -ne $shader.sha256){
      throw "Shader source cohort changed during isolated build: $($shader.relative_path)"
    }
  }
  Write-NewJson (Join-Path $outPath 'source-final-hashes.json') $sourceFinalRows 7

  $executableNames=@('MaterialBridgeProbe.exe','AuthoredSkyBridgeProbe.exe')
  if($IncludeNativeProbe){$executableNames += 'AuthoredSkyNativeProbe.exe'}
  $executables=$executableNames | ForEach-Object {
    Get-HashRow (Join-Path $outPath $_)
  }
  $generatedRows=@(Get-HashRow (Join-Path $outPath 'generated/skill_balance_generated.h'))
  $kitRows=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'kit') -File -Recurse | Sort-Object FullName |
    ForEach-Object { Get-HashRow $_.FullName })
  $exitSidecars=@(Get-ChildItem -LiteralPath $outPath -File -Filter '*.log.exit.txt' | Sort-Object Name)
  $expectedExitSidecars=if($IncludeNativeProbe){14}else{12}
  if($exitSidecars.Count -ne $expectedExitSidecars){
    throw "Expected $expectedExitSidecars command exit sidecars, got $($exitSidecars.Count)"
  }
  foreach($sidecar in $exitSidecars){
    if((Get-Content -Raw -LiteralPath $sidecar.FullName).Trim() -ne '0'){
      throw "Nonzero command exit sidecar: $($sidecar.Name)"
    }
  }
  $commandEvidence=@(Get-ChildItem -LiteralPath $outPath -File |
    Where-Object {$_.Name -like '*.log' -or $_.Name -like '*.log.exit.txt'} |
    Sort-Object Name | ForEach-Object { Get-HashRow $_.FullName })
  $shaderManifestRow=[ordered]@{origin=$priorManifest;destination=$manifestDestination;
    bytes=[int64](Get-Item -LiteralPath $manifestDestination).Length;sha256=$manifestHash}
  $positiveEvidence=@(Get-HashRow $positiveLogPath; Get-HashRow $positiveExitPath;
    Get-HashRow $priorBuildReportPath)
  $executionFlags=[ordered]@{
    material_bridge_probe=$false
    authored_sky_bridge_probe=$false
    gpu=$false
  }
  if($IncludeNativeProbe){$executionFlags.authored_sky_native_probe=$false}
  $report=[ordered]@{
    classification='fresh bounded standalone authored-sky bridge build; executables not launched; no product/version acceptance'
    build_label='sky-authored-isolated-unversioned'
    version_claim=$false
    include_native_probe=$IncludeNativeProbe.IsPresent
    authoritative_repository=$repoRoot
    output_root=$outPath
    translation_units=@($translationUnits.Values)
    source_provenance=$sourceRows
    compiler=$compilerRows
    generated=$generatedRows
    link_libraries=$linkLibraryRows
    runtime_files=$runtimeRows
    executables=$executables
    exported_kit=$kitRows
    command_evidence=$commandEvidence
    shader_reuse=[ordered]@{
      classification='verified attempt-09 shader reuse; no fresh shader compilation or negative shader tests'
      origin_root=$shaderReuseRoot
      strict_positive_evidence=$positiveEvidence
      current_source_closure=$shaderClosure
      artifacts=$shaderReuseRows
      manifest=$shaderManifestRow
      artifact_count=$shaderReuseRows.Count
      total_file_count=($shaderReuseRows.Count+1)
      fresh_shader_compilation=$false
      fresh_shader_negative_tests=$false
    }
    native_or_contract_executions=$executionFlags
  }
  Write-NewJson (Join-Path $outPath 'build-report.json') $report 10
  Test-Headroom 'after build report'
} finally {
  $protectedAfter=@($protectedBefore | ForEach-Object {
    $hash=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
    [ordered]@{path=$_.path;before_sha256=$_.sha256;after_sha256=$hash;unchanged=($hash -eq $_.sha256)}
  })
  $afterPath=Join-Path $outPath 'protected-after.json'
  if(!(Test-Path -LiteralPath $afterPath)){Write-NewJson $afterPath $protectedAfter 6}
  if(@($protectedAfter | Where-Object {-not $_.unchanged}).Count){throw 'Protected prior evidence changed'}
}

Write-Output "Isolated authored-sky build complete without probe execution: $outPath"
