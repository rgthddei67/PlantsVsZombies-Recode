param(
    [Parameter(Mandatory=$true)][string]$Before,
    [Parameter(Mandatory=$true)][string]$After
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$beforeImage = [System.Drawing.Bitmap]::new((Join-Path (Resolve-Path $Before) 'animation_filters.png'))
$afterImage = [System.Drawing.Bitmap]::new((Join-Path (Resolve-Path $After) 'animation_filters.png'))
try {
    if ($beforeImage.Width -ne 1100 -or $beforeImage.Height -ne 600 -or
        $afterImage.Width -ne $beforeImage.Width -or $afterImage.Height -ne $beforeImage.Height) {
        throw 'Unexpected screenshot dimensions'
    }
    $maxDifference = 0
    $maxPathDifference = 0
    $changedPixels = 0
    for ($y = 0; $y -lt $beforeImage.Height; ++$y) {
        for ($x = 0; $x -lt $beforeImage.Width; ++$x) {
            $a = $beforeImage.GetPixel($x, $y)
            $b = $afterImage.GetPixel($x, $y)
            $difference = [Math]::Max([Math]::Abs([int]$a.R - $b.R),
                [Math]::Max([Math]::Abs([int]$a.G - $b.G), [Math]::Abs([int]$a.B - $b.B)))
            if ($a.A -ne $b.A) { throw "Alpha changed at $x,$y" }
            if ($difference -gt 0) { ++$changedPixels }
            $maxDifference = [Math]::Max($maxDifference, $difference)
            # 左三列颜色相同：batch、instance 和 worker 三行必须画出同样的图案。
            if ($x -lt 550 -and $y -lt 145) {
                foreach ($row in 1,2) {
                    $c = $afterImage.GetPixel($x, $y + 145 * $row)
                    $pathDifference = [Math]::Max([Math]::Abs([int]$b.R - $c.R),
                        [Math]::Max([Math]::Abs([int]$b.G - $c.G), [Math]::Abs([int]$b.B - $c.B)))
                    $maxPathDifference = [Math]::Max($maxPathDifference, $pathDifference)
                }
            }
        }
    }
    [pscustomobject]@{ Before=$Before; After=$After; MaxChannelDifference=$maxDifference; ChangedPixels=$changedPixels; MaxPathDifference=$maxPathDifference }
    if ($maxDifference -gt 1) { throw 'Filter difference exceeds one byte level' }
    if ($maxPathDifference -gt 1) { throw 'Batch/instance/worker pictures differ' }
} finally {
    $beforeImage.Dispose()
    $afterImage.Dispose()
}
