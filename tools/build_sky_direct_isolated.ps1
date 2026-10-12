param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outPath=[IO.Path]::GetFullPath($OutputRoot)
if((Split-Path $outPath) -ne [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))){throw 'Output must be one new child of repository build'}
if(Test-Path -LiteralPath $outPath){throw 'Refusing to reuse an existing output root'}
function Test-Headroom {
  $free=(Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below384MiB: $free"}
  if(Test-Path -LiteralPath $outPath){
    $used=(Get-ChildItem -LiteralPath $outPath -File -Recurse | Measure-Object Length -Sum).Sum
    if($used -gt 134217728){throw 'Isolated output exceeds128MiB'}
  }
}
function Invoke-Checked([string]$exe,[string[]]$arguments,[string]$log){
  Test-Headroom
  & $exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $outPath $log)
  $exitCode=$LASTEXITCODE
  [IO.File]::WriteAllText((Join-Path $outPath ($log+'.exit.txt')),([string]$exitCode))
  if($exitCode -ne 0){throw "$exe failed($exitCode), see $log"}
  Test-Headroom
}
Test-Headroom
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($dir in @('source','obj','compiler-temp','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outPath $dir) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'include') -Destination (Join-Path $outPath 'source/include') -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'shaders') -Destination (Join-Path $outPath 'source/shaders') -Recurse
foreach($file in @('vcpkg.json','src/render/renderer2d.cpp','src/render/contact_shadow.cpp',
    'tools/contact_shadow_probe.cpp','tools/sky_direct_probe.cpp','tests/contact_shadow_tests.cpp',
    'tests/moon_lighting_tests.cpp','tests/shader_contract.py','tests/shader_negative.py','tools/build_sky_direct_isolated.ps1')){
  Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination (Join-Path $outPath ('source/'+[IO.Path]::GetFileName($file)))
}
$protected=@('build/concept-rebuild-validation/LocalOcclusionProbe.exe',
  'build/concept-rebuild-validation/GroundForwardProbe.exe',
  'build/concept-rebuild-validation/compiled-shaders/manifest.json',
  'build/contact-targeted-2026-10-07-attempt03/ContactShadowProbe.exe',
  'build/contact-targeted-2026-10-07-attempt03/assets/shaders/manifest.json')
$before=@($protected | ForEach-Object {
  $path=Join-Path $repoRoot $_
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing protected input: $path"}
  [ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
})
[IO.File]::WriteAllText((Join-Path $outPath 'protected-before.json'),($before|ConvertTo-Json -Depth 5))
$sources=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'source') -File -Recurse | ForEach-Object {
  [ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
[IO.File]::WriteAllText((Join-Path $outPath 'source-hashes.json'),($sources|ConvertTo-Json -Depth 5))
# Fresh snapshot/direct toolchain only: no CMake staging, installers or cleanup.
try {
. 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:TEMP=Join-Path $outPath 'compiler-temp'
$env:TMP=$env:TEMP
$common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-',
  ('/I'+(Join-Path $outPath 'source/include')),('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
  ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
  ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
foreach($name in @('renderer2d','contact_shadow','sky_direct_probe','contact_shadow_probe','moon_lighting_tests','contact_shadow_tests')){
  Invoke-Checked 'cl.exe' ($common+@('/c',(Join-Path $outPath "source/$name.cpp"),('/Fo'+(Join-Path $outPath "obj/$name.obj")))) "compile-$name.log"
}
$linkBase=@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console')
$sdlLibs=@((Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),
  (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'),
  'kernel32.lib','user32.lib','gdi32.lib','winspool.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib')
foreach($probe in @(@('SkyDirectProbe','sky_direct_probe'),@('ContactShadowProbe','contact_shadow_probe'))){
  Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath ($probe[0]+'.exe'))),
    (Join-Path $outPath 'obj/renderer2d.obj'),(Join-Path $outPath 'obj/contact_shadow.obj'),
    (Join-Path $outPath ('obj/'+$probe[1]+'.obj')))+$sdlLibs) ('link-'+$probe[0]+'.log')
}
Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'MoonLightingTests.exe')),(Join-Path $outPath 'obj/moon_lighting_tests.obj'))) 'link-moon-tests.log'
Invoke-Checked 'link.exe' ($linkBase+@(('/out:'+(Join-Path $outPath 'ContactShadowTests.exe')),(Join-Path $outPath 'obj/contact_shadow.obj'),(Join-Path $outPath 'obj/contact_shadow_tests.obj'))) 'link-contact-tests.log'
foreach($dll in @('.deps/SDL3-3.4.16/lib/x64/SDL3.dll','.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll')){
  Copy-Item -LiteralPath (Join-Path $repoRoot $dll) -Destination $outPath
}
$shadercross=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe'
$dxc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe'
$programs=@('sprite','surface_lit','surface_hdr','world_linear','hdr_composite','material_sprite',
  'ground_gbuffer','ground_lighting','material_forward','ground_contact','ground_contact_gbuffer','ground_contact_lighting',
  'ground_sky_lighting','material_sky_forward')
foreach($program in $programs){foreach($stage in @('vert','frag')){foreach($kind in @('DXIL','JSON')){
  $stageName=if($stage -eq 'vert'){'vertex'}else{'fragment'}
  Invoke-Checked $shadercross @((Join-Path $outPath "source/shaders/$program.$stage.hlsl"),'-s','HLSL','-d',$kind,
    '-t',$stageName,'-e','main','-I',(Join-Path $outPath 'source/shaders'),'-o',
    (Join-Path $outPath ('assets/shaders/'+$program+'.'+$stage+'.'+$kind.ToLowerInvariant()))) "shader-$program-$stage-$kind.log"
}}}
Invoke-Checked 'C:/Python314/python.exe' @((Join-Path $outPath 'source/shader_contract.py'),
  (Join-Path $outPath 'source/shaders'),(Join-Path $outPath 'assets/shaders'),'--dxc',$dxc,'--shadercross',$shadercross,'--write-manifest') 'shader-contract.log'
Invoke-Checked (Join-Path $outPath 'MoonLightingTests.exe') @() 'moon-tests.log'
Invoke-Checked (Join-Path $outPath 'ContactShadowTests.exe') @() 'contact-tests.log'
$env:RIBTERRA_PROOF_ROOT=$outPath
Invoke-Checked 'C:/Python314/python.exe' @((Join-Path $outPath 'source/shader_negative.py'),
  (Join-Path $outPath 'source/shaders'),(Join-Path $outPath 'assets/shaders'),$dxc,$shadercross) 'shader-negative.log'
} finally {
$after=@($before | ForEach-Object {
  $hash=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
  [ordered]@{path=$_.path;sha256=$hash;unchanged=($hash -eq $_.sha256)}
})
[IO.File]::WriteAllText((Join-Path $outPath 'protected-after.json'),($after|ConvertTo-Json -Depth 5))
if(@($after|Where-Object {!$_.unchanged}).Count){throw 'Protected prior input changed'}
}
Write-Output "Isolated sky build complete: $outPath"
