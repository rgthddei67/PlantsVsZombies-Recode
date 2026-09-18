# 将已审核的 ImageGen 原画按现有平地几何导出；不移动 Board/Cell 配合图片。
Add-Type -AssemblyName System.Drawing
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskAssets = Join-Path $taskRoot 'build/clang-release/resources/image'
$taskSource = [System.Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'assets/cold_storage_imagegen.png'))
$taskOutput = New-Object System.Drawing.Bitmap 1880, 720
$taskGraphics = [System.Drawing.Graphics]::FromImage($taskOutput)
$taskGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
# 原画自然土地区域与原版平地的可玩区逐段对应，外围建筑独立保留体量。
$taskSourceX = @(0, 500, 1330, 2027)
$taskSourceY = @(0, 132, 670, 776)
$taskTargetX = @(0, 492, 1212, 1880)
$taskTargetY = @(0, 138, 638, 720)
for ($taskRow = 0; $taskRow -lt 3; $taskRow++) {
    for ($taskColumn = 0; $taskColumn -lt 3; $taskColumn++) {
        $taskDestination = [System.Drawing.Rectangle]::new($taskTargetX[$taskColumn], $taskTargetY[$taskRow], ($taskTargetX[$taskColumn+1]-$taskTargetX[$taskColumn]), ($taskTargetY[$taskRow+1]-$taskTargetY[$taskRow]))
        $taskGraphics.DrawImage($taskSource, $taskDestination, $taskSourceX[$taskColumn], $taskSourceY[$taskRow], ($taskSourceX[$taskColumn+1]-$taskSourceX[$taskColumn]), ($taskSourceY[$taskRow+1]-$taskSourceY[$taskRow]), [System.Drawing.GraphicsUnit]::Pixel)
    }
}
$taskOutput.Save((Join-Path $taskAssets 'background_hot_cold_storage.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$taskGraphics.Dispose(); $taskOutput.Dispose(); $taskSource.Dispose()
$taskIcon = [System.Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'assets/cold_storage_ice_icon_imagegen.png'))
$taskOutput = New-Object System.Drawing.Bitmap 28, 32
$taskGraphics = [System.Drawing.Graphics]::FromImage($taskOutput)
$taskGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$taskGraphics.DrawImage($taskIcon, [System.Drawing.Rectangle]::new(0,0,28,32), 320,260,630,780,[System.Drawing.GraphicsUnit]::Pixel)
$taskOutput.Save((Join-Path $taskAssets 'cold_storage_ice_head.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$taskGraphics.Dispose(); $taskOutput.Dispose(); $taskIcon.Dispose()
