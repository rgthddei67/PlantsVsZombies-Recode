param([switch]$UpdateLock)

# 六种普通骨架变体的服装、头饰和离体部件；保持原始关节、透明边缘及时间轴。
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$res = Join-Path $root 'build/clang-release/resources'
$art = Join-Path $res 'image/reanim'
$inputs = [Collections.Generic.HashSet[string]]::new()
$outputs = [Collections.Generic.List[string]]::new()

function Read-Bitmap([string]$path) {
    [void]$inputs.Add($path)
    return [Drawing.Bitmap]::new($path)
}
function Save-Bitmap($bitmap, [string]$path) {
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $outputs.Add($path)
}
function New-Graphics($bitmap) {
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.Clear([Drawing.Color]::Transparent)
    $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    return $g
}
function Convert-Fabric($bitmap, [int[]]$color, [string]$mode, [int[]]$trim) {
    for ($y=0; $y -lt $bitmap.Height; $y++) {
        for ($x=0; $x -lt $bitmap.Width; $x++) {
            $p = $bitmap.GetPixel($x,$y)
            if ($p.A -eq 0) { continue }
            $v = [Math]::Max($p.R,[Math]::Max($p.G,$p.B))
            $brown = $p.R -gt $p.G*1.08 -and $p.R -gt $p.B*1.2
            $blue = $p.B -gt $p.R*1.08
            $fabric = ($mode -eq 'jacket' -and $brown) -or ($mode -eq 'pants' -and $blue) -or $mode -eq 'tie'
            if (!$fabric -or $v -le 48) { continue }
            $target = $color
            # 近肩的整段袖口是辅助识别带，仍保留原片 Alpha 和暗描边。
            if ($trim -and $mode -eq 'jacket' -and $bitmap.Width -le 24 -and $y -ge 5 -and $y -le 11) { $target=$trim }
            $light = [Math]::Min(1.15, 0.35 + $v/180.0)
            $rgb = @($target | ForEach-Object { [int][Math]::Min(255,[Math]::Round($_*$light)) })
            $bitmap.SetPixel($x,$y,[Drawing.Color]::FromArgb($p.A,$rgb[0],$rgb[1],$rgb[2]))
        }
    }
}
function Export-Headgear([string]$source, [string]$stem, [int]$width, [int]$height) {
    $im=Read-Bitmap (Join-Path $PSScriptRoot "assets/zombie_identity_${source}_source.png")
    $minX=$im.Width; $minY=$im.Height; $maxX=0; $maxY=0
    for($y=0;$y -lt $im.Height;$y++) { for($x=0;$x -lt $im.Width;$x++) {
        if($im.GetPixel($x,$y).A -ge 160) { $minX=[Math]::Min($minX,$x);$minY=[Math]::Min($minY,$y);$maxX=[Math]::Max($maxX,$x);$maxY=[Math]::Max($maxY,$y) }
    } }
    if($maxX -le $minX) { throw "Empty generated headgear: $source" }
    $out=[Drawing.Bitmap]::new($width,$height); $g=New-Graphics $out
    $g.DrawImage($im,[Drawing.Rectangle]::new(0,0,$width,$height),[Drawing.Rectangle]::new($minX,$minY,$maxX-$minX+1,$maxY-$minY+1),[Drawing.GraphicsUnit]::Pixel)
    $g.Dispose();$im.Dispose()
    Save-Bitmap $out (Join-Path $art "Identity_${stem}_headgear.png")
    return $out
}

# 配色为 RGB；headgear 尺寸/偏移是头轨道贴图空间的像素，与 ZombieIdentityVisuals.cpp 一致。
$styles=@(
    @{Stem='worker'; Rig='NormalZombie'; Name='IceWorkerZombie'; Coat=@(58,110,190); Pants=@(45,70,110); Trim=@(224,226,211); Hat=@(56,35,0,-18)},
    @{Stem='boiler'; Rig='NormalZombie'; Name='BoilerZombie'; Coat=@(215,82,35); Pants=@(66,62,60); Trim=@(231,175,58); Hat=@(52,20,-2,-4)},
    @{Stem='jammer'; Rig='BucketZombie'; Name='WeatherJammerZombie'; Coat=@(137,66,177); Pants=@(72,57,104); Trim=@(238,204,81)},
    @{Stem='healer'; Rig='NormalZombie'; Name='HealerZombie'; Coat=@(224,225,207); Pants=@(65,104,81); Trim=@(47,137,78); Hat=@(60,38,-8,-24)},
    @{Stem='sludge'; Rig='ConeZombie'; Name='SludgeZombie'; Coat=@(163,177,48); Pants=@(66,83,43); Trim=@(230,194,50)},
    @{Stem='guard'; Rig='NormalZombie'; Name='PressureGuardZombie'; Coat=@(35,130,140); Pants=@(40,70,91); Trim=@(199,217,212)}
)
$clothes=@('body','innerarm_upper','innerarm_lower','outerarm_upper','outerarm_upper2','outerarm_lower','innerleg_upper','innerleg_lower','outerleg_upper','outerleg_lower','tie')
foreach($s in $styles) {
    $stem=$s.Stem
    $rigPath=Join-Path $res "reanim/$($s.Rig).reanim"; [void]$inputs.Add($rigPath)
    $rig=[IO.File]::ReadAllText($rigPath).Replace("`r`n","`n")
    foreach($part in $clothes) {
        $im=Read-Bitmap (Join-Path $art "Zombie_${part}.png")
        $mode=if($part -match 'leg') {'pants'} elseif($part -eq 'tie') {'tie'} else {'jacket'}
        $color=if($mode -eq 'pants') {$s.Pants} elseif($mode -eq 'tie') {$s.Trim} else {$s.Coat}
        Convert-Fabric $im $color $mode $s.Trim
        Save-Bitmap $im (Join-Path $art "Identity_${stem}_${part}.png")
        $im.Dispose()
        $rig=$rig.Replace("<i>IMAGE_REANIM_ZOMBIE_$($part.ToUpperInvariant())</i>","<i>IMAGE_REANIM_IDENTITY_$($stem.ToUpperInvariant())_$($part.ToUpperInvariant())</i>")
    }
    $outRig=Join-Path $res "reanim/$($s.Name)Identity.reanim"
    [IO.File]::WriteAllText($outRig,$rig,[Text.UTF8Encoding]::new($false)); $outputs.Add($outRig)
    $hat=$null
    if($s.Hat) { $hat=Export-Headgear $stem $stem $s.Hat[0] $s.Hat[1] }

    # 头粒子统一画布 80x90，头轨原点在 (12,30)，中心相对头轨为 (28,15)。
    $head=[Drawing.Bitmap]::new(80,90);$g=New-Graphics $head
    $im=Read-Bitmap (Join-Path $art 'Zombie_head.png');$g.DrawImageUnscaled($im,12,30);$im.Dispose()
    $im=Read-Bitmap (Join-Path $art 'Zombie_jaw.png');$g.DrawImageUnscaled($im,21,70);$im.Dispose()
    if($hat) { $g.DrawImageUnscaled($hat,12+$s.Hat[2],30+$s.Hat[3]);$hat.Dispose() }
    else { $im=Read-Bitmap (Join-Path $art 'Zombie_hair.png');$g.DrawImageUnscaled($im,7,27);$im.Dispose() }
    $g.Dispose()
    # 下臂与手掌按普通骨架基准相对位置拼合，画布中心相对下臂轨为 (9,24.5)。
    $arm=[Drawing.Bitmap]::new(32,55);$g=New-Graphics $arm
    $im=[Drawing.Bitmap]::new((Join-Path $art "Identity_${stem}_outerarm_lower.png"));$g.DrawImageUnscaled($im,7,3);$im.Dispose()
    $im=Read-Bitmap (Join-Path $art 'Zombie_outerarm_hand.png');$g.DrawImageUnscaled($im,2,24);$im.Dispose();$g.Dispose()
    foreach($part in @('head','arm')) {
        $im=if($part -eq 'head') {$head} else {$arm}
        foreach($side in @('','_right')) {
            if($side) { $im.RotateFlip([Drawing.RotateFlipType]::RotateNoneFlipX) }
            $name="Identity_${stem}_${part}${side}"
            Save-Bitmap $im (Join-Path $res "particles/$name.png")
            $duration=if($part -eq 'head') {'1.0'} else {'0.8'}
            $speed=if($part -eq 'head') {'[35 65]'} else {'0'}
            $xml=@"
<Emitter>
  <Name>$name</Name>
  <SpawnMinActive>1</SpawnMinActive>
  <SpawnMaxLaunched>1</SpawnMaxLaunched>
  <ParticleDuration>$duration</ParticleDuration>
  <ParticleAlpha>1,80 0</ParticleAlpha>
  <ParticleScale>1</ParticleScale>
  <ParticleRotation>0</ParticleRotation>
  <LaunchSpeed>$speed</LaunchSpeed>
  <RandomLaunchSpin>1</RandomLaunchSpin>
  <ParticleGravity>140</ParticleGravity>
  <ParticleSpinSpeed>[-35 35]</ParticleSpinSpeed>
  <SystemDuration>1.1</SystemDuration>
  <Image>PARTICLE_$($name.ToUpperInvariant())</Image>
</Emitter>
"@
            $path=Join-Path $res "particles/config/$name.xml"
            [IO.File]::WriteAllText($path,$xml.Replace("`r`n","`n")+"`n",[Text.UTF8Encoding]::new($false));$outputs.Add($path)
        }
        $im.Dispose()
    }
}
# 共用铁桶/路障脱落路径需要保留魅惑镜像；原图不变，只生成右向离体副本。
foreach($name in @('Zombie_bucket3','FastZombie_bucket3','Zombie_cone3','Zombie_grounding_cone3','Zombie_icewall_engineer_hat3')) {
    $im=Read-Bitmap (Join-Path $art "$name.png")
    $im.RotateFlip([Drawing.RotateFlipType]::RotateNoneFlipX)
    Save-Bitmap $im (Join-Path $art "${name}_mirrored.png")
    $im.Dispose()
}
$hashes=[ordered]@{}
foreach($p in (@($inputs)+@($outputs)|Sort-Object -Unique)) {
    $relative=[IO.Path]::GetRelativePath($root,$p).Replace('\','/')
    # Git 的 autocrlf 不应改变美术内容哈希；文本统一 LF，PNG 保持原始字节。
    $bytes=if([IO.Path]::GetExtension($p) -in @('.reanim','.xml')) {
        [Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($p).Replace("`r`n","`n"))
    } else { [IO.File]::ReadAllBytes($p) }
    $hashes[$relative]=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}
$json=ConvertTo-Json $hashes -Depth 3
$lock=Join-Path $PSScriptRoot 'generate_zombie_identity_assets.sha256.json'
if((Test-Path $lock) -and !$UpdateLock -and [IO.File]::ReadAllText($lock).Trim() -ne $json.Trim()) { throw 'Identity art hash drift; review source/output before -UpdateLock' }
[IO.File]::WriteAllText($lock,$json+"`n",[Text.UTF8Encoding]::new($false))
Write-Output "Exported $($outputs.Count) identity assets; original timeline transforms and event frames preserved."
