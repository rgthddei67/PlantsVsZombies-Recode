param(
    [string]$ResourceRoot = (Join-Path $PSScriptRoot "../build/clang-release/resources")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$imageRoot = Join-Path $ResourceRoot "image/reanim"

function Convert-ToIceBlue {
    param(
        [System.Drawing.Bitmap]$Source,
        [double]$DarkBlue,
        [double]$LightBlue
    )

    $target = [System.Drawing.Bitmap]::new(
        $Source.Width, $Source.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $Source.Height; ++$y) {
        for ($x = 0; $x -lt $Source.Width; ++$x) {
            $pixel = $Source.GetPixel($x, $y)
            if ($pixel.A -eq 0) { continue }
            $luma = (0.30 * $pixel.R + 0.58 * $pixel.G + 0.12 * $pixel.B) / 255.0
            $red = [Math]::Min(255, [int](18 + 76 * $luma))
            $green = [Math]::Min(255, [int](45 + 118 * $luma))
            $blue = [Math]::Min(255, [int]($DarkBlue + $LightBlue * $luma))
            $target.SetPixel($x, $y,
                [System.Drawing.Color]::FromArgb($pixel.A, $red, $green, $blue))
        }
    }
    return $target
}

# 安全帽已迁移到 generate_zombie_identity_assets.ps1；此入口只维护墙体及旧骨架参考件。

function New-EngineerBody {
    $source = [System.Drawing.Bitmap]::new((Join-Path $imageRoot "Zombie_body.png"))
    $target = Convert-ToIceBlue -Source $source -DarkBlue 72 -LightBlue 115
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($target)
        try {
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

            # 背负式制冰罐占据轮廓右侧，并用橙色卡箍把“工程兵”信息拉到低分辨率也能读出。
            $tankEdge = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 18, 58, 82), 2.0)
            $tankBody = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(250, 118, 211, 232))
            $tankLight = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(225, 224, 250, 255))
            $safety = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 255, 143, 22), 4.2)
            $reflective = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(245, 255, 226, 103), 1.5)
            $pouch = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(255, 92, 48, 20))
            try {
                $graphics.FillRectangle($tankBody, 36, 12, 14, 31)
                $graphics.FillEllipse($tankBody, 36, 8, 14, 9)
                $graphics.FillEllipse($tankBody, 36, 38, 14, 9)
                $graphics.FillRectangle($tankLight, 39, 13, 3, 28)
                $graphics.DrawRectangle($tankEdge, 36, 12, 14, 31)
                $graphics.DrawArc($tankEdge, 36, 8, 14, 9, 180, 180)
                $graphics.DrawArc($tankEdge, 36, 38, 14, 9, 0, 180)
                $graphics.DrawLine($safety, 35, 18, 50, 18)
                $graphics.DrawLine($safety, 35, 35, 50, 35)

                # 高可视反光背带和工具腰带直接附着在身体轨道，行走、啃食与死亡均自然跟随。
                $graphics.DrawLine($safety, 11, 12, 38, 52)
                $graphics.DrawLine($reflective, 11, 12, 38, 52)
                $graphics.DrawLine($safety, 8, 43, 40, 51)
                $graphics.DrawLine($reflective, 8, 43, 40, 51)
                $graphics.FillRectangle($pouch, 12, 44, 8, 9)
                $graphics.FillRectangle($pouch, 28, 48, 8, 8)
            }
            finally {
                $tankEdge.Dispose(); $tankBody.Dispose(); $tankLight.Dispose()
                $safety.Dispose(); $reflective.Dispose(); $pouch.Dispose()
            }
        }
        finally { $graphics.Dispose() }
        $target.Save((Join-Path $imageRoot "Zombie_icewall_engineer_body.png"),
            [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $target.Dispose()
        $source.Dispose()
    }
}

function New-EngineerToolStrap {
    $target = [System.Drawing.Bitmap]::new(
        17, 30, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($target)
        try {
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $dark = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 31, 58, 70), 4.5)
            $metal = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 177, 235, 244), 2.4)
            $handle = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 255, 139, 20), 4.0)
            $shine = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 255, 225, 96), 1.2)
            try {
                $graphics.DrawLine($dark, 5, 3, 11, 26)
                $graphics.DrawLine($handle, 5, 3, 11, 26)
                $graphics.DrawLine($shine, 5, 3, 11, 26)
                $graphics.DrawLine($dark, 1, 4, 13, 1)
                $graphics.DrawLine($metal, 1, 4, 13, 1)
                $graphics.DrawLine($metal, 11, 26, 15, 23)
            }
            finally { $dark.Dispose(); $metal.Dispose(); $handle.Dispose(); $shine.Dispose() }
        }
        finally { $graphics.Dispose() }
        $target.Save((Join-Path $imageRoot "Zombie_icewall_engineer_toolstrap.png"),
            [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally { $target.Dispose() }
}

function New-IceWall {
    param([int]$Stage)

    $scale = 4
    $large = [System.Drawing.Bitmap]::new(
        84 * $scale, 138 * $scale,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $g = [System.Drawing.Graphics]::FromImage($large)
        try {
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $g.ScaleTransform($scale, $scale)
            $outline = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(255, 25, 72, 112))
            $ice = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(245, 124, 211, 239))
            $light = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(220, 220, 250, 255))
            $shadow = [System.Drawing.SolidBrush]::new(
                [System.Drawing.Color]::FromArgb(215, 55, 145, 194))
            $edge = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(235, 225, 252, 255), 2.1)
            $crack = [System.Drawing.Pen]::new(
                [System.Drawing.Color]::FromArgb(255, 36, 91, 137), 2.3)
            try {
                $outer = [System.Drawing.PointF[]]@(
                    [System.Drawing.PointF]::new(5, 136), [System.Drawing.PointF]::new(7, 38),
                    [System.Drawing.PointF]::new(18, 13), [System.Drawing.PointF]::new(30, 29),
                    [System.Drawing.PointF]::new(43, 3), [System.Drawing.PointF]::new(57, 25),
                    [System.Drawing.PointF]::new(69, 10), [System.Drawing.PointF]::new(79, 39),
                    [System.Drawing.PointF]::new(80, 136))
                $inner = [System.Drawing.PointF[]]@(
                    [System.Drawing.PointF]::new(10, 132), [System.Drawing.PointF]::new(11, 41),
                    [System.Drawing.PointF]::new(19, 20), [System.Drawing.PointF]::new(31, 36),
                    [System.Drawing.PointF]::new(43, 11), [System.Drawing.PointF]::new(56, 33),
                    [System.Drawing.PointF]::new(67, 18), [System.Drawing.PointF]::new(74, 42),
                    [System.Drawing.PointF]::new(75, 132))
                $g.FillPolygon($outline, $outer)
                $g.FillPolygon($ice, $inner)
                $g.FillPolygon($light, [System.Drawing.PointF[]]@(
                    [System.Drawing.PointF]::new(14, 42), [System.Drawing.PointF]::new(20, 25),
                    [System.Drawing.PointF]::new(29, 39), [System.Drawing.PointF]::new(26, 123),
                    [System.Drawing.PointF]::new(14, 129)))
                $g.FillPolygon($shadow, [System.Drawing.PointF[]]@(
                    [System.Drawing.PointF]::new(58, 36), [System.Drawing.PointF]::new(67, 22),
                    [System.Drawing.PointF]::new(72, 44), [System.Drawing.PointF]::new(73, 130),
                    [System.Drawing.PointF]::new(57, 126)))
                $g.DrawLine($edge, 43, 13, 44, 124)
                $g.DrawLine($edge, 30, 38, 33, 119)
                $g.FillEllipse($outline, 2, 125, 80, 13)
                $g.FillEllipse($ice, 6, 124, 72, 10)

                if ($Stage -ge 2) {
                    $g.DrawLines($crack, [System.Drawing.PointF[]]@(
                        [System.Drawing.PointF]::new(48, 42), [System.Drawing.PointF]::new(39, 58),
                        [System.Drawing.PointF]::new(47, 70), [System.Drawing.PointF]::new(34, 87)))
                    $g.DrawLine($crack, 39, 58, 29, 53)
                }
                if ($Stage -ge 3) {
                    $g.DrawLines($crack, [System.Drawing.PointF[]]@(
                        [System.Drawing.PointF]::new(64, 58), [System.Drawing.PointF]::new(54, 75),
                        [System.Drawing.PointF]::new(63, 91), [System.Drawing.PointF]::new(49, 112)))
                    $g.DrawLines($crack, [System.Drawing.PointF[]]@(
                        [System.Drawing.PointF]::new(24, 69), [System.Drawing.PointF]::new(31, 82),
                        [System.Drawing.PointF]::new(20, 99), [System.Drawing.PointF]::new(26, 118)))
                    $g.FillPolygon($outline, [System.Drawing.PointF[]]@(
                        [System.Drawing.PointF]::new(43, 76), [System.Drawing.PointF]::new(50, 82),
                        [System.Drawing.PointF]::new(45, 91), [System.Drawing.PointF]::new(37, 86)))
                }
            }
            finally {
                $outline.Dispose(); $ice.Dispose(); $light.Dispose(); $shadow.Dispose()
                $edge.Dispose(); $crack.Dispose()
            }
        }
        finally { $g.Dispose() }

        $small = [System.Drawing.Bitmap]::new(
            84, 138, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $down = [System.Drawing.Graphics]::FromImage($small)
            try {
                $down.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $down.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $down.DrawImage($large, 0, 0, 84, 138)
            }
            finally { $down.Dispose() }
            $name = if ($Stage -eq 1) { "Ice_Wall.png" } elseif ($Stage -eq 2) {
                "Ice_Wall_cracked1.png" } else { "Ice_Wall_cracked2.png" }
            $small.Save((Join-Path $imageRoot $name), [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally { $small.Dispose() }
    }
    finally { $large.Dispose() }
}

New-EngineerBody
New-EngineerToolStrap
for ($stage = 1; $stage -le 3; ++$stage) { New-IceWall -Stage $stage }

$expectedHashes = @{
    "Zombie_icewall_engineer_body.png" = "8BE4257AB241F962238ABE5B4604052DAC2D33857F9270B007D1AD613DD65019"
    "Zombie_icewall_engineer_toolstrap.png" = "FCD684C2B7CDA4ACCEF60BEF9AC6D1B07D960BE3BA82CA08252808051780628D"
    "Ice_Wall.png" = "8832AC59261223480C6BEB8C05A7D83B5FA01F9D74D91577FB450D0E13894BD9"
    "Ice_Wall_cracked1.png" = "F656986FEA1E09D9EC7F3B3F3A617963CF47D47474B0569D42FB1BAC6A676A05"
    "Ice_Wall_cracked2.png" = "A3E7606E4547D21AB7D7D2BCFE7AD095E417DD18CFCDE9AF1A7115AF4447836C"
}
foreach ($entry in $expectedHashes.GetEnumerator()) {
    $path = Join-Path $imageRoot $entry.Key
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $entry.Value) {
        throw "Unexpected generated hash for $($entry.Key): $actual"
    }
}

Write-Output "Generated and verified Ice Wall Engineer assets in $ResourceRoot"
