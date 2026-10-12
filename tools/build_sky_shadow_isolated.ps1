param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
$outPath=[IO.Path]::GetFullPath($OutputRoot)
if((Split-Path $outPath) -ne [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))){
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
function Assert-NewPath([string]$path){
  if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing path: $path"}
}
function Invoke-Checked([string]$exe,[object[]]$arguments,[string]$log){
  Test-Headroom "before $log"
  $logPath=Join-Path $outPath $log
  Assert-NewPath $logPath
  $savedErrorActionPreference=$ErrorActionPreference
  try {
    # Native stderr is evidence, not a PowerShell exception; preserve its real exit code below.
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

$protected=@(
  'build/concept-rebuild-validation/LocalOcclusionProbe.exe',
  'build/concept-rebuild-validation/GroundForwardProbe.exe',
  'build/concept-rebuild-validation/compiled-shaders/manifest.json',
  'build/contact-targeted-2026-10-07-attempt03/ContactShadowProbe.exe',
  'build/contact-targeted-2026-10-07-attempt03/assets/shaders/manifest.json',
  'build/sky-direct-2026-10-07-attempt03/SkyDirectProbe.exe',
  'build/sky-direct-2026-10-07-attempt03/assets/shaders/manifest.json')
$before=@($protected | ForEach-Object {
  $path=Join-Path $repoRoot $_
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing protected input: $path"}
  [ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
})

Test-Headroom 'before output creation'
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($dir in @('source','source/include','source/shaders','obj','compiler-temp','assets','assets/fonts','assets/shaders')){
  if(!(Test-Path -LiteralPath (Join-Path $outPath $dir))){
    New-Item -ItemType Directory -Path (Join-Path $outPath $dir) | Out-Null
  }
}
[IO.File]::WriteAllText((Join-Path $outPath 'protected-before.json'),($before|ConvertTo-Json -Depth 5))

Copy-Item -Path (Join-Path $repoRoot 'include/*') -Destination (Join-Path $outPath 'source/include') -Recurse
Copy-Item -Path (Join-Path $repoRoot 'shaders/*') -Destination (Join-Path $outPath 'source/shaders') -Recurse
$snapshotFiles=@(
  'src/render/renderer2d.cpp','src/render/contact_shadow.cpp','src/render/sky_shadow.cpp',
  'tools/sky_shadow_probe.cpp','tools/sky_direct_probe.cpp','tools/contact_shadow_probe.cpp',
  'tests/sky_shadow_tests.cpp','tests/moon_lighting_tests.cpp','tests/contact_shadow_tests.cpp',
  'tests/render_frame_tests.cpp','tests/shader_contract.py','tests/shader_negative.py',
  'tools/build_sky_shadow_isolated.ps1','tools/verify_sky_shadow_isolated.ps1',
  'cmake/CompileShaders.cmake','CMakeLists.txt','vcpkg.json')
foreach($file in $snapshotFiles){
  $source=Join-Path $repoRoot $file
  if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing snapshot input: $source"}
  Copy-Item -LiteralPath $source -Destination (Join-Path $outPath ('source/'+[IO.Path]::GetFileName($file)))
}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'source') -File -Recurse | Sort-Object FullName | ForEach-Object {
  [ordered]@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
[IO.File]::WriteAllText((Join-Path $outPath 'source-hashes.json'),($sources|ConvertTo-Json -Depth 5))
Test-Headroom 'after source snapshot'

# Fresh snapshot/direct toolchain only: no CMake staging, installers, native probe launch or cleanup.
try {
  . 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  $env:TEMP=Join-Path $outPath 'compiler-temp'
  $env:TMP=$env:TEMP
  $common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-',
    ('/I'+(Join-Path $outPath 'source/include')),('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
  $translationUnits=@('renderer2d','contact_shadow','sky_shadow','sky_shadow_probe','sky_direct_probe',
    'contact_shadow_probe','sky_shadow_tests','moon_lighting_tests','contact_shadow_tests','render_frame_tests')
  foreach($name in $translationUnits){
    Invoke-Checked 'cl.exe' ($common+@('/c',(Join-Path $outPath "source/$name.cpp"),
      ('/Fo'+(Join-Path $outPath "obj/$name.obj")))) "compile-$name.log"
  }

  $linkBase=@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console')
  $sdlLibs=@((Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),
    (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'),
    'kernel32.lib','user32.lib','gdi32.lib','winspool.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib')
  $rendererObjects=@((Join-Path $outPath 'obj/renderer2d.obj'),
    (Join-Path $outPath 'obj/contact_shadow.obj'),(Join-Path $outPath 'obj/sky_shadow.obj'))
  foreach($probe in @(@('SkyShadowProbe','sky_shadow_probe'),@('SkyDirectProbe','sky_direct_probe'),
                       @('ContactShadowProbe','contact_shadow_probe'))){
    Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath ($probe[0]+'.exe'))))+
      $rendererObjects+@((Join-Path $outPath ('obj/'+$probe[1]+'.obj')))+$sdlLibs) ('link-'+$probe[0]+'.log')
  }
  Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'RenderFrameTests.exe')))+
    $rendererObjects+@((Join-Path $outPath 'obj/render_frame_tests.obj'))+$sdlLibs) 'link-render-frame-tests.log'
  Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'SkyShadowTests.exe')),
    (Join-Path $outPath 'obj/sky_shadow.obj'),(Join-Path $outPath 'obj/sky_shadow_tests.obj'))) 'link-sky-shadow-tests.log'
  Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'MoonLightingTests.exe')),
    (Join-Path $outPath 'obj/moon_lighting_tests.obj'))) 'link-moon-tests.log'
  Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'ContactShadowTests.exe')),
    (Join-Path $outPath 'obj/contact_shadow.obj'),(Join-Path $outPath 'obj/contact_shadow_tests.obj'))) 'link-contact-tests.log'

  foreach($runtime in @(
      @('.deps/SDL3-3.4.16/lib/x64/SDL3.dll','SDL3.dll'),
      @('.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll','SDL3_ttf.dll'),
      @('assets/fonts/NotoSans-Regular.ttf','assets/fonts/NotoSans-Regular.ttf'))){
    $source=Join-Path $repoRoot $runtime[0]
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing runtime input: $source"}
    Copy-Item -LiteralPath $source -Destination (Join-Path $outPath $runtime[1])
  }

  $shadercross=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe'
  $dxc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe'
  $programs=@('sprite','surface_lit','surface_hdr','world_linear','hdr_composite','material_sprite',
    'ground_gbuffer','ground_lighting','material_forward','ground_contact','ground_contact_gbuffer',
    'ground_contact_lighting','ground_sky_lighting','material_sky_forward',
    'ground_shadow_lighting','material_shadow_forward','sky_shadow_depth')
  foreach($program in $programs){foreach($stage in @('vert','frag')){foreach($kind in @('DXIL','JSON')){
    $stageName=if($stage -eq 'vert'){'vertex'}else{'fragment'}
    Invoke-Checked $shadercross @((Join-Path $outPath "source/shaders/$program.$stage.hlsl"),'-s','HLSL','-d',$kind,
      '-t',$stageName,'-e','main','-I',(Join-Path $outPath 'source/shaders'),'-o',
      (Join-Path $outPath ('assets/shaders/'+$program+'.'+$stage+'.'+$kind.ToLowerInvariant()))) "shader-$program-$stage-$kind.log"
  }}}
  Invoke-Checked 'C:/Python314/python.exe' @((Join-Path $outPath 'source/shader_contract.py'),
    (Join-Path $outPath 'source/shaders'),(Join-Path $outPath 'assets/shaders'),'--dxc',$dxc,
    '--shadercross',$shadercross,'--write-manifest') 'shader-contract-positive.log'

  Invoke-Checked (Join-Path $outPath 'SkyShadowTests.exe') @() 'sky-shadow-tests.log'
  Invoke-Checked (Join-Path $outPath 'MoonLightingTests.exe') @() 'moon-tests.log'
  Invoke-Checked (Join-Path $outPath 'ContactShadowTests.exe') @() 'contact-tests.log'
  $env:RIBTERRA_PROOF_ROOT=$outPath
  Invoke-Checked 'C:/Python314/python.exe' @((Join-Path $outPath 'source/shader_negative.py'),
    (Join-Path $outPath 'source/shaders'),(Join-Path $outPath 'assets/shaders'),$dxc,$shadercross) 'shader-negative.log'

  $executables=@('SkyShadowProbe.exe','SkyDirectProbe.exe','ContactShadowProbe.exe','RenderFrameTests.exe',
    'SkyShadowTests.exe','MoonLightingTests.exe','ContactShadowTests.exe') | ForEach-Object {
      $path=Join-Path $outPath $_
      [ordered]@{path=$path;bytes=(Get-Item -LiteralPath $path).Length;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
  $manifest=Join-Path $outPath 'assets/shaders/manifest.json'
  $shaderArtifacts=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'assets/shaders') -File |
    Where-Object {$_.Name -ne 'manifest.json' -and $_.Extension -in @('.dxil','.json')} |
    Sort-Object Name | ForEach-Object {
      [ordered]@{file=$_.Name;path=$_.FullName;bytes=$_.Length;
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
  if($shaderArtifacts.Count -ne 68){throw "Expected exactly 68 compiled DXIL/JSON shader artifacts, got $($shaderArtifacts.Count)"}
  $report=[ordered]@{
    classification='fresh bounded direct build and CPU/shader checks; native probes and RenderFrameTests not run'
    authoritative_repository=$repoRoot
    output_root=$outPath
    shader_programs=$programs
    executables=$executables
    shader_manifest=[ordered]@{path=$manifest;sha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash}
    shader_artifacts=$shaderArtifacts
  }
  [IO.File]::WriteAllText((Join-Path $outPath 'build-report.json'),($report|ConvertTo-Json -Depth 7))
  Test-Headroom 'after report'
} finally {
  $after=@($before | ForEach-Object {
    $hash=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
    [ordered]@{path=$_.path;sha256=$hash;unchanged=($hash -eq $_.sha256)}
  })
  [IO.File]::WriteAllText((Join-Path $outPath 'protected-after.json'),($after|ConvertTo-Json -Depth 5))
  if(@($after|Where-Object {-not $_.unchanged}).Count){throw 'Protected prior input changed'}
}
Write-Output "Isolated sky-shadow build complete without native probe execution: $outPath"
