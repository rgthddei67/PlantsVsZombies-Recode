param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "../build/clang-release/resources/image/reanim")
)

$ErrorActionPreference = "Stop"
# 保留旧命令入口；胸甲、服装及离体碎片必须由同一生成器维护。
$authoritative = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../build/clang-release/resources/image/reanim"))
if ([IO.Path]::GetFullPath($OutputDirectory) -ne $authoritative) {
    throw "Insulator assets now use the authoritative resource directory. Run scripts/generate_zombie_identity_assets.ps1."
}
& (Join-Path $PSScriptRoot 'generate_zombie_identity_assets.ps1')
