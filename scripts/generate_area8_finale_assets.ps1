Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
$resourceRoot = Join-Path $repoRoot 'build/clang-release/resources'
$reanimDir = Join-Path $resourceRoot 'image/reanim'
$plantDir = Join-Path $resourceRoot 'image/PlantImage'
$particleDir = Join-Path $resourceRoot 'particles'

function New-Canvas([int]$width, [int]$height) {
    return [Drawing.Bitmap]::new($width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
}

function New-Graphics([Drawing.Bitmap]$bitmap) {
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    return $graphics
}

function Save-Png([Drawing.Bitmap]$bitmap, [string]$path) {
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function Draw-OutlinedPolygon($graphics, $points, $fill, $outline, [float]$width) {
    [Drawing.PointF[]]$typedPoints = $points
    $brush = [Drawing.SolidBrush]::new($fill)
    $pen = [Drawing.Pen]::new($outline, $width)
    $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    $graphics.FillPolygon($brush, $typedPoints)
    $graphics.DrawPolygon($pen, $typedPoints)
    $brush.Dispose()
    $pen.Dispose()
}

function Draw-Star($graphics, [float]$cx, [float]$cy, [float]$outer, [float]$inner, [Drawing.Color]$fill) {
    $points = New-Object Drawing.PointF[] 10
    for ($i = 0; $i -lt 10; $i++) {
        $radius = if (($i % 2) -eq 0) { $outer } else { $inner }
        $angle = -[Math]::PI / 2 + $i * [Math]::PI / 5
        $points[$i] = [Drawing.PointF]::new($cx + [Math]::Cos($angle) * $radius, $cy + [Math]::Sin($angle) * $radius)
    }
    Draw-OutlinedPolygon $graphics $points $fill ([Drawing.Color]::FromArgb(230, 30, 32, 65)) 3
}

# 仪器和星盘由 generate_zombie_identity_assets.ps1 生成；此处保留能力挂件、植物与光效。

# 胸前棱镜。
$bitmap = New-Canvas 66 58; $graphics = New-Graphics $bitmap
$prism = @([Drawing.PointF]::new(33,3),[Drawing.PointF]::new(62,52),[Drawing.PointF]::new(5,52))
Draw-OutlinedPolygon $graphics $prism ([Drawing.Color]::FromArgb(235,185,255,255)) ([Drawing.Color]::FromArgb(255,22,34,72)) 4
$graphics.DrawLine([Drawing.Pen]::new([Drawing.Color]::FromArgb(230,255,100,220),4),33,6,33,50)
$graphics.DrawLine([Drawing.Pen]::new([Drawing.Color]::FromArgb(230,90,235,255),4),33,27,8,50)
$graphics.Dispose(); Save-Png $bitmap (Join-Path $reanimDir 'Zombie_aurora_prism.png')

# 悬摆。
$bitmap = New-Canvas 48 92; $graphics = New-Graphics $bitmap
$graphics.DrawLine([Drawing.Pen]::new([Drawing.Color]::FromArgb(255,225,177,80),6),24,2,24,61)
$graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,41,215,238)),6,54,36,34)
$graphics.DrawEllipse([Drawing.Pen]::new([Drawing.Color]::FromArgb(255,245,205,98),4),6,54,36,34)
Draw-Star $graphics 24 70 10 4 ([Drawing.Color]::FromArgb(255,255,240,181))
$graphics.Dispose(); Save-Png $bitmap (Join-Path $reanimDir 'Zombie_polar_pendulum.png')

# 界碑花身份件：破晓石碑、三层晶棱与两枚可视碎片。
$bitmap = New-Canvas 94 104; $graphics = New-Graphics $bitmap
$stone = @([Drawing.PointF]::new(47,3),[Drawing.PointF]::new(82,25),[Drawing.PointF]::new(75,91),[Drawing.PointF]::new(19,91),[Drawing.PointF]::new(12,25))
Draw-OutlinedPolygon $graphics $stone ([Drawing.Color]::FromArgb(245,54,75,105)) ([Drawing.Color]::FromArgb(255,12,24,45)) 5
$inner = @([Drawing.PointF]::new(47,14),[Drawing.PointF]::new(69,31),[Drawing.PointF]::new(62,76),[Drawing.PointF]::new(31,76),[Drawing.PointF]::new(24,31))
Draw-OutlinedPolygon $graphics $inner ([Drawing.Color]::FromArgb(235,75,224,238)) ([Drawing.Color]::FromArgb(255,204,252,255)) 3
Draw-Star $graphics 47 45 17 7 ([Drawing.Color]::FromArgb(255,255,211,95))
$graphics.FillPolygon([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,120,244,255)), @([Drawing.PointF]::new(9,73),[Drawing.PointF]::new(22,60),[Drawing.PointF]::new(29,84)))
$graphics.FillPolygon([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,255,151,223)), @([Drawing.PointF]::new(85,73),[Drawing.PointF]::new(72,60),[Drawing.PointF]::new(65,84)))
$graphics.Dispose(); Save-Png $bitmap (Join-Path $reanimDir 'REANIM_BOUNDARYFLOWER_MONUMENT.png')

# 曙光莲花冠：分层花瓣、暖核与冷色外缘。
$bitmap = New-Canvas 112 104; $graphics = New-Graphics $bitmap
for ($i=0; $i -lt 12; $i++) {
    $angle = $i * 30
    $graphics.TranslateTransform(56,58); $graphics.RotateTransform($angle)
    $graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(225,88,222,255)),-12,-49,24,52)
    $graphics.ResetTransform()
}
for ($i=0; $i -lt 8; $i++) {
    $angle = $i * 45 + 22.5
    $graphics.TranslateTransform(56,58); $graphics.RotateTransform($angle)
    $graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(238,255,130,216)),-11,-35,22,39)
    $graphics.ResetTransform()
}
$graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,255,225,89)),34,36,44,44)
$graphics.DrawEllipse([Drawing.Pen]::new([Drawing.Color]::FromArgb(255,255,250,208),5),36,38,40,40)
Draw-Star $graphics 56 58 17 8 ([Drawing.Color]::FromArgb(255,255,248,195))
$graphics.Dispose(); Save-Png $bitmap (Join-Path $reanimDir 'REANIM_DAWNLOTUS_CROWN.png')

# 卡面以原版金盏花/睡莲立绘为基底，再叠加身份件；保留经典构图。
foreach ($entry in @(
    @{ Base='Marigold.png'; Overlay='REANIM_BOUNDARYFLOWER_MONUMENT.png'; Out='BoundaryFlower.png'; X=36; Y=8; W=58; H=68 },
    @{ Base='LilyPad.png'; Overlay='REANIM_DAWNLOTUS_CROWN.png'; Out='DawnLotus.png'; X=24; Y=10; W=78; H=72 }
)) {
    $base = [Drawing.Bitmap]::new((Join-Path $plantDir $entry.Base))
    $overlay = [Drawing.Image]::FromFile((Join-Path $reanimDir $entry.Overlay))
    $card = New-Canvas 120 120; $graphics = New-Graphics $card
    $graphics.DrawImage($base, 0, 0, 120, 120)
    $graphics.DrawImage($overlay, $entry.X, $entry.Y, $entry.W, $entry.H)
    $graphics.Dispose(); $base.Dispose(); $overlay.Dispose()
    Save-Png $card (Join-Path $plantDir $entry.Out)
}

# 四张粒子纹理承载裂隙、时间齿轮、界碑碎片与黎明花爆。
foreach ($particle in @(
    @{Name='AuroraRift.png'; Kind='rift'}, @{Name='TemporalGear.png'; Kind='gear'},
    @{Name='BoundaryShard.png'; Kind='shard'}, @{Name='DawnFlare.png'; Kind='dawn'}
)) {
    $bitmap = New-Canvas 96 96; $graphics = New-Graphics $bitmap
    if ($particle.Kind -eq 'gear') {
        $graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(220,52,44,99)),10,10,76,76)
        $graphics.DrawEllipse([Drawing.Pen]::new([Drawing.Color]::FromArgb(255,239,191,86),7),12,12,72,72)
        Draw-Star $graphics 48 48 24 13 ([Drawing.Color]::FromArgb(255,90,225,255))
    } elseif ($particle.Kind -eq 'shard') {
        Draw-OutlinedPolygon $graphics @([Drawing.PointF]::new(48,4),[Drawing.PointF]::new(82,73),[Drawing.PointF]::new(48,92),[Drawing.PointF]::new(14,73)) ([Drawing.Color]::FromArgb(235,85,234,255)) ([Drawing.Color]::FromArgb(255,255,221,104)) 5
    } elseif ($particle.Kind -eq 'dawn') {
        Draw-Star $graphics 48 48 43 16 ([Drawing.Color]::FromArgb(245,255,222,93))
        Draw-Star $graphics 48 48 24 10 ([Drawing.Color]::FromArgb(255,255,122,218))
    } else {
        Draw-OutlinedPolygon $graphics @([Drawing.PointF]::new(48,2),[Drawing.PointF]::new(77,25),[Drawing.PointF]::new(91,48),[Drawing.PointF]::new(70,74),[Drawing.PointF]::new(48,94),[Drawing.PointF]::new(25,73),[Drawing.PointF]::new(5,48),[Drawing.PointF]::new(22,23)) ([Drawing.Color]::FromArgb(205,63,226,255)) ([Drawing.Color]::FromArgb(255,255,104,226)) 5
        $graphics.FillEllipse([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(230,12,18,48)),28,17,40,62)
    }
    $graphics.Dispose(); Save-Png $bitmap (Join-Path $particleDir $particle.Name)
}
