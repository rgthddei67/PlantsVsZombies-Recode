param(
    [string]$ResourceRoot = (Join-Path $PSScriptRoot "../build/clang-release/resources")
)

$ErrorActionPreference = "Stop"
$sourceReanim = Join-Path $ResourceRoot "reanim/ConeZombie.reanim"
$targetReanim = Join-Path $ResourceRoot "reanim/GroundingZombie.reanim"

# 三档天线路障由 generate_zombie_identity_assets.ps1 生成；这里仅维护它读取的参考时间轴。
$reanimText = [System.IO.File]::ReadAllText($sourceReanim)
$reanimText = $reanimText.Replace(
    "IMAGE_REANIM_ZOMBIE_CONE1", "IMAGE_REANIM_ZOMBIE_GROUNDING_CONE1")
[System.IO.File]::WriteAllText(
    $targetReanim, $reanimText, [System.Text.UTF8Encoding]::new($false))

$expectedReanimHash = "899C6FCAC8610C8A5DD57E0CE21C9BF561D8790EC2D92D3640A2E4A2CEA01B1F"
$actualReanimHash = (Get-FileHash $targetReanim -Algorithm SHA256).Hash
if ($actualReanimHash -ne $expectedReanimHash) {
    throw "Unexpected generated hash for GroundingZombie.reanim: $actualReanimHash"
}

Write-Output "Generated GroundingZombie reference reanimation in $ResourceRoot"
