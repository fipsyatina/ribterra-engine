param(
  [Parameter(Mandatory=$true)][string]$ProofRoot,
  [Parameter(Mandatory=$true)][string]$ExportOutputRoot,
  [switch]$IncludeNativeProbe)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){
  throw 'PowerShell 7 or newer is required; invoke this verifier with pwsh.'
}

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$expectedRepoRoot='C:\Users\fipst\Desktop\ribterra'
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,$expectedRepoRoot)){
  throw "Repository identity mismatch: expected $expectedRepoRoot, got $repoRoot"
}
$taskRoot=(Resolve-Path -LiteralPath $ProofRoot).Path
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $taskRoot),$buildRoot)){
  throw 'ProofRoot must be one immediate child of the authoritative repository build directory'
}
$exportRoot=[IO.Path]::GetFullPath($ExportOutputRoot)
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $exportRoot),$buildRoot)){
  throw 'ExportOutputRoot must be one new immediate child of the authoritative repository build directory'
}
if([StringComparer]::OrdinalIgnoreCase.Equals($exportRoot,$taskRoot)){
  throw 'ExportOutputRoot must be separate from ProofRoot'
}
if(Test-Path -LiteralPath $exportRoot){throw 'ExportOutputRoot must not already exist'}

function Assert-NewPath([string]$path) {
  if(Test-Path -LiteralPath $path){throw "Refusing to overwrite retained evidence: $path"}
}

function Write-NewJson([string]$path,[object]$value,[int]$depth=10) {
  Assert-NewPath $path
  [IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth $depth))
}

function Assert-Hash([string]$path,[string]$expected,[object]$expectedBytes=$null) {
  if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing anchored file: $path"}
  $item=Get-Item -LiteralPath $path
  if($null -ne $expectedBytes -and [int64]$item.Length -ne [int64]$expectedBytes){
    throw "Anchored byte count changed: $path"
  }
  $actual=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
  if(-not [StringComparer]::OrdinalIgnoreCase.Equals($actual,[string]$expected)){
    throw "Anchored hash changed: $path"
  }
}

function Assert-HashRow([object]$row) {
  Assert-Hash ([string]$row.path) ([string]$row.sha256) $row.bytes
}

function Check-Budget([string]$phase) {
  $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
  if($free -lt 402653184){throw "Headroom below 384MiB during ${phase}: $free"}
  foreach($boundedRoot in @($taskRoot,$exportRoot)){
    if(!(Test-Path -LiteralPath $boundedRoot)){continue}
    $measurement=Get-ChildItem -LiteralPath $boundedRoot -File -Recurse | Measure-Object Length -Sum
    $logical=if($null -eq $measurement.Sum){[int64]0}else{[int64]$measurement.Sum}
    if($logical -gt 134217728){
      throw "Bounded root exceeds 128MiB during ${phase}: $boundedRoot ($logical bytes)"
    }
  }
}

Check-Budget 'initial verifier preflight'

$buildReportPath=Join-Path $taskRoot 'build-report.json'
if(!(Test-Path -LiteralPath $buildReportPath -PathType Leaf)){throw 'Fresh isolated build did not complete'}
$buildReportHash=(Get-FileHash -LiteralPath $buildReportPath -Algorithm SHA256).Hash
$built=Get-Content -Raw -LiteralPath $buildReportPath | ConvertFrom-Json
if($built.build_label -ne 'sky-authored-isolated-unversioned' -or $built.version_claim -ne $false){
  throw 'Build report made an unexpected version/product claim'
}

$verifierPath=(Resolve-Path -LiteralPath $PSCommandPath).Path
$verifierHash=(Get-FileHash -LiteralPath $verifierPath -Algorithm SHA256).Hash
$protectedBeforePath=Join-Path $taskRoot 'protected-before.json'
$protectedBefore=@(Get-Content -Raw -LiteralPath $protectedBeforePath | ConvertFrom-Json)
if($protectedBefore.Count -ne 12){throw "Expected 12 protected prior evidence files, got $($protectedBefore.Count)"}

$finalPaths=@(
  'material-native-01','bridge-native-01','bridge-native-02',
  'export-tests.log','export-tests.log.exit.txt',
  'material-software.log','material-software.log.exit.txt',
  'material-native-01.log','material-native-01.log.exit.txt',
  'bridge-native-01.log','bridge-native-01.log.exit.txt',
  'bridge-native-02.log','bridge-native-02.log.exit.txt',
  'authored-bridge-source-final.json','authored-bridge-protected-final.json',
  'authored-bridge-verification-ledger.json')
foreach($relative in $finalPaths){Assert-NewPath (Join-Path $taskRoot $relative)}

function Check-Anchors {
  Assert-Hash $buildReportPath $buildReportHash
  Assert-Hash $verifierPath $verifierHash

  $executables=@($built.executables)
  $expectedExecutables=@('AuthoredSkyBridgeProbe.exe','MaterialBridgeProbe.exe')
  if($IncludeNativeProbe){$expectedExecutables += 'AuthoredSkyNativeProbe.exe'}
  $actualExecutables=@($executables | ForEach-Object {[IO.Path]::GetFileName([string]$_.path)})
  $executableDifference=@(Compare-Object -ReferenceObject $expectedExecutables -DifferenceObject $actualExecutables)
  if($executables.Count -ne $expectedExecutables.Count -or $executableDifference.Count){
    throw 'Build executable inventory differs from the selected focused probe cohort'
  }
  if($built.include_native_probe -ne $IncludeNativeProbe.IsPresent){
    throw 'Build report native-probe selection differs from the verifier switch'
  }
  $expectedTranslationUnits=@(
    'src/render/renderer2d.cpp','src/render/contact_shadow.cpp','src/render/sky_shadow.cpp',
    'src/client/game_art.cpp','src/assets/asset_catalog.cpp','src/foundation/foundation.cpp',
    'tools/material_bridge_probe.cpp','tools/authored_sky_bridge_probe.cpp')
  if($IncludeNativeProbe){$expectedTranslationUnits += 'tools/authored_sky_native_probe.cpp'}
  $actualTranslationUnits=@($built.translation_units)
  $translationUnitDifference=@(Compare-Object -ReferenceObject $expectedTranslationUnits -DifferenceObject $actualTranslationUnits)
  if($actualTranslationUnits.Count -ne $expectedTranslationUnits.Count -or $translationUnitDifference.Count){
    throw 'Build translation-unit inventory differs from the selected focused probe cohort'
  }
  $nativeSourceRows=@($built.source_provenance | Where-Object {$_.relative_path -eq 'tools/authored_sky_native_probe.cpp'})
  $expectedNativeSourceRows=if($IncludeNativeProbe){1}else{0}
  if($nativeSourceRows.Count -ne $expectedNativeSourceRows){
    throw 'Frozen native-probe source provenance differs from the verifier switch'
  }
  foreach($row in $executables){Assert-HashRow $row}
  foreach($row in @($built.compiler)){Assert-HashRow $row}
  foreach($row in @($built.generated)){Assert-HashRow $row}
  foreach($row in @($built.link_libraries)){Assert-HashRow $row}
  $commandEvidence=@($built.command_evidence)
  $expectedSidecars=if($IncludeNativeProbe){14}else{12}
  $expectedCommandEvidence=$expectedSidecars*2
  $exitEvidence=@($commandEvidence | Where-Object {[IO.Path]::GetFileName([string]$_.path) -like '*.log.exit.txt'})
  if($commandEvidence.Count -ne $expectedCommandEvidence -or $exitEvidence.Count -ne $expectedSidecars){
    throw 'Build command evidence count differs from the selected focused probe cohort'
  }
  foreach($row in $commandEvidence){Assert-HashRow $row}
  foreach($row in $exitEvidence){
    if((Get-Content -Raw -LiteralPath $row.path).Trim() -ne '0'){
      throw "Nonzero anchored build command exit: $($row.path)"
    }
  }
  $executionFlags=$built.native_or_contract_executions
  if($executionFlags.material_bridge_probe -ne $false -or
      $executionFlags.authored_sky_bridge_probe -ne $false -or
      $executionFlags.gpu -ne $false -or
      ($IncludeNativeProbe -and $executionFlags.authored_sky_native_probe -ne $false)){
    throw 'Build report claims an unexpected probe or GPU execution'
  }
  if(@($built.exported_kit).Count -ne 10){throw 'Build report must anchor exactly 10 exported-kit files'}
  foreach($row in @($built.exported_kit)){Assert-HashRow $row}

  foreach($row in @($built.runtime_files)){
    Assert-Hash ([string]$row.origin) ([string]$row.sha256)
    Assert-Hash ([string]$row.destination) ([string]$row.sha256) $row.bytes
  }
  $shaderReuse=$built.shader_reuse
  if($shaderReuse.artifact_count -ne 68 -or $shaderReuse.total_file_count -ne 69 -or
      $shaderReuse.fresh_shader_compilation -ne $false -or
      $shaderReuse.fresh_shader_negative_tests -ne $false){
    throw 'Shader reuse classification/count differs from the verified attempt-09 cohort'
  }
  foreach($row in @($shaderReuse.strict_positive_evidence)){Assert-HashRow $row}
  foreach($row in @($shaderReuse.artifacts)){
    Assert-Hash ([string]$row.origin) ([string]$row.sha256) $row.bytes
    Assert-Hash ([string]$row.destination) ([string]$row.sha256) $row.bytes
  }
  Assert-Hash ([string]$shaderReuse.manifest.origin) ([string]$shaderReuse.manifest.sha256)
  Assert-Hash ([string]$shaderReuse.manifest.destination) ([string]$shaderReuse.manifest.sha256) $shaderReuse.manifest.bytes
  foreach($row in @($shaderReuse.current_source_closure)){
    Assert-Hash (Join-Path (Join-Path $repoRoot 'shaders') ([string]$row.relative_path)) ([string]$row.sha256)
  }

  foreach($row in @($built.source_provenance)){
    Assert-Hash ([string]$row.repository_path) ([string]$row.sha256) $row.bytes
    Assert-Hash ([string]$row.snapshot_path) ([string]$row.sha256) $row.bytes
  }
  foreach($row in $protectedBefore){Assert-Hash ([string]$row.path) ([string]$row.sha256) $row.bytes}
}

function Invoke-Checked([string]$executable,[object[]]$arguments,[string]$logName) {
  Check-Budget "before $logName"
  $logPath=Join-Path $taskRoot $logName
  $exitPath=$logPath+'.exit.txt'
  Assert-NewPath $logPath
  Assert-NewPath $exitPath
  $savedPreference=$ErrorActionPreference
  $exitCode=-1
  try {
    $ErrorActionPreference='Continue'
    & $executable @arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
    $exitCode=[int]$LASTEXITCODE
  } finally {
    $ErrorActionPreference=$savedPreference
  }
  if(!(Test-Path -LiteralPath $logPath)){[IO.File]::WriteAllText($logPath,'')}
  [IO.File]::WriteAllText($exitPath,"$exitCode`n")
  if($exitCode -ne 0){throw "$executable failed($exitCode); retained $logName"}
  Check-Budget "after $logName"
}

function Find-Source([string]$relative) {
  $rows=@($built.source_provenance | Where-Object {$_.relative_path -eq $relative})
  if($rows.Count -ne 1){throw "Expected one frozen source row for $relative"}
  return $rows[0]
}

function Get-TreeLedger([string]$root) {
  return @(Get-ChildItem -LiteralPath $root -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{relative=[IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/');
      bytes=[int64]$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
  })
}

function Json-Text([object]$value) {
  return ($value | ConvertTo-Json -Depth 100 -Compress)
}

$expectedBridgeChecks=@(
  'exported-six-triangle-descriptor',
  'derived-fixtures-created-write-once',
  'native-d3d12-construction',
  'optional-field-omitted-is-empty',
  'strict-invalid-descriptors-reject-before-upload',
  'upload-attempt-sentinel-proves-invalids-preupload',
  'opaque-valid-utf8-id-is-not-display-text',
  'utf8-id-256-byte-boundary-accepted',
  'canonical-six-triangle-transform-exact',
  'camera-translation-is-explicit-and-registry-immutable',
  'reveal-filter-contract',
  'adapter-failures-are-transactional',
  'frame-clear-removes-authored-casters')
$expectedInvalids=@(
  'field-not-array','unknown-object-field','missing-object-field','empty-id',
  'id-over-256-bytes','duplicate-id','wrong-triangle-cardinality',
  'wrong-vertex-cardinality','coordinate-not-number','coordinate-not-canonical-float32',
  'coordinate-double-out-of-range','coordinate-out-of-range','degenerate-triangle',
  'mask-is-float','mask-is-boolean','mask-out-of-range','raw-capacity-overflow',
  'sun-capacity-overflow','moon-capacity-overflow')
$expectedAdapterFailures=@(
  'existing plus staged raw capacity','existing plus staged sun capacity',
  'existing plus staged moon capacity','malformed existing source mask',
  'malformed existing bounds','malformed existing reveal',
  'malformed existing degenerate triangle','transformed out of fixed camera cube',
  'transformed nonfinite overflow','transformed float collapse',
  'nonfinite runtime anchor','nonpositive runtime scale','runtime reveal above one')
$expectedMaterialPng=@(
  'material-srgb-gray128.png','material-albedo-edge-no-bleed.png',
  'material-srgb-black-white-midpoint.png','material-srgb-gray-white-midpoint.png',
  'material-linear-ao128.png','material-normal-ao-edge-no-bleed.png',
  'material-linear-emission-mask128.png','material-emission-edge-no-bleed.png',
  'material-straight-alpha128.png','material-emission-off.png','material-emission-on.png',
  'material-normal-plus-x-lit.png','material-normal-minus-x-dark.png',
  'material-normal-plus-y-lit.png','material-normal-minus-y-dark.png',
  'material-depth-near-light.png','material-depth-far-light.png','material-world-ui-order.png',
  'game-art-schema2-lit-sprite.png','fault-candidate-1-old-catalog.png',
  'fault-candidate-3-old-catalog.png','fault-candidate-6-old-catalog.png')

Check-Anchors
Check-Budget 'before verification'
$protectedFinalPath=Join-Path $taskRoot 'authored-bridge-protected-final.json'
$verificationSucceeded=$false
try {
  $python='C:/Python314/python.exe'
  $pythonRows=@($built.compiler | Where-Object {[IO.Path]::GetFileName([string]$_.path) -eq 'python.exe'})
  if($pythonRows.Count -ne 1){throw 'Build report does not anchor the pinned Python interpreter'}
  Assert-HashRow $pythonRows[0]

  $exportTestSource=(Find-Source 'tests/material_master_export_tests.py').snapshot_path
  Invoke-Checked $python @($exportTestSource,'--output',$exportRoot) 'export-tests.log'
  $exportReport=Get-Content -Raw -LiteralPath (Join-Path $exportRoot 'test-report.json') | ConvertFrom-Json
  if($exportReport.result -ne 'pass' -or $exportReport.deterministic_file_count -ne 10 -or
      $exportReport.probes -ne 12 -or @($exportReport.negative_cases).Count -ne 41 -or
      @($exportReport.diagnostic_results).Count -ne 41 -or
      @($exportReport.diagnostic_results | Where-Object {$_.status -ne 2}).Count){
    throw 'Focused exporter report differs from the 10-file/12-probe/41-negative oracle'
  }
  foreach($binding in @(
      @('master','art-source/engine-calibration/material-master.json'),
      @('exporter','tools/export_material_master.py'),
      @('test','tests/material_master_export_tests.py'))){
    $sourceRow=Find-Source $binding[1]
    if(-not [StringComparer]::OrdinalIgnoreCase.Equals(
        [string]$exportReport.source_sha256.($binding[0]),[string]$sourceRow.sha256)){
      throw "Exporter report source binding changed: $($binding[0])"
    }
  }

  $materialExe=Join-Path $taskRoot 'MaterialBridgeProbe.exe'
  $bridgeExe=Join-Path $taskRoot 'AuthoredSkyBridgeProbe.exe'
  $assets=Join-Path $taskRoot 'assets'
  $kit=Join-Path $taskRoot 'kit'
  $savedVideo=[Environment]::GetEnvironmentVariable('SDL_VIDEODRIVER','Process')
  $savedRenderer=[Environment]::GetEnvironmentVariable('SDL_RENDER_DRIVER','Process')
  try {
    [Environment]::SetEnvironmentVariable('SDL_VIDEODRIVER','dummy','Process')
    [Environment]::SetEnvironmentVariable('SDL_RENDER_DRIVER','software','Process')
    Invoke-Checked $materialExe @('--software-contract') 'material-software.log'
  } finally {
    [Environment]::SetEnvironmentVariable('SDL_VIDEODRIVER',$savedVideo,'Process')
    [Environment]::SetEnvironmentVariable('SDL_RENDER_DRIVER',$savedRenderer,'Process')
  }

  $materialOutput=Join-Path $taskRoot 'material-native-01'
  Invoke-Checked $materialExe @($materialOutput) 'material-native-01.log'
  $materialReport=Get-Content -Raw -LiteralPath (Join-Path $materialOutput 'material-bridge-report.json') | ConvertFrom-Json
  $materialPng=@(Get-ChildItem -LiteralPath $materialOutput -File -Filter '*.png' | Select-Object -ExpandProperty Name)
  $materialPngDifference=@(Compare-Object -ReferenceObject ($expectedMaterialPng | Sort-Object) -DifferenceObject ($materialPng | Sort-Object))
  if($materialReport.complete -ne $true -or $materialReport.backend -ne 'direct3d12' -or
      $materialReport.software -ne $false -or @($materialReport.samples.PSObject.Properties).Count -ne 19 -or
      @($materialReport.fault_rollbacks).Count -ne 3 -or
      @($materialReport.schema2.negative_candidates).Count -ne 15 -or
      $materialPngDifference.Count){
    throw 'MaterialBridge native report/22-PNG inventory differs from the focused oracle'
  }

  $bridgeReports=@()
  foreach($serial in 1..2){
    $output=Join-Path $taskRoot ("bridge-native-0$serial")
    Invoke-Checked $bridgeExe @($assets,$kit,$output) ("bridge-native-0$serial.log")
    if(@(Get-ChildItem -LiteralPath $output -File -Filter '*.png' -Recurse).Count -ne 0){
      throw "Bridge run $serial unexpectedly emitted PNG/GPU-frame evidence"
    }
    $report=Get-Content -Raw -LiteralPath (Join-Path $output 'authored-sky-bridge-report.json') | ConvertFrom-Json
    $checkNames=@($report.checks | ForEach-Object {$_.name})
    $invalidNames=@($report.parser_transaction.invalid_candidates | ForEach-Object {$_.case})
    $variantNames=@($report.derived_variants | ForEach-Object {$_.name})
    $failureNames=@($report.adapter.failure_matrix | ForEach-Object {$_.case})
    $checkDifference=@(Compare-Object -ReferenceObject ($expectedBridgeChecks | Sort-Object) -DifferenceObject ($checkNames | Sort-Object))
    $invalidDifference=@(Compare-Object -ReferenceObject ($expectedInvalids | Sort-Object) -DifferenceObject ($invalidNames | Sort-Object))
    $variantDifference=@(Compare-Object -ReferenceObject ($expectedInvalids | Sort-Object) -DifferenceObject ($variantNames | Sort-Object))
    $failureDifference=@(Compare-Object -ReferenceObject ($expectedAdapterFailures | Sort-Object) -DifferenceObject ($failureNames | Sort-Object))
    if($report.complete -ne $false -or $report.native_authored_shadow -ne 'NOT_RUN' -or
        $report.check_count -ne 13 -or @($report.checks).Count -ne 13 -or
        @($report.checks | Where-Object {$_.passed -ne $true}).Count -or
        $checkDifference.Count -or
        @($report.parser_transaction.invalid_candidates).Count -ne 19 -or
        $invalidDifference.Count -or
        @($report.derived_variants).Count -ne 19 -or
        $variantDifference.Count -or
        @($report.adapter.failure_matrix).Count -ne 13 -or
        $failureDifference.Count -or
        $report.device.backend -ne 'direct3d12' -or $report.device.software -ne $false){
      throw "AuthoredSkyBridge report $serial differs from the 13/19/13 focused oracle"
    }
    $bridgeSource=Find-Source 'tools/authored_sky_bridge_probe.cpp'
    $bridgeExeRow=@($built.executables | Where-Object {[IO.Path]::GetFileName([string]$_.path) -eq 'AuthoredSkyBridgeProbe.exe'})[0]
    $kitManifest=Join-Path $kit 'manifest.json'
    $kitManifestJson=Get-Content -Raw -LiteralPath $kitManifest | ConvertFrom-Json
    $kitWorld=Join-Path $kit 'art/world.json'
    $kitCalibration=Get-Content -Raw -LiteralPath (Join-Path $kit 'art/material/calibration.json') | ConvertFrom-Json
    if(-not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.source_sha256,[string]$bridgeSource.sha256) -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.executable_sha256,[string]$bridgeExeRow.sha256) -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.runtime_shader_manifest_sha256,
          (Get-FileHash -LiteralPath (Join-Path $assets 'shaders/manifest.json') -Algorithm SHA256).Hash) -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.exported_manifest_sha256,
          (Get-FileHash -LiteralPath $kitManifest -Algorithm SHA256).Hash) -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.exported_content_sha256,
          [string]$kitManifestJson.content_sha256) -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$report.exported_world_sha256,
          (Get-FileHash -LiteralPath $kitWorld -Algorithm SHA256).Hash) -or
        $report.exporter_sky_shadow_triangles_sha256 -ne $kitCalibration.sky_shadow_triangles_sha256 -or
        $report.exporter_sky_shadow_triangles_recomputed_sha256 -ne $kitCalibration.sky_shadow_triangles_sha256 -or
        $exportReport.authored_sky_shadow.descriptor_sha256 -ne $kitCalibration.sky_shadow_triangles_sha256){
      throw "AuthoredSkyBridge report $serial provenance/hash binding failed"
    }
    $bridgeReports += [ordered]@{output=$output;report=$report;
      derived_ledger=(Get-TreeLedger (Join-Path $output 'derived-kits'));
      compact_ledger=(Get-TreeLedger (Join-Path $output 'compact-fixture'))}
  }

  if((Json-Text $bridgeReports[0].report.parser_transaction) -ne
       (Json-Text $bridgeReports[1].report.parser_transaction) -or
      (Json-Text $bridgeReports[0].report.adapter) -ne
       (Json-Text $bridgeReports[1].report.adapter) -or
      (Json-Text $bridgeReports[0].report.descriptor) -ne
       (Json-Text $bridgeReports[1].report.descriptor)){
    throw 'Repeated bridge runs changed deterministic parser/adapter/descriptor semantics'
  }
  $normalizedVariants=@(
    @($bridgeReports[0].report.derived_variants | Sort-Object name | ForEach-Object {
      [ordered]@{name=$_.name;world_sha256=$_.world_sha256;manifest_sha256=$_.manifest_sha256}
    }),
    @($bridgeReports[1].report.derived_variants | Sort-Object name | ForEach-Object {
      [ordered]@{name=$_.name;world_sha256=$_.world_sha256;manifest_sha256=$_.manifest_sha256}
    })
  )
  if((Json-Text $normalizedVariants[0]) -ne (Json-Text $normalizedVariants[1]) -or
      (Json-Text $bridgeReports[0].derived_ledger) -ne (Json-Text $bridgeReports[1].derived_ledger) -or
      (Json-Text $bridgeReports[0].compact_ledger) -ne (Json-Text $bridgeReports[1].compact_ledger)){
    throw 'Repeated bridge runs changed compact/derived fixture inventories or bytes'
  }
  foreach($bridgeReport in $bridgeReports){
    if($bridgeReport.report.device.device -ne $materialReport.device -or
        $bridgeReport.report.device.driver -ne $materialReport.driver -or
        $bridgeReport.report.device.backend -ne $materialReport.backend -or
        $bridgeReport.report.device.software -ne $materialReport.software){
      throw 'Focused native probes did not use the same GPU device/driver/backend identity'
    }
  }

  Check-Anchors
  $sourceFinal=@($built.source_provenance | ForEach-Object {
    $originHash=(Get-FileHash -LiteralPath $_.repository_path -Algorithm SHA256).Hash
    $snapshotHash=(Get-FileHash -LiteralPath $_.snapshot_path -Algorithm SHA256).Hash
    [ordered]@{relative_path=$_.relative_path;sha256=$_.sha256;
      repository_sha256=$originHash;snapshot_sha256=$snapshotHash;
      unchanged=([StringComparer]::OrdinalIgnoreCase.Equals($originHash,[string]$_.sha256) -and
                 [StringComparer]::OrdinalIgnoreCase.Equals($snapshotHash,[string]$_.sha256))}
  })
  if(@($sourceFinal | Where-Object {-not $_.unchanged}).Count){throw 'Build source cohort changed during verification'}
  Write-NewJson (Join-Path $taskRoot 'authored-bridge-source-final.json') $sourceFinal 7

  $ledger=[ordered]@{
    classification='focused authored schema-2 loader/adapter component verification; not the authored native shadow gate'
    component_verification_passed=$true
    authored_sky_product_goal_complete=$false
    proof_root=$taskRoot
    exporter_proof_root=$exportRoot
    build_report_sha256=$buildReportHash
    verifier_sha256=$verifierHash
    exporter=[ordered]@{files=10;probes=12;negative_cases=41;
      descriptor_sha256=$exportReport.authored_sky_shadow.descriptor_sha256}
    material_bridge=[ordered]@{software_contract_exit=0;native_exit=0;png_count=22;
      backend=$materialReport.backend;device=$materialReport.device;driver=$materialReport.driver}
    authored_bridge=[ordered]@{runs=2;checks_per_run=13;schema_invalids=19;
      adapter_failures=13;png_count=0;native_authored_shadow='NOT_RUN';complete=$false}
    authored_native_probe=[ordered]@{included_in_build=$IncludeNativeProbe.IsPresent;
      executed=$false;native_authored_shadow_pixels='NOT_RUN'}
    full_suite='NOT_RUN'
    native_authored_shadow_pixels='NOT_RUN'
    GameView='OPEN'
    final_art='OPEN'
    performance='NOT_MEASURED'
    fps_120='NOT_CLAIMED'
    logical_proof_bytes_before_ledger=[int64](Get-ChildItem -LiteralPath $taskRoot -File -Recurse | Measure-Object Length -Sum).Sum
    logical_exporter_bytes_before_ledger=[int64](Get-ChildItem -LiteralPath $exportRoot -File -Recurse | Measure-Object Length -Sum).Sum
    free_bytes=[int64](Get-Volume -DriveLetter C).SizeRemaining
  }
  Write-NewJson (Join-Path $taskRoot 'authored-bridge-verification-ledger.json') $ledger 8
  Check-Budget 'after verification ledger'
  $verificationSucceeded=$true
} finally {
  $protectedFinal=@($protectedBefore | ForEach-Object {
    $actual=(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash
    [ordered]@{path=$_.path;before_sha256=$_.sha256;after_sha256=$actual;
      unchanged=[StringComparer]::OrdinalIgnoreCase.Equals([string]$_.sha256,$actual)}
  })
  if(!(Test-Path -LiteralPath $protectedFinalPath)){Write-NewJson $protectedFinalPath $protectedFinal 6}
  if(@($protectedFinal | Where-Object {-not $_.unchanged}).Count){throw 'Protected prior evidence changed'}
  Check-Budget 'final verifier accounting after protected evidence'
}

if(-not $verificationSucceeded){throw 'Authored bridge verification did not reach its success ledger'}
Get-Content -Raw -LiteralPath (Join-Path $taskRoot 'authored-bridge-verification-ledger.json')
