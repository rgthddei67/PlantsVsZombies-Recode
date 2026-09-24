param([Parameter(Mandatory=$true)][string]$GameDirectory,
      [Parameter(Mandatory=$true)][string]$Script,
      [int]$TimeoutSeconds = 900)
$ErrorActionPreference = 'Stop'
$gameDir = (Resolve-Path -LiteralPath $GameDirectory).Path
$scriptPath = (Resolve-Path -LiteralPath $Script).Path
# 训练也在当前桌面可见；启动不固定种子，每局由实验脚本记录独立种子以便配对复测。
$process = Start-Process -FilePath (Join-Path $gameDir 'PlantsVsZombies.exe') `
    -ArgumentList @('-AutoTest', ('"' + $scriptPath + '"')) `
    -WorkingDirectory $gameDir -WindowStyle Normal -PassThru `
    -RedirectStandardError ($scriptPath + ".stderr.log") -RedirectStandardOutput ($scriptPath + ".stdout.log")
$processHandle = $process.Handle # 保持进程句柄，确保快速退出时仍可取得退出码
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    # 仅终止这个脚本自己启动且已经超时的子进程。
    $process.Kill()
    throw "Commander batch timed out: $scriptPath"
}
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Commander batch exit code: $($process.ExitCode)" }
