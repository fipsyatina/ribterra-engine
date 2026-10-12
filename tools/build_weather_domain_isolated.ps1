param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Use PowerShell7'}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if($repo -ne 'C:\Users\fipst\Desktop\ribterra'){throw 'Authoritative repository mismatch'}
$out=[IO.Path]::GetFullPath($OutputRoot)
if((Split-Path $out) -ne (Join-Path $repo 'build') -or (Test-Path -LiteralPath $out)){throw 'Output must be a fresh immediate build child'}
function Budget {
    if([int64](Get-Volume -DriveLetter C).SizeRemaining -lt 402653184){throw 'Free space below384MiB'}
    if(Test-Path -LiteralPath $out){
        [int64]$bytes=0
        foreach($f in Get-ChildItem -LiteralPath $out -Recurse -File){$bytes+=$f.Length}
        if($bytes -gt 134217728){throw 'Domain root exceeds128MiB'}
    }
}
function Row([string]$path){
    $f=Get-Item -LiteralPath $path
    if($f.PSIsContainer -or ($f.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Not a regular file:$path"}
    [ordered]@{path=$f.FullName;bytes=[int64]$f.Length;sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
function New-Json([string]$path,$data){
    if(Test-Path -LiteralPath $path){throw "Refuse overwrite:$path"}
    [IO.File]::WriteAllText($path,($data | ConvertTo-Json -Depth 10))
}
function Checked([string]$exe,[object[]]$arguments,[string]$log){
    Budget
    $path=Join-Path $out $log
    if(Test-Path -LiteralPath $path){throw 'Command log exists'}
    $saved=$ErrorActionPreference
    try{$ErrorActionPreference='Continue'; & $exe @arguments 2>&1 | Tee-Object -FilePath $path | Out-Host; $code=[int]$LASTEXITCODE}
    finally{$ErrorActionPreference=$saved}
    if(!(Test-Path -LiteralPath $path)){[IO.File]::WriteAllText($path,'')}
    [IO.File]::WriteAllText($path+'.exit.txt',"$code`n")
    if($code -ne 0){throw "Command failed($code):$log"}
    Budget
}
$inputs=@('src/sim/weather.cpp','tools/weather_domain_probe.cpp','tools/build_weather_domain_isolated.ps1',
    'tools/run_weather_domain_native.py','tests/weather_domain_evidence_tests.py',
    'tests/gameview_cumulative_evidence_tests.py','build/weather-carryover-derivation-2026-10-08.json',
    'build/weather-folded-key-derivation-2026-10-08.json')
foreach($p in $inputs){if(!(Test-Path -LiteralPath (Join-Path $repo $p) -PathType Leaf)){throw "Missing source:$p"}}
$jsonRoot=Join-Path $repo 'vcpkg_installed/x64-windows-release/include/nlohmann'
if(!(Test-Path -LiteralPath (Join-Path $jsonRoot 'json.hpp'))){throw 'Missing installed nlohmann headers'}
$launcherPaths=@('Play Ribterra.cmd','Play Ribterra Rework.cmd','dist/current-build.txt')
$protected=@($launcherPaths | ForEach-Object {Row (Join-Path $repo $_)})
Budget
New-Item -ItemType Directory -Path $out | Out-Null
foreach($d in @('source','source/external','obj','compiler-temp')){New-Item -ItemType Directory -Path (Join-Path $out $d) | Out-Null}
$snapshot=Join-Path $out 'source'
foreach($p in $inputs){
    $dest=Join-Path $snapshot $p
    New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo $p) -Destination $dest
}
Copy-Item -LiteralPath (Join-Path $repo 'include') -Destination (Join-Path $snapshot 'include') -Recurse
Copy-Item -LiteralPath $jsonRoot -Destination (Join-Path $snapshot 'external/nlohmann') -Recurse
$sources=@(Get-ChildItem -LiteralPath $snapshot -File -Recurse | Sort-Object FullName | ForEach-Object {
    $relative=[IO.Path]::GetRelativePath($snapshot,$_.FullName).Replace('\','/')
    $origin=if($relative.StartsWith('external/nlohmann/')){Join-Path $jsonRoot $relative.Substring(18)}else{Join-Path $repo $relative}
    $bound=Row $origin; $copy=Row $_.FullName
    if($copy.sha256 -ne $bound.sha256 -or $copy.bytes -ne $bound.bytes){throw "Snapshot differs:$relative"}
    [ordered]@{relative_path=$relative;repository=$bound;snapshot=$copy}
})
New-Json (Join-Path $out 'source-provenance.json') $sources
$savedTemp=$env:TEMP; $savedTmp=$env:TMP
try {
    . 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $env:TEMP=Join-Path $out 'compiler-temp'; $env:TMP=$env:TEMP
    $compiler=(Get-Command cl.exe).Source; $linker=(Get-Command link.exe).Source
    $tools=@((Row $compiler),(Row $linker))
    $options=@('/nologo','/TP','/EHsc','/O2','/Ob2','/DNDEBUG','/std:c++20','/MT','/utf-8','/W4','/WX','/permissive-','/Zc:__cplusplus',
        ('/I'+(Join-Path $snapshot 'include')),('/external:I'+(Join-Path $snapshot 'external')),'/external:W0')
    Checked $compiler ($options+@('/c',(Join-Path $snapshot 'src/sim/weather.cpp'),('/Fo'+(Join-Path $out 'obj/weather.obj')))) 'compile-weather.log'
    Checked $compiler ($options+@('/c',(Join-Path $snapshot 'tools/weather_domain_probe.cpp'),('/Fo'+(Join-Path $out 'obj/weather_domain_probe.obj')))) 'compile-weather-domain-probe.log'
    Checked $linker @('/nologo','/machine:x64','/incremental:no','/debug:none','/subsystem:console',
        (Join-Path $out 'obj/weather.obj'),(Join-Path $out 'obj/weather_domain_probe.obj'),
        ('/out:'+(Join-Path $out 'WeatherDomainProbe.exe'))) 'link-weather-domain.log'
    $final=@($sources | ForEach-Object {
        $current=Row $_.repository.path; $copy=Row $_.snapshot.path
        [ordered]@{relative_path=$_.relative_path;current=$current;snapshot=$copy;
            unchanged=($current.sha256 -eq $_.repository.sha256 -and $copy.sha256 -eq $_.snapshot.sha256 -and $current.bytes -eq $_.repository.bytes -and $copy.bytes -eq $_.snapshot.bytes)}
    })
    if(@($final | Where-Object {-not $_.unchanged}).Count){throw 'Domain source mutated during build'}
    $protectedFinal=@($protected | ForEach-Object {$current=Row $_.path; [ordered]@{before=$_;after=$current;unchanged=($current.sha256 -eq $_.sha256 -and $current.bytes -eq $_.bytes)}})
    if(@($protectedFinal | Where-Object {-not $_.unchanged}).Count){throw 'Protected launcher mutated'}
    $commands=@(Get-ChildItem -LiteralPath $out -File | Where-Object {$_.Name -like '*.log' -or $_.Name -like '*.log.exit.txt'} | Sort-Object Name | ForEach-Object {Row $_.FullName})
    New-Json (Join-Path $out 'weather-build-report.json') ([ordered]@{schema=1;complete=$true;root=$out;source_files=$sources;source_final=$final;tool_files=$tools;command_evidence=$commands;protected=$protectedFinal;executable=(Row (Join-Path $out 'WeatherDomainProbe.exe'));compiled_units=2;flags=$options;scope='pure weather component; World/save/wire/UI/FX integration OPEN'})
    Budget
    Write-Output "Strict isolated weather domain build passed:$out"
} finally {$env:TEMP=$savedTemp; $env:TMP=$savedTmp}
