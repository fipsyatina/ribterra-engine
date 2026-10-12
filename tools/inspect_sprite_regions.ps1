param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)][string]$RegionsJson)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new((Resolve-Path -LiteralPath $Path).Path)
try {
    $regions = $RegionsJson | ConvertFrom-Json
    $result = foreach ($region in $regions) {
        $left = $bitmap.Width; $top = $bitmap.Height; $right = -1; $bottom = -1
        $opaque = 0; $transparent = 0; $partial = 0
        for ($y = [int]$region.y; $y -lt [int]$region.y + [int]$region.h; $y++) {
            for ($x = [int]$region.x; $x -lt [int]$region.x + [int]$region.w; $x++) {
                $alpha = $bitmap.GetPixel($x, $y).A
                if ($alpha -lt 8) { $transparent++; continue }
                if ($alpha -ge 248) { $opaque++ } else { $partial++ }
                if ($alpha -lt 32) { continue }
                $left = [Math]::Min($left, $x); $top = [Math]::Min($top, $y)
                $right = [Math]::Max($right, $x); $bottom = [Math]::Max($bottom, $y)
            }
        }
        [pscustomobject]@{ id=$region.id; rect=@($left,$top,($right-$left+1),($bottom-$top+1)); opaque=$opaque; transparent=$transparent; partial=$partial }
    }
    [pscustomobject]@{ width=$bitmap.Width; height=$bitmap.Height; regions=@($result) } | ConvertTo-Json -Depth 5
} finally { $bitmap.Dispose() }
