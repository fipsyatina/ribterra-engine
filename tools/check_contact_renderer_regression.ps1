param([string]$ProofRoot)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if([string]::IsNullOrWhiteSpace($ProofRoot)){
    $proofRoot = Join-Path $repoRoot 'build/contact-targeted-2026-10-07-attempt03'
} else {
    $proofRoot = [IO.Path]::GetFullPath($ProofRoot)
    if((Split-Path $proofRoot) -ne [IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))){
        throw 'ProofRoot must be an existing immediate child of repository build'
    }
}
$regressionRoot = Join-Path $proofRoot 'renderer-regression-01'
$compilerTemp = Join-Path $proofRoot 'compiler-temp'
$minimumFreeBytes = 384MB
$maximumProofBytes = 128MB

function Get-ProofBytes {
    if (-not (Test-Path -LiteralPath $proofRoot -PathType Container)) {
        throw "Missing isolated proof root: $proofRoot"
    }
    $measurement = Get-ChildItem -LiteralPath $proofRoot -Recurse -File |
        Measure-Object -Property Length -Sum
    if ($null -eq $measurement.Sum) { return [int64]0 }
    return [int64]$measurement.Sum
}

function Test-Budget([string]$phase) {
    $freeBytes = [int64](Get-Volume -DriveLetter C).SizeRemaining
    $proofBytes = Get-ProofBytes
    if ($freeBytes -lt $minimumFreeBytes) {
        throw "Free space below 384 MiB during ${phase}: $freeBytes bytes"
    }
    if ($proofBytes -gt $maximumProofBytes) {
        throw "Isolated proof root exceeds 128 MiB during ${phase}: $proofBytes bytes"
    }
    return [ordered]@{ phase = $phase; free_bytes = $freeBytes; proof_bytes = $proofBytes }
}

function Assert-NewPath([string]$path) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite existing path: $path" }
}

function Invoke-Logged([string]$program, [object[]]$arguments, [string]$logPath) {
    Assert-NewPath $logPath
    & $program @arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
    $exitCode = [int]$LASTEXITCODE
    if (-not (Test-Path -LiteralPath $logPath)) {
        [IO.File]::WriteAllText($logPath, '')
    }
    $exitPath = "$logPath.exit.txt"
    Assert-NewPath $exitPath
    [IO.File]::WriteAllText($exitPath, "$exitCode`n")
    return $exitCode
}

function New-HardLink([string]$source, [string]$destination) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Hard-link source is missing: $source"
    }
    Assert-NewPath $destination
    New-Item -ItemType HardLink -Path $destination -Target $source | Out-Null
}

if (-not (Test-Path -LiteralPath $compilerTemp -PathType Container)) {
    throw "Scoped compiler temporary directory is missing: $compilerTemp"
}
Assert-NewPath $regressionRoot
$budgetRows = [Collections.Generic.List[object]]::new()
$budgetRows.Add((Test-Budget 'before regression root creation'))

foreach ($directory in @(
    $regressionRoot,
    (Join-Path $regressionRoot 'source'),
    (Join-Path $regressionRoot 'obj'),
    (Join-Path $regressionRoot 'runtime-soft'),
    (Join-Path $regressionRoot 'runtime-soft/assets'),
    (Join-Path $regressionRoot 'runtime-soft/assets/fonts'),
    (Join-Path $regressionRoot 'runtime-soft/assets/shaders'),
    (Join-Path $regressionRoot 'runtime-gpu'),
    (Join-Path $regressionRoot 'runtime-gpu/assets'),
    (Join-Path $regressionRoot 'runtime-gpu/assets/fonts'),
    (Join-Path $regressionRoot 'runtime-gpu/assets/shaders'))) {
    Assert-NewPath $directory
    New-Item -ItemType Directory -Path $directory | Out-Null
}

$testSource = Join-Path $repoRoot 'tests/render_frame_tests.cpp'
$snapshotSource = Join-Path $regressionRoot 'source/render_frame_tests.cpp'
Copy-Item -LiteralPath $testSource -Destination $snapshotSource
$sourceHash = (Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash
$snapshotHash = (Get-FileHash -LiteralPath $snapshotSource -Algorithm SHA256).Hash
if ($sourceHash -ne $snapshotHash) { throw 'RenderFrameTests source snapshot differs from its input' }

$shaderRoot = Join-Path $proofRoot 'assets/shaders'
$shaderFiles = @(Get-ChildItem -LiteralPath $shaderRoot -File | Sort-Object Name)
$shaderManifest=Get-Content -LiteralPath (Join-Path $shaderRoot 'manifest.json') -Raw | ConvertFrom-Json
$expectedRuntimeShaderCount=@($shaderManifest.programs.PSObject.Properties).Count*4
if ($shaderFiles.Count -ne ($expectedRuntimeShaderCount+1)) { throw "Staged shader inventory differs from strict manifest programs: $($shaderFiles.Count)" }
$runtimeShaderFiles = @($shaderFiles | Where-Object Name -ne 'manifest.json')
if ($runtimeShaderFiles.Count -ne $expectedRuntimeShaderCount) { throw "Runtime shader inventory differs from strict manifest programs: $($runtimeShaderFiles.Count)" }
$shaderBefore = @($shaderFiles | ForEach-Object {
    [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})

. 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' `
    -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:TEMP = $compilerTemp
$env:TMP = $compilerTemp

$testObject = Join-Path $regressionRoot 'obj/render_frame_tests.obj'
$compileLog = Join-Path $regressionRoot 'compile-render-frame-tests.log'
$common = @(
    '/nologo', '/TP', '/DWIN32', '/D_WINDOWS', '/EHsc', '/O2', '/Ob2', '/DNDEBUG',
    '/std:c++20', '/MT', '/utf-8', '/W4', '/WX', '/permissive-',
    ('/I' + (Join-Path $proofRoot 'source/include')),
    ('/external:I' + (Join-Path $repoRoot '.deps/SDL3-3.4.16/include')),
    ('/external:I' + (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/include')),
    ('/external:I' + (Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include')),
    '/external:W0')
$budgetRows.Add((Test-Budget 'before RenderFrameTests compile'))
$compileExit = Invoke-Logged 'cl.exe' ($common + @('/c', $snapshotSource, ('/Fo' + $testObject))) $compileLog
$budgetRows.Add((Test-Budget 'after RenderFrameTests compile'))
if ($compileExit -ne 0) { throw "RenderFrameTests compilation failed with exit $compileExit" }

$linkedExe = Join-Path $regressionRoot 'RenderFrameTests.exe'
$linkLog = Join-Path $regressionRoot 'link-render-frame-tests.log'
$linkArguments = @(
    '/nologo', '/machine:x64', '/incremental:no', '/debug:none', '/subsystem:console',
    ('/out:' + $linkedExe),
    $testObject,
    (Join-Path $proofRoot 'obj/renderer2d.obj'),
    (Join-Path $proofRoot 'obj/contact_shadow.obj'),
    (Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.lib'),
    (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.lib'),
    'kernel32.lib', 'user32.lib', 'gdi32.lib', 'winspool.lib', 'shell32.lib',
    'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'comdlg32.lib', 'advapi32.lib')
$budgetRows.Add((Test-Budget 'before RenderFrameTests link'))
$linkExit = Invoke-Logged 'link.exe' $linkArguments $linkLog
$budgetRows.Add((Test-Budget 'after RenderFrameTests link'))
if ($linkExit -ne 0) { throw "RenderFrameTests link failed with exit $linkExit" }

$runtimeInputs = @(
    [ordered]@{ source = $linkedExe; relative = 'RenderFrameTests.exe' },
    [ordered]@{ source = (Join-Path $repoRoot '.deps/SDL3-3.4.16/lib/x64/SDL3.dll'); relative = 'SDL3.dll' },
    [ordered]@{ source = (Join-Path $repoRoot '.deps/SDL3_ttf-3.2.2/lib/x64/SDL3_ttf.dll'); relative = 'SDL3_ttf.dll' },
    [ordered]@{ source = (Join-Path $repoRoot 'assets/fonts/NotoSans-Regular.ttf'); relative = 'assets/fonts/NotoSans-Regular.ttf' })
foreach ($runtimeName in @('runtime-soft', 'runtime-gpu')) {
    $runtime = Join-Path $regressionRoot $runtimeName
    foreach ($input in $runtimeInputs) {
        New-HardLink $input.source (Join-Path $runtime $input.relative)
    }
    foreach ($shader in $runtimeShaderFiles) {
        New-HardLink $shader.FullName (Join-Path $runtime "assets/shaders/$($shader.Name)")
    }
    $stagedCount = @(Get-ChildItem -LiteralPath $runtime -Recurse -File).Count
    if ($stagedCount -ne ($expectedRuntimeShaderCount+4)) { throw "$runtimeName has unexpected hard-linked runtime inventory: $stagedCount" }
}
$budgetRows.Add((Test-Budget 'after two bounded runtime layouts'))

$softwareLog = Join-Path $regressionRoot 'software.log'
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_RENDER_DRIVER = 'software'
$budgetRows.Add((Test-Budget 'before software regression'))
$softwareExit = Invoke-Logged (Join-Path $regressionRoot 'runtime-soft/RenderFrameTests.exe') @() $softwareLog
$budgetRows.Add((Test-Budget 'after software regression'))

$env:SDL_VIDEODRIVER = $null
$env:SDL_RENDER_DRIVER = $null
$gpuLog = Join-Path $regressionRoot 'gpu.log'
$budgetRows.Add((Test-Budget 'before GPU regression'))
$gpuExit = Invoke-Logged (Join-Path $regressionRoot 'runtime-gpu/RenderFrameTests.exe') @('--gpu') $gpuLog
$budgetRows.Add((Test-Budget 'after GPU regression'))

$shaderAfter = @($shaderFiles | ForEach-Object {
    [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$shaderUnchanged = ($shaderBefore | ConvertTo-Json -Depth 5 -Compress) -eq
    ($shaderAfter | ConvertTo-Json -Depth 5 -Compress)
$budgetRows.Add((Test-Budget 'final'))

$report = [ordered]@{
    classification = 'selected fresh direct renderer regression; not CTest, full game, concept, release, or performance acceptance'
    proof_root = $proofRoot
    regression_root = $regressionRoot
    source = [ordered]@{ path = $testSource; snapshot = $snapshotSource; sha256 = $sourceHash; snapshot_sha256 = $snapshotHash }
    executable = [ordered]@{ path = $linkedExe; sha256 = (Get-FileHash -LiteralPath $linkedExe -Algorithm SHA256).Hash; bytes = (Get-Item $linkedExe).Length }
    readonly_objects = @(
        [ordered]@{ path = (Join-Path $proofRoot 'obj/renderer2d.obj'); sha256 = (Get-FileHash -LiteralPath (Join-Path $proofRoot 'obj/renderer2d.obj') -Algorithm SHA256).Hash },
        [ordered]@{ path = (Join-Path $proofRoot 'obj/contact_shadow.obj'); sha256 = (Get-FileHash -LiteralPath (Join-Path $proofRoot 'obj/contact_shadow.obj') -Algorithm SHA256).Hash })
    runtime = [ordered]@{ per_directory_files = ($expectedRuntimeShaderCount+4); shader_files = $expectedRuntimeShaderCount; hard_links = $true }
    runs = @(
        [ordered]@{ mode = 'software'; argv = @(); SDL_VIDEODRIVER = 'dummy'; SDL_RENDER_DRIVER = 'software'; exit = $softwareExit; log = $softwareLog },
        [ordered]@{ mode = 'gpu'; argv = @('--gpu'); SDL_VIDEODRIVER = $null; SDL_RENDER_DRIVER = $null; exit = $gpuExit; log = $gpuLog })
    staged_shader_inputs_unchanged = $shaderUnchanged
    staged_shader_before = $shaderBefore
    staged_shader_after = $shaderAfter
    budgets = @($budgetRows)
}
$reportPath = Join-Path $regressionRoot 'report.json'
Assert-NewPath $reportPath
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8))

if (-not $shaderUnchanged) { throw 'Staged shader input bytes changed during regression' }
if ($softwareExit -ne 0 -or $gpuExit -ne 0) {
    throw "Renderer regression failed: software=$softwareExit gpu=$gpuExit"
}
Write-Output "Renderer regression passed: software=$softwareExit gpu=$gpuExit report=$reportPath"
