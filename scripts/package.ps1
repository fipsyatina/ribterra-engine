param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
$manifest = Get-Content -LiteralPath (Join-Path $projectDir 'vcpkg.json') -Raw | ConvertFrom-Json
$version = [string]$manifest.'version-string'
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Package version must use major.minor.patch.' }
$packageDir = Join-Path $projectDir ('dist\releases\' + $version + '\Ribterra')
foreach ($required in @('Ribterra.exe', 'RibterraServer.exe', 'RibterraNetProbe.exe', 'SimRunner.exe',
                        'SDL3.dll', 'SDL3_ttf.dll', 'assets\shaders\sprite.vert.dxil',
                        'assets\shaders\sprite.frag.dxil', 'assets\shaders\manifest.json',
                        'assets\shaders\sprite.vert.json', 'assets\shaders\sprite.frag.json',
                        'assets\shaders\surface_lit.vert.dxil', 'assets\shaders\surface_lit.frag.dxil',
                        'assets\shaders\surface_lit.vert.json', 'assets\shaders\surface_lit.frag.json',
                        'assets\fonts\NotoSans-Regular.ttf', 'assets\fonts\CormorantGaramond.ttf',
                        'assets\art\bridges-017-0.png', 'assets\art\living-018-0.png', 'assets\ui\concept.rcss',
                        'assets\ui\art\menu-landscape.png', 'assets\ui\art\building-bridge.png',
                        'assets\ui\art\glyph-13.png', 'licenses\Cormorant-OFL.txt', 'assets\manifest.json',
                        'assets\art\world.json', 'assets\art\buildings-014-0.png', 'assets\art\buildings-014-1.png',
                        'assets\ui\art\building-mine.png', 'assets\ui\art\building-barracks.png', 'assets\art\atlas-0.png', 'assets\art\atlas-1.png',
                        'assets\art\atlas-2.png', 'assets\art\atlas-3.png', 'assets\ui\art\theme.rcss',
                        'assets\ui\menu.rml', 'assets\ui\hud.rml', 'assets\ui\ribterra.rcss',
                        'assets\locales\en.json', 'assets\locales\uk.json', 'assets\locales\ru.json',
                        'assets\locales\pl.json', 'assets\locales\es.json',
                        'licenses\Ribterra-Assets.txt', 'THIRD_PARTY_NOTICES.md', 'README.md', 'Play Ribterra.cmd')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $required) -PathType Leaf)) {
        throw ('Incomplete package: ' + $required)
    }
}
$archive = Join-Path $projectDir ('dist\Ribterra-Windows-x64-' + $version + '.zip')
Compress-Archive -LiteralPath $packageDir -DestinationPath $archive -Force
[pscustomobject]@{
    Archive = $archive
    Bytes = (Get-Item -LiteralPath $archive).Length
    SHA256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
}
