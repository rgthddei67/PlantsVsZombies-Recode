param([switch]$UpdateLock)

# 普通骨架变体的服装、头饰和离体部件；保持原始关节、透明边缘及时间轴。
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
            $fabric = ($mode -eq 'jacket' -and $brown) -or ($mode -eq 'pants' -and $blue) -or $mode -in @('tie','rubber')
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
    if ($source -in @('coldguard','disaster','insulator','clockmaker','priest','drummer')) {
        $out = Export-AtlasCell $source 0 2 $width $height
        Save-Bitmap $out (Join-Path $art "Identity_${stem}_headgear.png")
        return $out
    }
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

# 按最大不透明连通部件取边界，排除母图透明区中孤立的生成杂点；保留边界内原始 Alpha。
Add-Type -ReferencedAssemblies System.Drawing.Common,System.Drawing.Primitives -TypeDefinition @'
using System;
using System.Drawing;
public static class IdentityAtlasBounds {
    public static Rectangle Find(Bitmap image, int index, int columns) {
        int left=image.Width*index/columns, right=image.Width*(index+1)/columns;
        int w=right-left, h=image.Height, best=0; var seen=new bool[w*h]; var queue=new int[w*h];
        Rectangle bounds=Rectangle.Empty;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int start=y*w+x;
            if(seen[start] || image.GetPixel(left+x,y).A<160) continue;
            int read=0,count=1,minX=x,maxX=x,minY=y,maxY=y; queue[0]=start; seen[start]=true;
            while(read<count) {
                int p=queue[read++], px=p%w, py=p/w;
                minX=Math.Min(minX,px);maxX=Math.Max(maxX,px);minY=Math.Min(minY,py);maxY=Math.Max(maxY,py);
                foreach(int n in new int[]{px>0?p-1:-1,px+1<w?p+1:-1,py>0?p-w:-1,py+1<h?p+w:-1}) {
                    if(n<0 || seen[n]) continue;
                    seen[n]=true;
                    if(image.GetPixel(left+n%w,n/w).A>=160) queue[count++]=n;
                }
            }
            if(count>best) {best=count;bounds=Rectangle.FromLTRB(left+minX,minY,left+maxX+1,maxY+1);}
        }
        if(best<100) throw new InvalidOperationException("Empty sprite atlas cell");
        return bounds;
    }
}
'@
function Export-AtlasCell([string]$source, [int]$index, [int]$columns, [int]$width, [int]$height) {
    $im=Read-Bitmap (Join-Path $PSScriptRoot "assets/zombie_identity_${source}_source.png")
    $bounds=[IdentityAtlasBounds]::Find($im,$index,$columns)
    $out=[Drawing.Bitmap]::new($width,$height);$g=New-Graphics $out
    $g.DrawImage($im,[Drawing.Rectangle]::new(0,0,$width,$height),$bounds,[Drawing.GraphicsUnit]::Pixel)
    $g.Dispose();$im.Dispose();return $out
}

# 三档防具保持旧画布与安装基准；破损只改变材质，绝不改变碰撞或承伤规则。
$equipment=@(
    @{Source='coldguard_damage'; Names=@('ColdChain_shield','ColdChain_shield_cracked1','ColdChain_shield_cracked2'); Canvas=@(60,82); Rect=@(0,0,60,82)},
    @{Source='insulator_damage'; Names=@('Zombie_insulator_armor1','Zombie_insulator_armor2','Zombie_insulator_armor3'); Canvas=@(72,72); Rect=@(5,5,59,63)},
    @{Source='grounding_damage'; Names=@('Zombie_grounding_cone1','Zombie_grounding_cone2','Zombie_grounding_cone3'); Canvas=@(59,57); Rect=@(0,0,53,56)},
    @{Source='iceengineer_damage'; Names=@('Zombie_icewall_engineer_hat1','Zombie_icewall_engineer_hat2','Zombie_icewall_engineer_hat3'); Canvas=@(59,57); Rect=@(0,14,48,29)}
)
foreach($e in $equipment) { for($stage=0;$stage -lt 3;$stage++) {
    $piece=Export-AtlasCell $e.Source $stage 3 $e.Rect[2] $e.Rect[3]
    $out=[Drawing.Bitmap]::new($e.Canvas[0],$e.Canvas[1]);$g=New-Graphics $out
    $g.DrawImageUnscaled($piece,$e.Rect[0],$e.Rect[1]);$g.Dispose();$piece.Dispose()
    Save-Bitmap $out (Join-Path $art "$($e.Names[$stage]).png");$out.Dispose()
} }
foreach($e in @(
    @{Source='clockmaker';Name='Zombie_polar_clock_disk';Size=@(110,110)},
    @{Source='priest';Name='Zombie_aurora_device';Size=@(104,104)},
    @{Source='drummer';Name='CrystalDrummer_drum';Size=@(51,55)},
    @{Source='grounding';Name='Identity_grounding_spool';Size=@(29,24)},
    @{Source='iceengineer';Name='Identity_iceengineer_harness';Size=@(42,51)}
)) {
    $im=Export-AtlasCell $e.Source 1 2 $e.Size[0] $e.Size[1]
    Save-Bitmap $im (Join-Path $art "$($e.Name).png");$im.Dispose()
}
# 冷却罐的液位是既有五阶段状态；仅压暗窗口上部，外壳和安装点完全一致。
for($stage=0;$stage -le 4;$stage++) {
    $im=Export-AtlasCell 'disaster' 1 2 88 116
    for($y=0;$y -lt $im.Height;$y++) {for($x=0;$x -lt $im.Width;$x++) {
        $p=$im.GetPixel($x,$y)
        if($p.G -gt $p.R*1.5 -and $p.B -gt $p.R*1.4 -and $y -lt (91-($stage*14))) {
            $im.SetPixel($x,$y,[Drawing.Color]::FromArgb($p.A,39,65,73))
        }
    }}
    Save-Bitmap $im (Join-Path $art "Disaster_canister_$stage.png");$im.Dispose()
}

# 配色为 RGB；headgear 尺寸/偏移是头轨道贴图空间的像素，与 ZombieIdentityVisuals.cpp 一致。
$styles=@(
    @{Stem='worker'; Rig='NormalZombie'; Name='IceWorkerZombie'; Coat=@(58,110,190); Pants=@(45,70,110); Trim=@(224,226,211); Hat=@(56,35,0,-18)},
    @{Stem='boiler'; Rig='NormalZombie'; Name='BoilerZombie'; Coat=@(215,82,35); Pants=@(66,62,60); Trim=@(231,175,58); Hat=@(52,20,-2,-4)},
    @{Stem='jammer'; Rig='BucketZombie'; Name='WeatherJammerZombie'; Coat=@(137,66,177); Pants=@(72,57,104); Trim=@(238,204,81)},
    @{Stem='healer'; Rig='NormalZombie'; Name='HealerZombie'; Coat=@(224,225,207); Pants=@(65,104,81); Trim=@(47,137,78); Hat=@(60,38,-8,-24)},
    @{Stem='sludge'; Rig='ConeZombie'; Name='SludgeZombie'; Coat=@(163,177,48); Pants=@(66,83,43); Trim=@(230,194,50)},
    @{Stem='guard'; Rig='NormalZombie'; Name='PressureGuardZombie'; Coat=@(35,130,140); Pants=@(40,70,91); Trim=@(199,217,212)},
    @{Stem='coldguard'; Rig='NormalZombie'; Name='ColdChainGuardZombie'; Coat=@(76,104,164); Pants=@(48,63,92); Trim=@(225,227,210); Hat=@(58,49,-4,-20)},
    @{Stem='disaster'; Rig='NormalZombie'; Name='DisasterEngineerZombie'; Coat=@(204,147,52); Pants=@(63,81,91); Trim=@(222,227,205); Hat=@(60,37,-8,-19)},
    @{Stem='insulator'; Rig='NormalZombie'; Name='InsulatorZombie'; Coat=@(142,108,57); Pants=@(68,65,49); Trim=@(225,217,177); Hat=@(58,32,-6,-19)},
    @{Stem='grounding'; Rig='GroundingZombie'; Name='GroundingZombie'; Coat=@(100,64,139); Pants=@(55,53,76); Trim=@(200,139,78)},
    @{Stem='iceengineer'; Rig='ConeZombie'; Name='IceWallEngineerZombie'; Coat=@(60,130,146); Pants=@(47,76,89); Trim=@(227,164,66)},
    @{Stem='clockmaker'; Rig='NormalZombie'; Name='PolarClockmakerZombie'; Coat=@(63,66,120); Pants=@(53,52,77); Trim=@(202,168,85); Hat=@(60,38,-8,-24)},
    @{Stem='priest'; Rig='NormalZombie'; Name='AuroraPriestZombie'; Coat=@(104,62,145); Pants=@(58,60,89); Trim=@(129,211,184); Hat=@(54,38,-3,-26)},
    @{Stem='drummer'; Rig='CrystalDrummerZombie'; Name='CrystalDrummerZombie'; Coat=@(145,97,57); Pants=@(75,57,82); Trim=@(199,158,85); Hat=@(60,42,-8,-28)}
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
    if($stem -eq 'insulator') {
        foreach($part in @('innerarm_hand','outerarm_hand','outerarm_hand2','innerleg_foot','outerleg_foot')) {
            $im=Read-Bitmap (Join-Path $art "Zombie_$part.png")
            Convert-Fabric $im @(57,62,47) 'rubber' $null
            Save-Bitmap $im (Join-Path $art "Identity_${stem}_$part.png");$im.Dispose()
            $rig=$rig.Replace("<i>IMAGE_REANIM_ZOMBIE_$($part.ToUpperInvariant())</i>","<i>IMAGE_REANIM_IDENTITY_INSULATOR_$($part.ToUpperInvariant())</i>")
        }
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
    $hand=if($stem -eq 'insulator') {'Identity_insulator_outerarm_hand.png'} else {'Zombie_outerarm_hand.png'}
    $im=Read-Bitmap (Join-Path $art $hand);$g.DrawImageUnscaled($im,2,24);$im.Dispose();$g.Dispose()
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
# 碎片取自实际装备纹理，轮廓裁切只负责破片形状；颜色、材质和阴影同源。
foreach($e in @(
    @{Stem='coldguard';File='ColdChain_shield';Rect=@(13,20,28,38)},
    @{Stem='insulator';File='Zombie_insulator_armor1';Rect=@(17,25,25,35)},
    @{Stem='clockmaker';File='Zombie_polar_clock_disk';Rect=@(15,18,70,70)},
    @{Stem='priest';File='Zombie_aurora_device';Rect=@(30,28,38,50)},
    @{Stem='drummer';File='CrystalDrummer_drum';Rect=@(10,35,28,18)}
)) {
    $im=[Drawing.Bitmap]::new((Join-Path $art "$($e.File).png"))
    for($i=0;$i -lt 3;$i++) {
        $fragment=[Drawing.Bitmap]::new(16,18);$g=New-Graphics $fragment
        $shape=[Drawing.Drawing2D.GraphicsPath]::new()
        $shape.AddPolygon([Drawing.Point[]]@([Drawing.Point]::new(1,4+$i),[Drawing.Point]::new(12-$i,1),[Drawing.Point]::new(15,10),[Drawing.Point]::new(5,17)))
        $g.SetClip($shape)
        $g.DrawImage($im,[Drawing.Rectangle]::new(0,0,16,18),[Drawing.Rectangle]::new($e.Rect[0],$e.Rect[1],$e.Rect[2],$e.Rect[3]),[Drawing.GraphicsUnit]::Pixel)
        $g.Dispose();$shape.Dispose()
        Save-Bitmap $fragment (Join-Path $art "Identity_$($e.Stem)_shard$i.png");$fragment.Dispose()
    }
    $im.Dispose()
}
function Export-Feedback([string]$name, [string]$image, [int]$count, [string]$scale, [string]$speed, [string]$gravity, [string]$color, [string]$extra='') {
    $xml=@"
<Emitter>
  <Name>$name</Name>
  <SpawnMinActive>$count</SpawnMinActive><SpawnMaxLaunched>$count</SpawnMaxLaunched>
  <SystemDuration>0.8</SystemDuration><ParticleDuration>[0.45 0.8]</ParticleDuration>
  <ParticleAlpha>0.9,55 0.65,100 0</ParticleAlpha><ParticleScale>$scale</ParticleScale>
  <EmitterType>Circle</EmitterType><EmitterRadius>[2 12]</EmitterRadius>
  <LaunchSpeed>$speed</LaunchSpeed><RandomLaunchSpin>1</RandomLaunchSpin>
  <ParticleGravity>$gravity</ParticleGravity><ParticleSpinSpeed>[-120 120]</ParticleSpinSpeed>
  $color
  <Image>$image</Image>
  $extra
</Emitter>
"@
    $path=Join-Path $res "particles/config/$name.xml"
    $xml=[regex]::Replace($xml.Replace("`r`n","`n"),'(?m)[ \t]+$','')
    [IO.File]::WriteAllText($path,$xml+"`n",[Text.UTF8Encoding]::new($false));$outputs.Add($path)
}
foreach($e in @(@('coldguard','IdentityColdShieldBreak'),@('insulator','IdentityCeramicBreak'),@('clockmaker','IdentityClockShards'),@('priest','IdentityAuroraShards'))) {
    $keys=(0..2 | ForEach-Object {"IMAGE_IDENTITY_$($e[0].ToUpperInvariant())_SHARD$_"}) -join ','
    Export-Feedback $e[1] $keys 9 '[0.45 0.8]' '[35 85]' '135' ''
}
$ice='<ParticleRed>.55</ParticleRed><ParticleGreen>.88</ParticleGreen><ParticleBlue>1</ParticleBlue>'
$gold='<ParticleRed>1</ParticleRed><ParticleGreen>.8</ParticleGreen><ParticleBlue>.4</ParticleBlue>'
Export-Feedback 'IdentityColdShieldRepair' 'PARTICLE_WINDSTREAK' 7 '[.025 .045]' '[8 25]' '-12' $ice '<Field><FieldType>Circle</FieldType><X>22</X></Field>'
Export-Feedback 'IdentityCeramicCharge' 'PARTICLE_WINDSTREAK' 9 '[.025 .05]' '[24 50]' '0' $gold
Export-Feedback 'IdentityGroundingSpark' 'PARTICLE_WINDSTREAK' 7 '[.02 .04]' '[20 45]' '0' $gold
Export-Feedback 'IdentityDisasterReady' 'PARTICLE_WINDSTREAK' 5 '[.02 .04]' '[8 18]' '-20' $ice
Export-Feedback 'IdentityDisasterVent' 'PARTICLE_ZAMBONISMOKE_PART_0,PARTICLE_ZAMBONISMOKE_PART_1,PARTICLE_ZAMBONISMOKE_PART_2' 7 '[.65 1.0]' '[10 24]' '-35' $ice
Export-Feedback 'IdentityDrumBeat' 'IMAGE_IDENTITY_DRUMMER_SHARD0,IMAGE_IDENTITY_DRUMMER_SHARD1,IMAGE_IDENTITY_DRUMMER_SHARD2' 6 '[.25 .45]' '[30 60]' '35' ''
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
