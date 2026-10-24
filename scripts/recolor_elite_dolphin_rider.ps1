param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$riderHue = 220.0
$dolphinHue = 340.0
$riderSuffixes = @(
    'BODY1',
    'BODY2',
    'HEAD',
    'INNERARM_HAND',
    'INNERARM_LOWER',
    'INNERARM_UPPER',
    'INNERLEG_FOOT',
    'INNERLEG_LOWER',
    'INNERLEG_UPPER',
    'OUTERARM_HAND',
    'OUTERARM_LOWER',
    'OUTERARM_UPPER',
    'OUTERARM_UPPER2',
    'OUTERLEG_FOOT1',
    'OUTERLEG_FOOT2',
    'OUTERLEG_LOWER',
    'OUTERLEG_UPPER'
)
$dolphinSuffixes = @(
    'DOLPHINBODY1',
    'DOLPHINBODY2',
    'DOLPHINFIN1',
    'DOLPHINFIN2',
    'DOLPHININWATER',
    'DOLPHINJAW'
)

# Generated outputs are locked so threshold or runtime drift cannot silently alter art.
Set-Variable -Name expectedHashes -Value (@{ bootstrap = 'bootstrap' }) -Scope Script
$expectedHashes.Remove('bootstrap')
$expectedHashes['image/reanim/Zombie_elitedolphinrider_body1.png'] = '215e3031226bd488c49e0720a8a39a1b1154bc47f0885a24df49660d07192a3d'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_body2.png'] = '2261642a92053267a033ac27ad73b36fce40189b377dfd6a8f9dc7886e9d85aa'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphinbody1.png'] = 'a6841752d3e10de17cafdac2c97a176a2e38dfbb94e7347a62220e57cdfbc18c'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphinbody2.png'] = '36750b556060d2ddfe0f83b545bccbdc5facb0ff6830825980378e398ed04c87'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphinfin1.png'] = 'afc9dce9c10038da4c4405c5f77a2b9f43e98663fba1ef1f6b7d6a44311cee73'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphinfin2.png'] = 'a0733a39a1255c86e6ee813e19a8fa1bde35aaaf19c3294a1bc30255483f717a'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphininwater.png'] = '8661893c1b5c16f721c354fdb39ed0e50d5a5e8e495e0dbb524736b75375168f'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_dolphinjaw.png'] = '62347315e31aeb0a37b32584d046d341bd27f6e70e8904862fa21dcc74c3d068'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_head.png'] = 'ba171692c1d6e3febcd280af770638925c10d01b29a1c2a3434483505f5de26b'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerarm_hand.png'] = '165ff9512d6961cfc478385de4109398eee66e1e336339d14d46fe766de8530f'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerarm_lower.png'] = 'cef5c36965011e7415f6f340ebfbc8e629cb05f71ab72dd7b8b9acf8f7cd4992'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerarm_upper.png'] = '88f9a6e35355fd34090a33c563cd1299b5b7569c8d6e3556ab9144598c408589'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerleg_foot.png'] = '319ff2d30e70c23995b3dd99f99496a33bc9e0661b53995ec5463d5af8a2dbf3'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerleg_lower.png'] = '704e92e80a1627ae5c42e7fca240b6c7dd809102f356f1cf7d0c00429f953fde'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_innerleg_upper.png'] = 'd26489d86b3c75cdc9915f7a1536b3a259004b0bdd677dc59d71584304335560'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerarm_hand.png'] = '0424694748129c0259b64d6050de923637e7809849bff863fd0f068856bc3b8a'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerarm_lower.png'] = 'b35fb3baf5b06512b3d274f5dd5cd1c3bb2a14bdf4b907d53a5039ee7bd7eeb1'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerarm_upper.png'] = 'f9b35199d99dec7137858f6d48ea1fee5d017a03afa47372abc90aad4077cb10'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerarm_upper2.png'] = '490907f2a8508d7db0bc76f9ebf57443a585d011b1cf55a047c4a6ee5075f12a'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerleg_foot1.png'] = '7fc70edf40718b93aadd59e6ef403ce1c496c2d3ffef4363a9595f851462afad'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerleg_foot2.png'] = '9bc8787e0847e6a4020d8a2808972dc697c3bab3b9060b3bcdd8f3ba336c33eb'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerleg_lower.png'] = 'a5e5314fc41ad7ffff6550808c6ba4770826991622b00ef7335a5acbb5ee8000'
$expectedHashes['image/reanim/Zombie_elitedolphinrider_outerleg_upper.png'] = 'bde452dda9976746e0c24f6becb9588008bef58ca5fe6d8420c24072ef72e0cf'
$expectedHashes['particles/ZombieEliteDolphinRiderHead.png'] = '94ccdeb34e3a50e772dcc47a9977d94eb7444b1ac4441a51cff0bae5de15171d'
$expectedHashes['reanim/EliteDolphinRider.reanim'] = '430816fc98e53b4051e980271c6c9a3a5571e1d805e2123e5df363293a34b3e8'

function Convert-HsvToRgb {
    param(
        [double]$Hue,
        [double]$Saturation,
        [double]$Value
    )

    $chroma = $Value * $Saturation
    $sector = $Hue / 60.0
    $x = $chroma * (1.0 - [Math]::Abs(($sector % 2.0) - 1.0))
    $r1 = 0.0
    $g1 = 0.0
    $b1 = 0.0

    if ($sector -lt 1.0) { $r1 = $chroma; $g1 = $x }
    elseif ($sector -lt 2.0) { $r1 = $x; $g1 = $chroma }
    elseif ($sector -lt 3.0) { $g1 = $chroma; $b1 = $x }
    elseif ($sector -lt 4.0) { $g1 = $x; $b1 = $chroma }
    elseif ($sector -lt 5.0) { $r1 = $x; $b1 = $chroma }
    else { $r1 = $chroma; $b1 = $x }

    $m = $Value - $chroma
    return @(
        [Math]::Min(255, [Math]::Max(0, [int][Math]::Round(($r1 + $m) * 255.0))),
        [Math]::Min(255, [Math]::Max(0, [int][Math]::Round(($g1 + $m) * 255.0))),
        [Math]::Min(255, [Math]::Max(0, [int][Math]::Round(($b1 + $m) * 255.0)))
    )
}

function Convert-Image {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [Parameter(Mandatory = $true)][ValidateSet('Rider', 'Dolphin')][string]$Material
    )

    $source = [System.Drawing.Bitmap]::new($SourcePath)
    $result = [System.Drawing.Bitmap]::new(
        $source.Width,
        $source.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    try {
        for ($y = 0; $y -lt $source.Height; $y++) {
            for ($x = 0; $x -lt $source.Width; $x++) {
                $color = $source.GetPixel($x, $y)
                if ($color.A -eq 0) {
                    $result.SetPixel($x, $y, $color)
                    continue
                }

                $red = $color.R / 255.0
                $green = $color.G / 255.0
                $blue = $color.B / 255.0
                $max = [Math]::Max($red, [Math]::Max($green, $blue))
                $min = [Math]::Min($red, [Math]::Min($green, $blue))
                $delta = $max - $min
                $saturation = if ($max -gt 0.0) { $delta / $max } else { 0.0 }
                $replace = $false
                $targetHue = 0.0
                $targetSaturation = 0.0
                $targetValue = $max

                if ($Material -eq 'Rider') {
                    # Select saturated red wetsuit material, retaining skin, white trim and outlines.
                    $replace = $max -ge 0.08 `
                        -and $saturation -ge 0.20 `
                        -and $red -gt ($green * 1.12) `
                        -and $red -gt ($blue * 1.05)
                    $targetHue = $riderHue
                    $targetSaturation = [Math]::Min(
                        0.92, [Math]::Max(0.60, $saturation * 1.03))
                    $targetValue = [Math]::Min(0.92, $max * 0.94)
                }
                else {
                    # Select mid-value gray dolphin material, retaining white belly, eyes and black outline.
                    $replace = $max -ge 0.10 `
                        -and $max -le 0.82 `
                        -and $saturation -le 0.50
                    $targetHue = $dolphinHue
                    $targetSaturation = [Math]::Min(
                        0.50, [Math]::Max(0.30, 0.36 + $saturation * 0.20))
                    $targetValue = [Math]::Min(0.90, $max * 1.10)
                }

                if (-not $replace) {
                    $result.SetPixel($x, $y, $color)
                    continue
                }

                $rgb = Convert-HsvToRgb `
                    -Hue $targetHue `
                    -Saturation $targetSaturation `
                    -Value $targetValue
                $result.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                    $color.A, $rgb[0], $rgb[1], $rgb[2]))
            }
        }

        $result.Save($DestinationPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $result.Dispose()
        $source.Dispose()
    }
}

$resourceRoot = Join-Path $RepositoryRoot 'build\clang-release\resources'
$imageDirectory = Join-Path $resourceRoot 'image\reanim'
$generatedFiles = [System.Collections.Generic.List[string]]::new()

foreach ($suffix in $riderSuffixes) {
    $sourceName = "Zombie_dolphinrider_$($suffix.ToLowerInvariant()).png"
    $targetName = "Zombie_elitedolphinrider_$($suffix.ToLowerInvariant()).png"
    $sourcePath = Join-Path $imageDirectory $sourceName
    $targetPath = Join-Path $imageDirectory $targetName
    Convert-Image -SourcePath $sourcePath -DestinationPath $targetPath -Material Rider
    $generatedFiles.Add($targetPath)
}

foreach ($suffix in $dolphinSuffixes) {
    $sourceName = "Zombie_dolphinrider_$($suffix.ToLowerInvariant()).png"
    $targetName = "Zombie_elitedolphinrider_$($suffix.ToLowerInvariant()).png"
    $sourcePath = Join-Path $imageDirectory $sourceName
    $targetPath = Join-Path $imageDirectory $targetName
    Convert-Image -SourcePath $sourcePath -DestinationPath $targetPath -Material Dolphin
    $generatedFiles.Add($targetPath)
}

$sourceHead = Join-Path $resourceRoot 'particles\ZombieDolphinRiderHead.png'
$targetHead = Join-Path $resourceRoot 'particles\ZombieEliteDolphinRiderHead.png'
Convert-Image -SourcePath $sourceHead -DestinationPath $targetHead -Material Rider
$generatedFiles.Add($targetHead)

$sourceReanim = Join-Path $resourceRoot 'reanim\Zombie_dolphinrider.reanim'
$targetReanim = Join-Path $resourceRoot 'reanim\EliteDolphinRider.reanim'
$reanimText = [System.IO.File]::ReadAllText($sourceReanim, [System.Text.Encoding]::UTF8)
foreach ($suffix in @($riderSuffixes + $dolphinSuffixes)) {
    $reanimText = $reanimText.Replace(
        "IMAGE_REANIM_ZOMBIE_DOLPHINRIDER_$suffix",
        "IMAGE_REANIM_ZOMBIE_ELITEDOLPHINRIDER_$suffix")
}
[System.IO.File]::WriteAllText(
    $targetReanim,
    $reanimText,
    [System.Text.UTF8Encoding]::new($false))
$generatedFiles.Add($targetReanim)

$actualHashes = @{}
foreach ($path in $generatedFiles) {
    $relativePath = $path.Substring($resourceRoot.Length + 1).Replace('\', '/')
    $actualHashes[$relativePath] =
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

if ($expectedHashes.Count -ne $generatedFiles.Count) {
    foreach ($relativePath in ($actualHashes.Keys | Sort-Object)) {
        Write-Output "`$expectedHashes['$relativePath'] = '$($actualHashes[$relativePath])'"
    }
    throw "Expected hash count $($expectedHashes.Count) does not match generated file count $($generatedFiles.Count)."
}

foreach ($relativePath in $actualHashes.Keys) {
    if (-not $expectedHashes.ContainsKey($relativePath)) {
        throw "Missing expected SHA-256 for $relativePath."
    }
    if ($actualHashes[$relativePath] -ne $expectedHashes[$relativePath]) {
        throw "SHA-256 mismatch for $relativePath. Expected $($expectedHashes[$relativePath]), got $($actualHashes[$relativePath])."
    }
}

Write-Output "Verified $($generatedFiles.Count) elite dolphin rider resource hashes."
