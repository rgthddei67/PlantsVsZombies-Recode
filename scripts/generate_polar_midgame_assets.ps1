param(
    [string]$ResourceRoot = "build/clang-release/resources"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$resourcePath = [IO.Path]::GetFullPath((Join-Path $PWD $ResourceRoot))
$reanimImagePath = Join-Path $resourcePath "image/reanim"
$cardPath = Join-Path $resourcePath "image/PlantImage"
$reanimPath = Join-Path $resourcePath "reanim"
$motherPath = Join-Path $PSScriptRoot "assets/ThermalPulse_mother.png"
$utf8NoBom = [Text.UTF8Encoding]::new($false)

function Clamp-Byte([double]$value) {
    return [byte][Math]::Max(0, [Math]::Min(255, [Math]::Round($value)))
}

function Get-AggregateHash([string[]]$paths) {
    $lines = foreach ($path in ($paths | Sort-Object)) {
        $fullPath = [IO.Path]::GetFullPath($path)
        if (-not (Test-Path -LiteralPath $fullPath)) {
            throw "Missing asset in hash set: $fullPath"
        }
        "{0}|{1}" -f $fullPath.ToLowerInvariant(),
            (Get-FileHash -Algorithm SHA256 -LiteralPath $fullPath).Hash
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "")
    }
    finally { $sha.Dispose() }
}

function Convert-PalettePixel([Drawing.Color]$color, [string]$palette) {
    if ($color.A -eq 0) { return $color }
    $r = [double]$color.R
    $g = [double]$color.G
    $b = [double]$color.B
    $lum = 0.299 * $r + 0.587 * $g + 0.114 * $b

    # 原版墨线保持深色；中高光分别映射到北极星蓝紫和冰镜青蓝。
    if ($lum -lt 38) {
        return [Drawing.Color]::FromArgb($color.A,
            (Clamp-Byte (0.72 * $r)), (Clamp-Byte (0.82 * $g)),
            (Clamp-Byte ([Math]::Min(255, 0.98 * $b + 8))))
    }
    switch ($palette) {
        "northPetal" {
            return [Drawing.Color]::FromArgb($color.A,
                (Clamp-Byte (72 + 0.54 * $lum)),
                (Clamp-Byte (80 + 0.58 * $lum)),
                (Clamp-Byte (128 + 0.54 * $lum)))
        }
        "northFace" {
            return [Drawing.Color]::FromArgb($color.A,
                (Clamp-Byte (28 + 0.42 * $lum)),
                (Clamp-Byte (82 + 0.55 * $lum)),
                (Clamp-Byte (112 + 0.55 * $lum)))
        }
        "northLeaf" {
            return [Drawing.Color]::FromArgb($color.A,
                (Clamp-Byte (12 + 0.25 * $lum)),
                (Clamp-Byte (65 + 0.64 * $lum)),
                (Clamp-Byte (92 + 0.70 * $lum)))
        }
        "mirrorBody" {
            return [Drawing.Color]::FromArgb($color.A,
                (Clamp-Byte (54 + 0.46 * $lum)),
                (Clamp-Byte (106 + 0.55 * $lum)),
                (Clamp-Byte (138 + 0.50 * $lum)))
        }
        default {
            return [Drawing.Color]::FromArgb($color.A,
                (Clamp-Byte (24 + 0.30 * $lum)),
                (Clamp-Byte (82 + 0.58 * $lum)),
                (Clamp-Byte (116 + 0.62 * $lum)))
        }
    }
}

function Convert-ClassicImage(
    [string]$sourcePath,
    [string]$destinationPath,
    [string]$palette
) {
    $source = [Drawing.Bitmap]::FromFile($sourcePath)
    try {
        $output = [Drawing.Bitmap]::new($source.Width, $source.Height,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            for ($y = 0; $y -lt $source.Height; ++$y) {
                for ($x = 0; $x -lt $source.Width; ++$x) {
                    $output.SetPixel($x, $y,
                        (Convert-PalettePixel $source.GetPixel($x, $y) $palette))
                }
            }
            $output.Save($destinationPath, [Drawing.Imaging.ImageFormat]::Png)
        }
        finally { $output.Dispose() }
    }
    finally { $source.Dispose() }
}

function Get-ReanimImageIndex {
    $index = @{}
    foreach ($file in Get-ChildItem -LiteralPath $reanimImagePath -Filter "*.png") {
        $key = "IMAGE_REANIM_" + $file.BaseName.ToUpperInvariant()
        $index[$key] = $file.FullName
    }
    return $index
}

function Generate-RecoloredReanim(
    [string]$sourceName,
    [string]$destinationName,
    [scriptblock]$paletteSelector,
    [hashtable]$imageIndex,
    [System.Collections.Generic.List[string]]$outputs
) {
    $sourceReanim = Join-Path $reanimPath "$sourceName.reanim"
    $xml = [IO.File]::ReadAllText($sourceReanim)
    $keys = [regex]::Matches($xml, '<i>(IMAGE_REANIM_[^<]+)</i>') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    foreach ($key in $keys) {
        if (-not $imageIndex.ContainsKey($key)) {
            throw "Cannot resolve classic reanim image key: $key"
        }
        $source = $imageIndex[$key]
        $sourceStem = [IO.Path]::GetFileNameWithoutExtension($source)
        $separator = $sourceStem.IndexOf('_')
        $suffix = if ($separator -ge 0) {
            $sourceStem.Substring($separator + 1)
        } else { $sourceStem }
        $destinationStem = "${destinationName}_$suffix"
        $destination = Join-Path $reanimImagePath "$destinationStem.png"
        $palette = & $paletteSelector $key
        Convert-ClassicImage $source $destination $palette
        $outputs.Add($destination)
        $xml = $xml.Replace($key,
            "IMAGE_REANIM_" + $destinationStem.ToUpperInvariant())
    }
    $destinationReanim = Join-Path $reanimPath "$destinationName.reanim"
    [IO.File]::WriteAllText($destinationReanim, $xml, $utf8NoBom)
    $outputs.Add($destinationReanim)
}

function Generate-Card(
    [string]$sourceName,
    [string]$destinationName,
    [string]$palette,
    [string]$emblem,
    [System.Collections.Generic.List[string]]$outputs
) {
    $destination = Join-Path $cardPath "$destinationName.png"
    Convert-ClassicImage (Join-Path $cardPath "$sourceName.png") $destination $palette
    $bitmap = [Drawing.Bitmap]::FromFile($destination)
    try {
        $copy = [Drawing.Bitmap]::new($bitmap)
    }
    finally { $bitmap.Dispose() }
    try {
        $temporary = "$destination.tmp.png"
        try {
            $graphics = [Drawing.Graphics]::FromImage($copy)
            try {
                $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
                if ($emblem -eq "star") {
                    $points = [Drawing.PointF[]]@(
                        [Drawing.PointF]::new(64, 5), [Drawing.PointF]::new(68, 15),
                        [Drawing.PointF]::new(79, 15), [Drawing.PointF]::new(70, 22),
                        [Drawing.PointF]::new(73, 33), [Drawing.PointF]::new(64, 27),
                        [Drawing.PointF]::new(55, 33), [Drawing.PointF]::new(58, 22),
                        [Drawing.PointF]::new(49, 15), [Drawing.PointF]::new(60, 15))
                    $graphics.FillPolygon([Drawing.Brushes]::White, $points)
                    $graphics.DrawPolygon([Drawing.Pen]::new(
                        [Drawing.Color]::FromArgb(255, 74, 116, 215), 2.0), $points)
                }
                else {
                    $brush = [Drawing.SolidBrush]::new(
                        [Drawing.Color]::FromArgb(155, 168, 239, 255))
                    $pen = [Drawing.Pen]::new(
                        [Drawing.Color]::FromArgb(245, 235, 254, 255), 2.5)
                    try {
                        $graphics.FillEllipse($brush, 49, 5, 30, 38)
                        $graphics.DrawEllipse($pen, 49, 5, 30, 38)
                        $graphics.DrawLine($pen, 55, 33, 73, 13)
                    }
                    finally { $brush.Dispose(); $pen.Dispose() }
                }
            }
            finally { $graphics.Dispose() }
            $copy.Save($temporary, [Drawing.Imaging.ImageFormat]::Png)
            Move-Item -LiteralPath $temporary -Destination $destination -Force
        }
        finally {
            if (Test-Path -LiteralPath $temporary) {
                Remove-Item -LiteralPath $temporary -Force
            }
        }
    }
    finally { $copy.Dispose() }
    $outputs.Add($destination)
}

function Generate-ThermalPulse(
    [string]$sourcePath,
    [string]$destinationPath,
    [System.Collections.Generic.List[string]]$outputs
) {
    $source = [Drawing.Bitmap]::FromFile($sourcePath)
    try {
        $left = $source.Width
        $top = $source.Height
        $right = -1
        $bottom = -1
        for ($y = 0; $y -lt $source.Height; ++$y) {
            for ($x = 0; $x -lt $source.Width; ++$x) {
                $pixel = $source.GetPixel($x, $y)
                $strength = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
                if ($strength -gt 18) {
                    $left = [Math]::Min($left, $x)
                    $top = [Math]::Min($top, $y)
                    $right = [Math]::Max($right, $x)
                    $bottom = [Math]::Max($bottom, $y)
                }
            }
        }
        if ($right -lt $left -or $bottom -lt $top) { throw "Thermal pulse mother art is empty" }
        $crop = [Drawing.Bitmap]::new($right - $left + 1, $bottom - $top + 1,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            for ($y = 0; $y -lt $crop.Height; ++$y) {
                for ($x = 0; $x -lt $crop.Width; ++$x) {
                    $pixel = $source.GetPixel($left + $x, $top + $y)
                    $strength = [Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B))
                    $alpha = Clamp-Byte ([Math]::Max(0, ($strength - 8) * 1.45))
                    $crop.SetPixel($x, $y, [Drawing.Color]::FromArgb(
                        $alpha, $pixel.R, $pixel.G, $pixel.B))
                }
            }
            $output = [Drawing.Bitmap]::new(80, 42,
                [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                $graphics = [Drawing.Graphics]::FromImage($output)
                try {
                    $graphics.InterpolationMode =
                        [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $scale = [Math]::Min(76.0 / $crop.Width, 38.0 / $crop.Height)
                    $drawWidth = [int][Math]::Round($crop.Width * $scale)
                    $drawHeight = [int][Math]::Round($crop.Height * $scale)
                    $graphics.DrawImage($crop, [int]((80 - $drawWidth) / 2),
                        [int]((42 - $drawHeight) / 2), $drawWidth, $drawHeight)
                }
                finally { $graphics.Dispose() }
                $output.Save($destinationPath, [Drawing.Imaging.ImageFormat]::Png)
            }
            finally { $output.Dispose() }
        }
        finally { $crop.Dispose() }
    }
    finally { $source.Dispose() }
    $outputs.Add($destinationPath)
}

$imageIndex = Get-ReanimImageIndex
$outputs = [System.Collections.Generic.List[string]]::new()
$northSelector = {
    param($key)
    if ($key -match 'SUNFLOWER_.*PETAL') { return "northPetal" }
    if ($key -match 'SUNFLOWER_(HEAD|BLINK)') { return "northFace" }
    return "northLeaf"
}
$mirrorSelector = {
    param($key)
    if ($key -match 'PLANTERN_(BODY|EYES)') { return "mirrorBody" }
    return "mirrorLeaf"
}

Generate-RecoloredReanim "SunFlower" "NorthStarFlower" $northSelector $imageIndex $outputs
Generate-RecoloredReanim "Plantern" "IceMirrorGrass" $mirrorSelector $imageIndex $outputs
Generate-Card "SunFlower" "NorthStarFlower" "northPetal" "star" $outputs
Generate-Card "Plantern" "IceMirrorGrass" "mirrorBody" "mirror" $outputs
Generate-ThermalPulse $motherPath (Join-Path $resourcePath "image/ProjectileThermalPulse.png") $outputs
# 热感狙击装备由 generate_zombie_equipment_polish.py 导出。

$sourceReanims = @(
    (Join-Path $reanimPath "SunFlower.reanim"),
    (Join-Path $reanimPath "Plantern.reanim"),
    (Join-Path $cardPath "SunFlower.png"),
    (Join-Path $cardPath "Plantern.png"),
    $motherPath
)
$sourceAggregate = Get-AggregateHash $sourceReanims
$outputAggregate = Get-AggregateHash $outputs.ToArray()
$expectedSourceAggregate = "DE26626721FB8F0B714483DEE5529735E4CA4547690D64AD2BD35D494477AD92"
$expectedOutputAggregate = "EB2B9A4480EA0DFFF622A8F87139EBB8761B7C157E506CBDD1A1B0A106E13568"
if ($expectedSourceAggregate -ne "TO_BE_FILLED" -and
    $sourceAggregate -ne $expectedSourceAggregate) {
    throw "Polar midgame source assets drifted: expected=$expectedSourceAggregate actual=$sourceAggregate"
}
if ($expectedOutputAggregate -ne "TO_BE_FILLED" -and
    $outputAggregate -ne $expectedOutputAggregate) {
    throw "Polar midgame generated assets drifted: expected=$expectedOutputAggregate actual=$outputAggregate"
}

Write-Output "sourceAggregate=$sourceAggregate"
Write-Output "outputAggregate=$outputAggregate"
Write-Output "Generated animated plant recolors, distinct cards and thermal pulse."
