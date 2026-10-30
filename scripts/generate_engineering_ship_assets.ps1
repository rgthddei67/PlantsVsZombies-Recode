#Requires -Version 7.0
param([switch]$UpdateLock)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path $PSScriptRoot -Parent
$assetRoot = Join-Path $PSScriptRoot 'assets'
$runtimeRoot = Join-Path $projectRoot 'build/clang-release/resources'
$sourcePath = Join-Path $assetRoot 'engineering_ship_parts_source.png'
$waterPath = Join-Path $assetRoot 'engineering_ship_water_source.png'
$damagePath = Join-Path $assetRoot 'engineering_ship_damaged_source.png'
$lockPath = Join-Path $assetRoot 'engineering_ship.sha256.json'

# ImageGen 母图只做 Alpha 裁边、等比缩放与分件打包；不重画或抹除素材内容。
$drawingReferences = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Rectangle].Assembly.Location, (Join-Path $PSHOME 'System.Runtime.dll'))
foreach($assemblyName in @('System.Private.Windows.GdiPlus.dll','System.Private.Windows.Core.dll')) {
    $assemblyPath = Join-Path $PSHOME $assemblyName
    if(Test-Path -LiteralPath $assemblyPath) { $drawingReferences += $assemblyPath }
}
Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
public static class EngineeringShipAssets {
    public static Rectangle Bounds(Bitmap image, Rectangle region) {
        int left=region.Right, top=region.Bottom, right=region.Left, bottom=region.Top;
        for(int y=region.Top;y<region.Bottom;y++) for(int x=region.Left;x<region.Right;x++) {
            if(image.GetPixel(x,y).A < 8) continue;
            left=Math.Min(left,x); top=Math.Min(top,y); right=Math.Max(right,x); bottom=Math.Max(bottom,y);
        }
        if(right<left || bottom<top) throw new InvalidOperationException("Empty sprite region");
        return Rectangle.FromLTRB(left,top,right+1,bottom+1);
    }
    public static void Export(Bitmap source, Rectangle region, int width, int height, string destination) {
        using(var bitmap=new Bitmap(width,height,PixelFormat.Format32bppArgb))
        using(var graphics=Graphics.FromImage(bitmap)) {
            graphics.CompositingMode=CompositingMode.SourceCopy;
            graphics.InterpolationMode=InterpolationMode.HighQualityBicubic;
            graphics.PixelOffsetMode=PixelOffsetMode.HighQuality;
            graphics.DrawImage(source,new Rectangle(0,0,width,height),region,GraphicsUnit.Pixel);
            bitmap.Save(destination,ImageFormat.Png);
        }
    }
}
'@

$source = [System.Drawing.Bitmap]::FromFile($sourcePath)
$water = [System.Drawing.Bitmap]::FromFile($waterPath)
$damage = [System.Drawing.Bitmap]::FromFile($damagePath)
$outputs = [System.Collections.Generic.List[string]]::new()
try {
    $parts = @(
        @{Name='body'; Region=[System.Drawing.Rectangle]::new(0,60,960,440); Width=340},
        @{Name='cannon'; Region=[System.Drawing.Rectangle]::new(968,70,568,430); Width=188},
        @{Name='captain'; Region=[System.Drawing.Rectangle]::new(180,510,490,470); Width=112},
        @{Name='hatch'; Region=[System.Drawing.Rectangle]::new(900,510,500,470); Width=74}
    )
    foreach($part in $parts) {
        $bounds = [EngineeringShipAssets]::Bounds($source,$part.Region)
        $height = [int][Math]::Round($bounds.Height*$part.Width/[double]$bounds.Width)
        $destination = Join-Path $runtimeRoot "image/reanim/Engineering_ship_$($part.Name).png"
        [EngineeringShipAssets]::Export($source,$bounds,$part.Width,$height,$destination)
        $outputs.Add($destination)
        Write-Output "$($part.Name): source=$bounds output=$($part.Width)x$height"
        if($part.Name -eq 'hatch') {
            $half=[int][Math]::Floor($bounds.Width/2)
            foreach($side in @('left','right')) {
                $slice=if($side -eq 'left') { [System.Drawing.Rectangle]::new($bounds.X,$bounds.Y,$half,$bounds.Height) } else { [System.Drawing.Rectangle]::new($bounds.X+$half,$bounds.Y,$bounds.Width-$half,$bounds.Height) }
                $destination=Join-Path $runtimeRoot "image/Engineering_ship_hatch_$side.png"
                [EngineeringShipAssets]::Export($source,$slice,37,$height,$destination)
                $outputs.Add($destination)
            }
        }
    }
    $waterBounds = [EngineeringShipAssets]::Bounds($water,[System.Drawing.Rectangle]::new(0,0,$water.Width,$water.Height))
    $destination = Join-Path $runtimeRoot 'image/Engineering_ship_waterjet.png'
    [EngineeringShipAssets]::Export($water,$waterBounds,600,200,$destination)
    $outputs.Add($destination)
    $destination = Join-Path $runtimeRoot 'image/Engineering_ship_splash.png'
    [EngineeringShipAssets]::Export($water,[System.Drawing.Rectangle]::new(0,0,760,$water.Height),192,184,$destination)
    $outputs.Add($destination)
    $destination = Join-Path $runtimeRoot 'image/Engineering_ship_body_damaged.png'
    [EngineeringShipAssets]::Export($damage,[System.Drawing.Rectangle]::new(0,0,$damage.Width,$damage.Height),340,142,$destination)
    $outputs.Add($destination)
} finally { $source.Dispose(); $water.Dispose(); $damage.Dispose() }

# 新时间轴只提供连续机械微动；技能和死亡均由正式玩法时钟控制，不注册新帧事件。
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$timeline = [System.Text.StringBuilder]::new()
[void]$timeline.AppendLine('<fps>12</fps>')
function Add-Track([string]$name,[string]$image,[double]$x,[double]$y,[double]$bob,[double]$phase) {
    [void]$timeline.AppendLine("<track><name>$name</name>")
    for($frame=0;$frame -lt 24;$frame++) {
        $wave = [Math]::Sin(2*[Math]::PI*$frame/24+$phase)
        $frameY = ($y+$bob*$wave).ToString('F4',$culture)
        $frameX = $x.ToString('F4',$culture)
        $payload = if($image) { "<i>$image</i><sx>0.5</sx><sy>0.5</sy>" } else { '' }
        [void]$timeline.AppendLine("<t><x>$frameX</x><y>$frameY</y><f>0</f>$payload</t>")
    }
    [void]$timeline.AppendLine('</track>')
}
Add-Track 'anim_idle' '' 0 0 0 0
Add-Track 'anim_walk' '' 0 0 0 0
Add-Track '_ground' '' 0 0 0 0
Add-Track 'anim_head1' 'IMAGE_REANIM_ENGINEERING_SHIP_CAPTAIN' 108 -2 0.7 0.2
Add-Track 'ship_cannon' 'IMAGE_REANIM_ENGINEERING_SHIP_CANNON' -3 6 0.35 0
Add-Track 'Zombie_body' 'IMAGE_REANIM_ENGINEERING_SHIP_BODY' 0 42 0.35 0
Add-Track 'ship_hatch' 'IMAGE_REANIM_ENGINEERING_SHIP_HATCH' 97 61 0.35 0
$reanimPath = Join-Path $runtimeRoot 'reanim/EngineeringShipZombie.reanim'
[System.IO.File]::WriteAllText($reanimPath,$timeline.ToString(),[System.Text.UTF8Encoding]::new($false))
$outputs.Add($reanimPath)
[xml]$reanim = '<root>' + [System.IO.File]::ReadAllText($reanimPath) + '</root>'
foreach($track in $reanim.root.track) { if($track.t.Count -ne 24) { throw 'Unequal reanim frame counts' } }

$hashes = [ordered]@{}
foreach($path in @($sourcePath,$waterPath,$damagePath)+$outputs.ToArray()) {
    $key = [System.IO.Path]::GetRelativePath($projectRoot,$path).Replace('\','/')
    $hashes[$key] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
if(!$UpdateLock -and (Test-Path -LiteralPath $lockPath)) {
    $locked = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json -AsHashtable
    foreach($entry in $hashes.GetEnumerator()) { if($locked[$entry.Key] -ne $entry.Value) { throw "Asset hash changed: $($entry.Key). Inspect before -UpdateLock." } }
} else {
    [System.IO.File]::WriteAllText($lockPath,($hashes | ConvertTo-Json),[System.Text.UTF8Encoding]::new($false))
}
