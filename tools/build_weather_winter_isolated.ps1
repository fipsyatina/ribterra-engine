param([Parameter(Mandatory=$true)][string]$OutputRoot)

$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Use PowerShell 7 (pwsh)'}
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not [StringComparer]::OrdinalIgnoreCase.Equals($repoRoot,'C:\Users\fipst\Desktop\ribterra')){
    throw 'Authoritative repository mismatch'
}
$buildRoot=Join-Path $repoRoot 'build'
$outPath=[IO.Path]::GetFullPath($OutputRoot)
if(-not [StringComparer]::OrdinalIgnoreCase.Equals((Split-Path $outPath),$buildRoot) -or
   (Test-Path -LiteralPath $outPath)){
    throw 'OutputRoot must be a fresh immediate repository build child'
}
foreach($directory in @($repoRoot,$buildRoot)){
    $item=Get-Item -LiteralPath $directory
    if(-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
        throw "Invalid build ancestry: $directory"
    }
}

function Test-Headroom([string]$phase){
    $free=[int64](Get-Volume -DriveLetter C).SizeRemaining
    if($free -lt 402653184){throw "Free space below 384MiB during ${phase}: $free"}
    if(Test-Path -LiteralPath $outPath){
        [int64]$bytes=0
        foreach($file in Get-ChildItem -LiteralPath $outPath -Recurse -File){$bytes+=$file.Length}
        if($bytes -gt 134217728){throw "Authority root exceeds 128MiB during ${phase}: $bytes"}
    }
}
function Assert-NewPath([string]$path){
    if(Test-Path -LiteralPath $path){throw "Refuse overwrite: $path"}
}
function Get-HashRow([string]$path){
    $file=Get-Item -LiteralPath $path
    if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){
        throw "Not a regular file: $path"
    }
    [ordered]@{path=$file.FullName;bytes=[int64]$file.Length;
        sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
}
function Write-NewText([string]$path,[string]$value){
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($value)
        $stream.Write($bytes,0,$bytes.Length)
        $stream.Flush($true)
    }finally{$stream.Dispose()}
}
function Write-NewJson([string]$path,[object]$value){
    Write-NewText $path (($value | ConvertTo-Json -Depth 14)+"`n")
}
function Copy-NewFile([string]$source,[string]$destination){
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    $inputStream=[IO.File]::OpenRead($source)
    try{
        $outputStream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputStream.CopyTo($outputStream)}finally{$outputStream.Dispose()}
    }finally{$inputStream.Dispose()}
}
function Resolve-SystemLibrary([string]$name){
    foreach($directory in ($env:LIB -split ';')){
        if([string]::IsNullOrWhiteSpace($directory)){continue}
        $path=Join-Path $directory $name
        if(Test-Path -LiteralPath $path -PathType Leaf){return (Resolve-Path $path).Path}
    }
    throw "Missing toolchain library: $name"
}
function Get-TreeNames([string]$root){
    $item=Get-Item -LiteralPath $root
    if(-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
        throw "Invalid input tree: $root"
    }
    @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object {
        [IO.Path]::GetRelativePath($root,$_.FullName).Replace('\','/')
    })
}
function Compare-Binding([object[]]$rows){
    @($rows | ForEach-Object {
        $before=$_; $after=$null; $problem=''
        try{$after=Get-HashRow $before.path}catch{$problem=$_.Exception.Message}
        [ordered]@{before=$before;after=$after;error=$problem;
            unchanged=($null -ne $after -and $before.bytes -eq $after.bytes -and $before.sha256 -eq $after.sha256)}
    })
}
$commandRows=[Collections.Generic.List[object]]::new()
function Invoke-Checked([string]$exe,[object[]]$arguments,[string]$log){
    Test-Headroom "before $log"
    $logPath=Join-Path $outPath $log
    Assert-NewPath $logPath
    Assert-NewPath ($logPath+'.exit.txt')
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$exe
    $start.WorkingDirectory=$outPath
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    foreach($argument in $arguments){$start.ArgumentList.Add([string]$argument)}
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$start
    $code=-1; $stdout=''; $stderr=''
    try{
        if(-not $process.Start()){throw "Could not start: $exe"}
        $stdoutTask=$process.StandardOutput.ReadToEndAsync()
        $stderrTask=$process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout=$stdoutTask.GetAwaiter().GetResult()
        $stderr=$stderrTask.GetAwaiter().GetResult()
        $code=[int]$process.ExitCode
    }catch{
        $stderr+="`n"+$_.Exception.Message
    }finally{
        $process.Dispose()
        Write-NewText $logPath ($stdout+$stderr)
        Write-NewText ($logPath+'.exit.txt') "$code`n"
        $commandRows.Add([ordered]@{executable=$exe;arguments=@($arguments);working_directory=$outPath;
            exit_code=$code;log=(Get-HashRow $logPath);exit=(Get-HashRow ($logPath+'.exit.txt'))})
    }
    if($stdout){Write-Output $stdout}
    if($stderr){Write-Output $stderr}
    if($code -ne 0){throw "Command failed ($code): $log"}
    Test-Headroom "after $log"
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
    weather='src/sim/weather.cpp'
    game_server='src/server/game_server.cpp'
    gns_transport='src/net/gns_transport.cpp'
    protocol='src/net/protocol.cpp'
    network_session='src/net/network_session.cpp'
    client_session='src/client/client_session.cpp'
    weather_winter_probe='tools/weather_winter_probe.cpp'
}
$incClosure=@(
    'src/sim/world_settlement.inc','src/sim/world_economy.inc','src/sim/world_wilderness.inc',
    'src/sim/world_nations.inc','src/sim/world_logistics.inc','src/sim/world_logistics_validation.inc',
    'src/sim/world_manual.inc','src/sim/world_manual_validation.inc','src/sim/world_autonomy.inc',
    'src/sim/world_professions.inc','src/sim/world_professions_validation.inc','src/sim/world_work.inc',
    'src/sim/world_work_validation.inc','src/sim/world_economy_api.inc','src/sim/world_wilderness_api.inc',
    'src/sim/world_nations_api.inc','src/server/game_server_lobby.inc','src/client/client_production_group.inc')
$repositoryFiles=@($translationUnits.Values)+$incClosure+@(
    'tools/compile_skill_balance.py','data/skills.json','schemas/world_save.fbs','schemas/network.fbs',
    'schemas/network_frame.fbs','CMakeLists.txt','tools/build_weather_winter_isolated.ps1',
    'tools/weather_domain_probe.cpp','tools/build_weather_domain_isolated.ps1',
    'tools/run_weather_domain_native.py','tests/weather_domain_evidence_tests.py',
    'tools/run_weather_winter_native.py','tests/weather_winter_evidence_tests.py',
    'tests/gameview_cumulative_evidence_tests.py','tests/weather_cpu_evidence.py')
$sdkRoot=Join-Path $repoRoot 'vcpkg_installed/x64-windows-release/include'
$sdkDirectories=@('entt','nlohmann','flatbuffers','openssl','GameNetworkingSockets')
$trees=@([ordered]@{origin=(Join-Path $repoRoot 'include');relative='include';names=@()})+
    @($sdkDirectories | ForEach-Object {[ordered]@{origin=(Join-Path $sdkRoot $_);relative=('external/include/'+$_);names=@()}})
$inputRows=[Collections.Generic.List[object]]::new()
foreach($relative in $repositoryFiles){
    $inputRows.Add([ordered]@{relative_path=$relative;origin=(Get-HashRow (Join-Path $repoRoot $relative))})
}
foreach($tree in $trees){
    $tree.names=@(Get-TreeNames $tree.origin)
    foreach($name in $tree.names){
        $inputRows.Add([ordered]@{relative_path=($tree.relative+'/'+$name);origin=(Get-HashRow (Join-Path $tree.origin $name))})
    }
}
foreach($name in @('zstd.h','zstd_errors.h')){
    $inputRows.Add([ordered]@{relative_path=('external/include/'+$name);origin=(Get-HashRow (Join-Path $sdkRoot $name))})
}

$launcher=Join-Path $repoRoot 'Play Ribterra.cmd'
$reworkLauncher=Join-Path $repoRoot 'Play Ribterra Rework.cmd'
$pointer=Join-Path $repoRoot 'dist/current-build.txt'
$launcherExecutable=Join-Path $repoRoot 'Ribterra.exe'
if(-not (Test-Path -LiteralPath $launcherExecutable -PathType Leaf)){
    $pointerValue=(Get-Content -Raw -LiteralPath $pointer).Trim()
    if([string]::IsNullOrWhiteSpace($pointerValue)){throw 'Empty current build pointer'}
    $launcherExecutable=[IO.Path]::GetFullPath((Join-Path $repoRoot $pointerValue))
}
$reworkMatch=[regex]::Match((Get-Content -Raw -LiteralPath $reworkLauncher),
    '(?im)^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$')
if(-not $reworkMatch.Success){throw 'Could not resolve protected rework executable'}
$reworkExecutable=[IO.Path]::GetFullPath((Join-Path $repoRoot $reworkMatch.Groups[1].Value))
$protectedBefore=@(@($launcher,$reworkLauncher,$pointer,$launcherExecutable,$reworkExecutable) |
    Sort-Object -Unique | ForEach-Object {Get-HashRow $_})

$setup='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1'
$python='C:/Python314/python.exe'
$flatc=Join-Path $repoRoot 'vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe'
$setupBinding=Get-HashRow $setup
$pythonBinding=Get-HashRow $python
$flatcBinding=Get-HashRow $flatc
Test-Headroom 'before toolchain discovery'
. $setup -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$compiler=(Get-Command cl.exe).Source
$linker=(Get-Command link.exe).Source
$toolBefore=@($setupBinding,$pythonBinding,$flatcBinding,(Get-HashRow $compiler),(Get-HashRow $linker))
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
    'absl_throw_delegate.lib','absl_raw_logging_internal.lib','absl_log_severity.lib','libcrypto.lib')
$systemNames=@('winmm.lib','iphlpapi.lib','crypt32.lib','bcrypt.lib','shell32.lib','ws2_32.lib',
    'kernel32.lib','user32.lib','gdi32.lib','winspool.lib','ole32.lib','oleaut32.lib','uuid.lib',
    'comdlg32.lib','advapi32.lib')
$linkLibraries=@($vcpkgNames | ForEach-Object {Join-Path $vcpkgLibRoot $_})+
    @($systemNames | ForEach-Object {Resolve-SystemLibrary $_})
$libraryBefore=@($linkLibraries | ForEach-Object {Get-HashRow $_})

Test-Headroom 'before output creation'
New-Item -ItemType Directory -Path $outPath | Out-Null
foreach($name in @('source','obj','generated','compiler-temp')){
    New-Item -ItemType Directory -Path (Join-Path $outPath $name) | Out-Null
}
$sourceRoot=Join-Path $outPath 'source'
$savedTemp=$env:TEMP; $savedTmp=$env:TMP; $savedBytecode=$env:PYTHONDONTWRITEBYTECODE
$sourceRows=@(); $options=@(); $objects=@(); $generated=@(); $executable=$null
$success=$false; $buildFailure=''
try{
    foreach($inputRow in $inputRows){
        $destination=Join-Path $sourceRoot $inputRow.relative_path
        Copy-NewFile $inputRow.origin.path $destination
        $copy=Get-HashRow $destination
        if($copy.bytes -ne $inputRow.origin.bytes -or $copy.sha256 -ne $inputRow.origin.sha256){
            throw "Snapshot differs from preflight: $($inputRow.relative_path)"
        }
        $sourceRows+=[ordered]@{relative_path=$inputRow.relative_path;origin=$inputRow.origin;snapshot=$copy}
    }
    Write-NewJson (Join-Path $outPath 'source-provenance.json') $sourceRows
    Test-Headroom 'after source and SDK snapshot'
    $env:TEMP=Join-Path $outPath 'compiler-temp'; $env:TMP=$env:TEMP; $env:PYTHONDONTWRITEBYTECODE='1'
    Assert-NewPath (Join-Path $outPath 'generated/skill_balance_generated.h')
    Invoke-Checked $python @((Join-Path $sourceRoot 'tools/compile_skill_balance.py'),
        (Join-Path $sourceRoot 'data/skills.json'),(Join-Path $outPath 'generated/skill_balance_generated.h')) 'generate-skill-balance.log'
    foreach($schema in @('world_save','network','network_frame')){
        Assert-NewPath (Join-Path $outPath ('generated/'+$schema+'_generated.h'))
        Invoke-Checked $flatc @('--cpp','--scoped-enums','--gen-object-api','-o',(Join-Path $outPath 'generated'),
            (Join-Path $sourceRoot ('schemas/'+$schema+'.fbs'))) ('generate-'+$schema+'.log')
    }
    $options=@('/nologo','/TP','/DWIN32','/D_WINDOWS','/EHsc','/O2','/Ob2','/DNDEBUG',
        '/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/GR','/Zc:__cplusplus',
        '/DRIBTERRA_PROBE_BUILD="weather-winter-isolated-unversioned"',
        ('/I'+(Join-Path $sourceRoot 'include')),('/I'+(Join-Path $outPath 'generated')),
        ('/external:I'+(Join-Path $sourceRoot 'external/include/GameNetworkingSockets')),
        ('/external:I'+(Join-Path $sourceRoot 'external/include')),'/external:W0')
    foreach($entry in $translationUnits.GetEnumerator()){
        $object=Join-Path $outPath ('obj/'+$entry.Key+'.obj')
        Assert-NewPath $object
        $extra=if($entry.Key -eq 'gns_transport'){@('/DSTEAMNETWORKINGSOCKETS_STATIC_LINK')}else{@()}
        Invoke-Checked $compiler ($options+$extra+@('/c',(Join-Path $sourceRoot $entry.Value),('/Fo'+$object))) ('compile-'+$entry.Key+'.log')
    }
    $objects=@($translationUnits.Keys | ForEach-Object {Get-HashRow (Join-Path $outPath ('obj/'+$_+'.obj'))})
    Assert-NewPath (Join-Path $outPath 'WeatherWinterProbe.exe')
    Invoke-Checked $linker (@('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
        ('/out:'+(Join-Path $outPath 'WeatherWinterProbe.exe')))+@($objects | ForEach-Object {$_.path})+$linkLibraries) 'link-weather-winter.log'
    $executable=Get-HashRow (Join-Path $outPath 'WeatherWinterProbe.exe')
    $generated=@(Get-ChildItem -LiteralPath (Join-Path $outPath 'generated') -File | Sort-Object Name |
        ForEach-Object {Get-HashRow $_.FullName})
    if($generated.Count -ne 4 -or $objects.Count -ne 15 -or $commandRows.Count -ne 20){
        throw 'Expected exactly four generated headers, fifteen objects and twenty actual commands'
    }
    $success=$true
}catch{
    $buildFailure=$_.Exception.Message
}finally{
    $env:TEMP=$savedTemp; $env:TMP=$savedTmp; $env:PYTHONDONTWRITEBYTECODE=$savedBytecode
    $originFinal=@(Compare-Binding @($inputRows | ForEach-Object {$_.origin}))
    $sourceFinal=@($sourceRows | ForEach-Object {
        $bindings=@(Compare-Binding @($_.origin,$_.snapshot))
        [ordered]@{relative_path=$_.relative_path;bindings=$bindings;
            unchanged=(@($bindings | Where-Object {-not $_.unchanged}).Count -eq 0)}
    })
    $treeFinal=@($trees | ForEach-Object {
        $now=@(); $problem=''
        try{$now=@(Get-TreeNames $_.origin)}catch{$problem=$_.Exception.Message}
        [ordered]@{origin=$_.origin;before_count=$_.names.Count;after_count=$now.Count;error=$problem;
            unchanged=($problem -eq '' -and (($now -join "`n") -ceq ($_.names -join "`n")))}
    })
    $toolFinal=@(Compare-Binding $toolBefore)
    $libraryFinal=@(Compare-Binding $libraryBefore)
    $protectedFinal=@(Compare-Binding $protectedBefore)
    $unchanged=(@($originFinal+$sourceFinal+$treeFinal+$toolFinal+$libraryFinal+$protectedFinal |
        Where-Object {-not $_.unchanged}).Count -eq 0)
    if(-not $unchanged){$success=$false; $buildFailure+='; input binding changed during build'}
    try{Test-Headroom 'before final report'}catch{$success=$false; $buildFailure+='; '+$_.Exception.Message}
    Write-NewJson (Join-Path $outPath 'weather-winter-build-report.json') ([ordered]@{
        schema=1;complete=$success;error=$buildFailure;root=$outPath;authoritative_repository=$repoRoot;
        classification='fresh bounded CPU authority build; executable not launched';
        build_label='weather-winter-isolated-unversioned';translation_unit_count=15;compiled_units=15;
        internal_production_translation_unit_count=14;translation_units=@($translationUnits.Values);
        nested_inc_closure=$incClosure;source_files=$sourceRows;source_final=$sourceFinal;origin_final=$originFinal;
        input_tree_final=$treeFinal;tool_files=$toolBefore;tool_final=$toolFinal;
        link_libraries=$libraryBefore;library_final=$libraryFinal;protected=$protectedFinal;
        generated_headers=$generated;objects=$objects;executable=$executable;flags=$options;
        commands=@($commandRows);
        command_evidence=@($commandRows | ForEach-Object {$_.log;$_.exit});
        execution=[ordered]@{probe=$false;game=$false;gpu=$false};
        claims=[ordered]@{world_authority=$false;persistence=$false;wire=$false;remote=$false;full_engine=$false;fps=$false};
        retention='existing files untouched; fresh outputs and failed staging retained; no cleanup or installs'
    })
}
if(-not $success){throw "Authority build failed; retained report/output: $buildFailure"}
Test-Headroom 'after final report'
Write-Output "Strict isolated weather authority build complete without execution: $outPath"
