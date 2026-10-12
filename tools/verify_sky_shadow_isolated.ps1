param([Parameter(Mandatory=$true)][string]$ProofRoot)
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskRoot=(Resolve-Path -LiteralPath $ProofRoot).Path
if((Split-Path $taskRoot) -ne (Join-Path $repoRoot 'build')){throw 'Expected an immediate repository build child'}
if(!(Test-Path -LiteralPath (Join-Path $taskRoot 'build-report.json'))){throw 'Fresh build did not complete'}
foreach($taskName in @('source-final-hashes.json','authoritative-source-lineage.json','protected-final.json','sky-shadow-verification-ledger.json')){
  if(Test-Path -LiteralPath (Join-Path $taskRoot $taskName)){throw "Refusing reused final output: $taskName"}
}
$built=Get-Content -LiteralPath (Join-Path $taskRoot 'build-report.json') -Raw|ConvertFrom-Json
$shaderInputHashes=@($built.shader_artifacts)
if($shaderInputHashes.Count -ne 68){throw 'Build report must anchor exactly68 shader artifacts'}
$shaderNames=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'assets/shaders') -File |
  Where-Object {$_.Name -ne 'manifest.json'} | Select-Object -ExpandProperty Name)
if(@(Compare-Object ($shaderNames|Sort-Object) ($shaderInputHashes.file|Sort-Object)).Count){throw 'Shader inventory differs from build-completion record'}
$snapshot=@(Get-Content -LiteralPath (Join-Path $taskRoot 'source-hashes.json') -Raw | ConvertFrom-Json)
$flatSources=@{}
foreach($taskFile in @(
  'src/render/renderer2d.cpp','src/render/contact_shadow.cpp','src/render/sky_shadow.cpp',
  'tools/sky_shadow_probe.cpp','tools/sky_direct_probe.cpp','tools/contact_shadow_probe.cpp',
  'tests/sky_shadow_tests.cpp','tests/moon_lighting_tests.cpp','tests/contact_shadow_tests.cpp',
  'tests/render_frame_tests.cpp','tests/shader_contract.py','tests/shader_negative.py',
  'tools/build_sky_shadow_isolated.ps1','tools/verify_sky_shadow_isolated.ps1',
  'cmake/CompileShaders.cmake','CMakeLists.txt','vcpkg.json')){
  $flatSources[[IO.Path]::GetFileName($taskFile)]=$taskFile
}
$sourcePrefix=(Join-Path $taskRoot 'source')+[IO.Path]::DirectorySeparatorChar
$sourceLineage=@($snapshot|ForEach-Object {
  if(!$_.path.StartsWith($sourcePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Snapshot input outside source root'}
  $relative=$_.path.Substring($sourcePrefix.Length).Replace('\','/')
  if($relative.StartsWith('include/') -or $relative.StartsWith('shaders/')){$origin=Join-Path $repoRoot $relative}
  elseif($flatSources.ContainsKey($relative)){$origin=Join-Path $repoRoot $flatSources[$relative]}
  else{throw "Unmapped source snapshot: $relative"}
  [ordered]@{source_path=$origin;snapshot_path=$_.path;sha256=$_.sha256}
})
function Check-CurrentInputs {
  foreach($taskInput in @($built.executables)+@($built.shader_manifest)+$shaderInputHashes){
    if((Get-FileHash -LiteralPath $taskInput.path -Algorithm SHA256).Hash -ne $taskInput.sha256){throw "Current build input changed: $($taskInput.path)"}
  }
  foreach($taskSource in $sourceLineage){
    if((Get-FileHash -LiteralPath $taskSource.source_path -Algorithm SHA256).Hash -ne $taskSource.sha256){throw "Authoritative source changed: $($taskSource.source_path)"}
    if((Get-FileHash -LiteralPath $taskSource.snapshot_path -Algorithm SHA256).Hash -ne $taskSource.sha256){throw "Frozen build source changed: $($taskSource.snapshot_path)"}
  }
}
Check-CurrentInputs
function Check-Budget {
  if((Get-Volume -DriveLetter C).SizeRemaining -lt 402653184){throw 'Headroom below384MiB'}
  if((Get-ChildItem -LiteralPath $taskRoot -File -Recurse | Measure-Object Length -Sum).Sum -gt 134217728){throw 'Proof exceeds128MiB'}
}
function Run-Probe([string]$exe,[string[]]$arguments,[string]$log){
  Check-Budget
  $logPath=Join-Path $taskRoot $log
  if((Test-Path -LiteralPath $logPath) -or (Test-Path -LiteralPath ($logPath+'.exit.txt'))){throw 'Refusing reused log/exit'}
  $savedPreference=$ErrorActionPreference
  try {
    $ErrorActionPreference='Continue'
    & (Join-Path $taskRoot $exe) @arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
    $taskExit=[int]$LASTEXITCODE
  } finally { $ErrorActionPreference=$savedPreference }
  [IO.File]::WriteAllText($logPath+'.exit.txt',[string]$taskExit)
  if($taskExit -ne 0){throw "$exe failed($taskExit), retained $log"}
  Check-Budget
}
function Compare-Files([string]$first,[string]$second,[string[]]$names,[string]$output){
  $pattern=if($output -eq 'prior-shader-comparison.json'){'*'}else{'*.png'}
  $secondNames=@(Get-ChildItem -LiteralPath $second -Filter $pattern -File | Where-Object {$_.Name -ne 'manifest.json'} | Select-Object -ExpandProperty Name)
  if($output -eq 'prior-shader-comparison.json'){
    if($secondNames.Count -ne 68){throw 'Expected exactly68 current shader artifacts'}
    $secondNames=@($secondNames|Where-Object {$_ -notmatch '^(ground_shadow_lighting|material_shadow_forward|sky_shadow_depth)\.(vert|frag)\.(dxil|json)$'})
  }
  if(@(Compare-Object ($names|Sort-Object) ($secondNames|Sort-Object)).Count){throw "File inventory differs: $output"}
  $rows=@($names | ForEach-Object {
    $a=Join-Path $first $_; $b=Join-Path $second $_
    $hashA=(Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash
    $hashB=(Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
    [ordered]@{file=$_;first_sha256=$hashA;second_sha256=$hashB;equal=($hashA -eq $hashB)}
  })
  $path=Join-Path $taskRoot $output
  if(Test-Path -LiteralPath $path){throw 'Refusing reused comparison'}
  [IO.File]::WriteAllText($path,($rows|ConvertTo-Json -Depth 5))
  if(@($rows|Where-Object {!$_.equal}).Count){throw "Byte comparison failed: $output"}
  return $rows.Count
}
$assets=Join-Path $taskRoot 'assets'
$oldSky=Join-Path $repoRoot 'build/sky-direct-2026-10-07-attempt03'
$oldContact=Join-Path $repoRoot 'build/contact-targeted-2026-10-07-attempt03'
$oldPngHashes=@(foreach($taskOld in @((Join-Path $oldSky 'native-01'),(Join-Path $oldContact 'native-01'))){
  Get-ChildItem -LiteralPath $taskOld -Filter '*.png' -File | ForEach-Object {
    [ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
  }
})
foreach($taskRun in @('native-01','native-02','sky-regression-01','contact-regression-01')){
  if(Test-Path -LiteralPath (Join-Path $taskRoot $taskRun)){throw "Existing output: $taskRun"}
}
Run-Probe 'SkyShadowProbe.exe' @($assets,(Join-Path $taskRoot 'native-01')) 'native-01.log'
Run-Probe 'SkyShadowProbe.exe' @($assets,(Join-Path $taskRoot 'native-02')) 'native-02.log'
Run-Probe 'SkyDirectProbe.exe' @($assets,(Join-Path $taskRoot 'sky-regression-01')) 'sky-regression-01.log'
Run-Probe 'ContactShadowProbe.exe' @($assets,(Join-Path $taskRoot 'contact-regression-01')) 'contact-regression-01.log'
$env:SDL_VIDEODRIVER='dummy';$env:SDL_RENDER_DRIVER='software'
Run-Probe 'RenderFrameTests.exe' @() 'render-frame-software.log'
$env:SDL_VIDEODRIVER=$null;$env:SDL_RENDER_DRIVER=$null
Run-Probe 'RenderFrameTests.exe' @('--gpu') 'render-frame-gpu.log'
foreach($taskReportName in @('native-01','native-02','sky-regression-01')){
  $reportFile=if($taskReportName -like 'native*'){'sky-shadow-report.json'}else{'sky-direct-report.json'}
  $taskReport=Get-Content -LiteralPath (Join-Path $taskRoot "$taskReportName/$reportFile") -Raw|ConvertFrom-Json
  $expectedPng=if($taskReportName -like 'native*'){27}else{26}
  if(!$taskReport.complete -or $taskReport.png_count -ne $expectedPng -or @($taskReport.checks|Where-Object {!$_.pass}).Count){throw "Incomplete native report: $taskReportName"}
  if($taskReportName -like 'native*'){
    $expectedChecks=@('pre-command-negative-contracts','target-candidate-failure-atomic-retry',
      'zero-caster-old-graph-compatibility','two-slot-sun-ground-material-causality',
      'independent-source-masks-and-opposite-directions','shared-mask-both-sources',
      'two-sided-and-moved-caster','same-caster-direction-relocates-shadow',
      'nearest-depth-overlap-causality','tilted-slope-ground-causality',
      'disabled-direct-preserves-contact-local-emission-ui','raw-and-reveal-filtered-capacity-boundary')
    if(@($taskReport.checks).Count -ne 12 -or @($taskReport.captures).Count -ne 27 -or
      @(Compare-Object ($expectedChecks|Sort-Object) ($taskReport.checks.name|Sort-Object)).Count){throw 'Sky check/capture inventory differs from frozen oracle'}
  }
  $device=if($taskReportName -like 'native*'){$taskReport.production.device}else{$taskReport.device}
  # SkyDirect report's device is under production in some retained schemas.
  if(!$device){$device=$taskReport.production.device}
  if($device.backend -ne 'direct3d12' -or $device.software){throw "Native GPU device contract failed: $taskReportName"}
}
$contactReport=Get-ChildItem -LiteralPath (Join-Path $taskRoot 'contact-regression-01') -Filter '*contact-shadow-report*.json' -File
if(@($contactReport).Count -ne 1){throw 'Expected one complete contact report'}
$contact=Get-Content -LiteralPath $contactReport.FullName -Raw|ConvertFrom-Json
if(!$contact.complete -or $contact.check_count -ne 53 -or @($contact.checks).Count -ne 53 -or @($contact.checks|Where-Object {!$_.pass}).Count -or $contact.device.backend -ne 'direct3d12' -or $contact.device.software){throw 'Incomplete contact native report'}
$repeatNames=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'native-01') -Filter '*.png' -File | Select-Object -ExpandProperty Name)
$repeat=Compare-Files (Join-Path $taskRoot 'native-01') (Join-Path $taskRoot 'native-02') $repeatNames 'native-repeat-comparison.json'
$skyNames=@(Get-ChildItem -LiteralPath (Join-Path $oldSky 'native-01') -Filter '*.png' -File | Select-Object -ExpandProperty Name)
$skyEqual=Compare-Files (Join-Path $oldSky 'native-01') (Join-Path $taskRoot 'sky-regression-01') $skyNames 'prior-sky-png-comparison.json'
$contactNames=@(Get-ChildItem -LiteralPath (Join-Path $oldContact 'native-01') -Filter '*.png' -File | Select-Object -ExpandProperty Name)
$contactEqual=Compare-Files (Join-Path $oldContact 'native-01') (Join-Path $taskRoot 'contact-regression-01') $contactNames 'prior-contact-png-comparison.json'
$shaderNames=@(Get-ChildItem -LiteralPath (Join-Path $oldSky 'assets/shaders') -File | Where-Object {$_.Name -ne 'manifest.json'} | Select-Object -ExpandProperty Name)
$shaderEqual=Compare-Files (Join-Path $oldSky 'assets/shaders') (Join-Path $taskRoot 'assets/shaders') $shaderNames 'prior-shader-comparison.json'
if($repeat -ne 27 -or $skyEqual -ne 26 -or $contactEqual -ne 31 -or $shaderEqual -ne 56){throw 'Evidence count contract failed'}
Check-CurrentInputs
foreach($taskOldPng in $oldPngHashes){if((Get-FileHash -LiteralPath $taskOldPng.path -Algorithm SHA256).Hash -ne $taskOldPng.sha256){throw 'Prior PNG changed'}}
$sources=@($snapshot|ForEach-Object {
  [ordered]@{path=$_.path;sha256=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash;unchanged=((Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash -eq $_.sha256)}
})
[IO.File]::WriteAllText((Join-Path $taskRoot 'source-final-hashes.json'),($sources|ConvertTo-Json -Depth 5))
if(@($sources|Where-Object {!$_.unchanged}).Count){throw 'Build source snapshot changed'}
[IO.File]::WriteAllText((Join-Path $taskRoot 'authoritative-source-lineage.json'),($sourceLineage|ConvertTo-Json -Depth 5))
$protected=@(Get-Content -LiteralPath (Join-Path $taskRoot 'protected-before.json') -Raw|ConvertFrom-Json)
$protectedAfter=@($protected|ForEach-Object {
  $hash=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
  [ordered]@{path=$_.path;sha256=$hash;unchanged=($hash -eq $_.sha256)}
})
[IO.File]::WriteAllText((Join-Path $taskRoot 'protected-final.json'),($protectedAfter|ConvertTo-Json -Depth 5))
if(@($protectedAfter|Where-Object {!$_.unchanged}).Count){throw 'Protected prior file changed'}
$ledger=[ordered]@{classification='bounded fixed-camera D32 shadow prerequisite only';native_repeat_png=$repeat;prior_sky_png=$skyEqual;prior_contact_png=$contactEqual;prior_shader_files=$shaderEqual;snapshot_files=$sources.Count;protected_files=$protectedAfter.Count;render_frame_software_exit=0;render_frame_gpu_exit=0;full_suite='NOT_RUN';concept_art='OPEN';game_view='BakedCompat';dynamic='unavailable';fps_120='NOT_MEASURED';source='fresh isolated /W4 /WX';logical_proof_bytes=(Get-ChildItem -LiteralPath $taskRoot -File -Recurse|Measure-Object Length -Sum).Sum;free_bytes=(Get-Volume -DriveLetter C).SizeRemaining}
[IO.File]::WriteAllText((Join-Path $taskRoot 'sky-shadow-verification-ledger.json'),($ledger|ConvertTo-Json -Depth 5))
Check-Budget
Write-Output ($ledger|ConvertTo-Json -Depth 5)
