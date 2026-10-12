param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outPath=[IO.Path]::GetFullPath($OutputRoot)
$expectedParent=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
if((Split-Path $outPath) -ne $expectedParent){throw 'Output must be one new child of the repository build directory'}
if(Test-Path -LiteralPath $outPath){throw 'Refusing to reuse any existing output root'}
function Test-Headroom {
  $free=(Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Compile/link headroom below 384 MiB: $free bytes"}
  if(Test-Path -LiteralPath $outPath){
    $used=(Get-ChildItem -LiteralPath $outPath -File -Recurse | Measure-Object Length -Sum).Sum
    if($used -gt 134217728){throw 'Isolated working output exceeds 128 MiB'}
  }
}
Test-Headroom
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($dir in @('source','obj','compiler-temp','assets/shaders')){
  New-Item -ItemType Directory -Path (Join-Path $outPath $dir) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'include') -Destination (Join-Path $outPath 'source/include') -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'shaders') -Destination (Join-Path $outPath 'source/shaders') -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'vcpkg.json') -Destination (Join-Path $outPath 'source/vcpkg.json')
foreach($file in @('src/render/renderer2d.cpp','src/render/contact_shadow.cpp','tools/contact_shadow_probe.cpp','tests/contact_shadow_tests.cpp','tests/shader_contract.py','tests/shader_negative.py')){
  Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination (Join-Path $outPath ('source/'+[IO.Path]::GetFileName($file)))
}
$oldFiles=@(
  'build/concept-rebuild-validation/LocalOcclusionProbe.exe',
  'build/concept-rebuild-validation/GroundForwardProbe.exe',
  'build/concept-rebuild-validation/compiled-shaders/manifest.json')
$oldHashes=@($oldFiles | ForEach-Object { $filePath=Join-Path $repoRoot $_; if(!(Test-Path -LiteralPath $filePath -PathType Leaf)){throw "Missing protected prior artifact: $filePath"}; [ordered]@{path=$filePath;sha256=(Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash} })
[IO.File]::WriteAllText((Join-Path $outPath 'prior-input-hashes.json'),($oldHashes | ConvertTo-Json -Depth 5))
$sourceHashes=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'source') -File -Recurse | ForEach-Object { [ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} })
[IO.File]::WriteAllText((Join-Path $outPath 'source-hashes.json'),($sourceHashes | ConvertTo-Json -Depth 5))
# Development-shell setup is read-only. No CMake, staging, installer or cleanup.
. 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:TEMP=Join-Path $outPath 'compiler-temp'
$env:TMP=$env:TEMP
$common=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-',('/I'+(Join-Path $outPath 'source/include')),
  ('/external:I'+(Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
  ('/external:I'+(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
  ('/external:I'+(Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),'/external:W0')
foreach($name in @('renderer2d','contact_shadow','contact_shadow_probe','contact_shadow_tests')){
  Test-Headroom
  $args=$common+@('/c',(Join-Path $outPath "source/$name.cpp"),('/Fo'+(Join-Path $outPath "obj/$name.obj")))
  & cl.exe @args 2>&1 | Tee-Object -FilePath (Join-Path $outPath "compile-$name.log")
  if($LASTEXITCODE -ne 0){throw "Compilation failed: $name exit $LASTEXITCODE"}
  Test-Headroom
}
Test-Headroom
$linkArgs=@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',('/out:'+(Join-Path $outPath 'ContactShadowProbe.exe')),
  (Join-Path $outPath 'obj/renderer2d.obj'),(Join-Path $outPath 'obj/contact_shadow.obj'),(Join-Path $outPath 'obj/contact_shadow_probe.obj'),
  (Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),(Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'),
  'kernel32.lib','user32.lib','gdi32.lib','winspool.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib')
& link.exe @linkArgs 2>&1 | Tee-Object -FilePath (Join-Path $outPath 'link-probe.log')
if($LASTEXITCODE -ne 0){throw 'Probe link failed'}
Test-Headroom
& link.exe /nologo /machine:x64 /incremental:no /debug:none /subsystem:console ('/out:'+(Join-Path $outPath 'ContactShadowTests.exe')) (Join-Path $outPath 'obj/contact_shadow.obj') (Join-Path $outPath 'obj/contact_shadow_tests.obj') 2>&1 | Tee-Object -FilePath (Join-Path $outPath 'link-tests.log')
if($LASTEXITCODE -ne 0){throw 'CPU test link failed'}
Test-Headroom
foreach($dll in @('.deps/SDL3-3.4.16/lib/x64/SDL3.dll','.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll')){
  Copy-Item -LiteralPath (Join-Path $repoRoot $dll) -Destination $outPath
}
$shadercross=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe'
$programs=@('sprite','surface_lit','surface_hdr','world_linear','hdr_composite','material_sprite','ground_gbuffer','ground_lighting','material_forward','ground_contact','ground_contact_gbuffer','ground_contact_lighting')
foreach($program in $programs){foreach($stage in @('vert','frag')){foreach($kind in @('DXIL','JSON')){
  $shaderInput=Join-Path $outPath "source/shaders/$program.$stage.hlsl"
  $suffix=$kind.ToLowerInvariant(); $stageName=if($stage -eq 'vert'){'vertex'}else{'fragment'}
  $shaderOutput=Join-Path $outPath "assets/shaders/$program.$stage.$suffix"
  Test-Headroom
  & $shadercross $shaderInput -s HLSL -d $kind -t $stageName -e main -I (Join-Path $outPath 'source/shaders') -o $shaderOutput 2>&1 | Tee-Object -FilePath (Join-Path $outPath "shader-$program-$stage-$suffix.log")
  if($LASTEXITCODE -ne 0){throw "Shader compilation failed: $program.$stage.$suffix"}
  Test-Headroom
}}}
Test-Headroom
& 'C:/Python314/python.exe' (Join-Path $outPath 'source/shader_contract.py') (Join-Path $outPath 'source/shaders') (Join-Path $outPath 'assets/shaders') --dxc (Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe') --shadercross $shadercross --write-manifest 2>&1 | Tee-Object -FilePath (Join-Path $outPath 'shader-contract.log')
if($LASTEXITCODE -ne 0){throw 'Shader contract failed'}
Test-Headroom
& (Join-Path $outPath 'ContactShadowTests.exe') 2>&1 | Tee-Object -FilePath (Join-Path $outPath 'cpu-tests.log')
if($LASTEXITCODE -ne 0){throw 'CPU tests failed'}
Test-Headroom
$env:RIBTERRA_PROOF_ROOT=$outPath
& 'C:/Python314/python.exe' (Join-Path $outPath 'source/shader_negative.py') (Join-Path $outPath 'source/shaders') (Join-Path $outPath 'assets/shaders') (Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe') $shadercross 2>&1 | Tee-Object -FilePath (Join-Path $outPath 'shader-negative.log')
if($LASTEXITCODE -ne 0){throw 'Shader negative contracts failed'}
Test-Headroom
$afterHashes=@($oldHashes | ForEach-Object {
  if(!(Test-Path -LiteralPath $_.path -PathType Leaf)){throw "Protected prior artifact disappeared: $($_.path)"}
  $actual=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
  [ordered]@{path=$_.path;sha256=$actual;unchanged=($actual -eq $_.sha256)}
})
[IO.File]::WriteAllText((Join-Path $outPath 'prior-input-hashes-after.json'),($afterHashes | ConvertTo-Json -Depth 5))
if(@($afterHashes | Where-Object {!$_.unchanged}).Count){throw 'A protected prior artifact changed'}
Write-Output "Isolated build complete: $outPath"
