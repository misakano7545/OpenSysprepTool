$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false
Set-Location 'D:\dev\OpenSysprepTool'

# 离线 hive 三项的实测验收：我的文档 / 收藏夹 / IE 主页。
#
# 为什么必须拷一份 NTUSER.DAT 来测：这三项写的是
# <系统盘>\Users\Default\NTUSER.DAT，直接改等于改了本机所有新用户的默认配置。
# 所以这里把真实的默认配置复制到临时目录，用 optimization.defaultUserHive
# 指过去，验证「挂载 -> 写 -> 卸载 -> 重新挂载读回」整条链路，绝不碰原件。
#
# 需要管理员：RegLoadKey 要求启用 SeRestorePrivilege / SeBackupPrivilege。

$ospt = if ($args.Count -ge 1) { $args[0] } else { 'build\bin\Release\ospt.exe' }

# 提权进程的输出没法用 Start-Process 重定向（-Verb RunAs 与 -RedirectStandardOutput 互斥），
# 所以自己开一份 transcript 落盘，便于事后核对。
$transcript = Join-Path (Get-Location) 'build\m10\hive-verify.log'
New-Item -ItemType Directory -Path (Split-Path $transcript) -Force | Out-Null
try { Start-Transcript -Path $transcript -Force | Out-Null } catch { }
$work = Join-Path (Get-Location) 'build\m10\hive-check'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null

$fail = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) { Write-Host "  PASS  $name" }
    else {
        Write-Host "  FAIL  $name  [$detail]"
        $script:fail++
    }
}

$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $elevated) {
    Write-Host '  SKIP  离线 hive 往返  [当前会话未提权；RegLoadKey 需要管理员]'
    Write-Host 'RESULT: ALL PASSED（0 项检查，1 项跳过）'
    try { Stop-Transcript | Out-Null } catch { }
    exit 0
}

$source = 'C:\Users\Default\NTUSER.DAT'
if (-not (Test-Path -LiteralPath $source)) {
    Write-Host "  SKIP  离线 hive 往返  [本机没有 $source]"
    Write-Host 'RESULT: ALL PASSED（0 项检查，1 项跳过）'
    try { Stop-Transcript | Out-Null } catch { }
    exit 0
}

$copy = Join-Path $work 'ntuser.dat'
Copy-Item -LiteralPath $source -Destination $copy -Force
Check '拷出一份默认用户配置用于测试' (Test-Path -LiteralPath $copy)

$cfg = Join-Path $work 'cfg.json'
$json = @"
{
  "schemaVersion": 1,
  "optimization": {
    "moveDocuments": true, "documentsSmart": false, "myDocumentsTarget": "D:\\OspDocs",
    "moveFavorites": true, "favoritesSmart": false, "favoritesTarget": "D:\\OspFavs",
    "saveIeHomePage": true, "ieHomePage": "http://osp.example/home",
    "defaultUserHive": "$($copy -replace '\\','\\')"
  },
  "runPlan": []
}
"@
Set-Content -LiteralPath $cfg -Value $json -Encoding UTF8

$journal = Join-Path $work 'journal.json'
$output = & $ospt --prepare $cfg --execute --journal $journal *>&1 | ForEach-Object { [string]$_ }
$code = $LASTEXITCODE
$output | ForEach-Object { Write-Host "    $_" }

Check 'prepare --execute 退出码 0' ($code -eq 0) "exit=$code"
# IE 主页在 Win10/11 上写不进（系统键保护），必须降级为告警而不是失败。
# 判定用 ASCII 标记：ospt 输出是 UTF-8，PowerShell 用 ANSI 解码后中文会成乱码，
# 拿中文去匹配必然匹配不上（实测踩过），所以只看 warning: 前缀与 Start Page。
$ieWarning = ($output | Select-String -Pattern 'warning:\s*optimization\.saveIeHomePage').Count -gt 0
$ieWritten = ($output | Select-String -Pattern 'HiveSet.*Start Page').Count -gt 0
Check 'IE 主页：写入成功或降级为告警' ($ieWarning -or $ieWritten) ("warning=$ieWarning written=$ieWritten")

# 重新挂载副本，读回两个路径值 —— 这才证明「卸载后真的落盘」
& reg.exe unload 'HKU\OSPTCHECK' 2>&1 | Out-Null
$loadMsg = & reg.exe load 'HKU\OSPTCHECK' $copy 2>&1
$loadOk = $LASTEXITCODE -eq 0
Check '执行完能重新挂载副本（说明卸载干净）' $loadOk ("load exit=$LASTEXITCODE msg=$loadMsg")

if ($loadOk) {
    $folders = & reg.exe query 'HKU\OSPTCHECK\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' 2>&1
    $personal = ($folders | Select-String -Pattern 'Personal\s+REG_\w+\s+(.+)$').Matches
    $favorites = ($folders | Select-String -Pattern 'Favorites\s+REG_\w+\s+(.+)$').Matches
    $personalValue = if ($personal.Count -gt 0) { $personal[0].Groups[1].Value.Trim() } else { '' }
    $favoritesValue = if ($favorites.Count -gt 0) { $favorites[0].Groups[1].Value.Trim() } else { '' }
    Check "我的文档已写入副本 (Personal = D:\OspDocs)" ($personalValue -eq 'D:\OspDocs') "实际=$personalValue"
    Check "收藏夹已写入副本 (Favorites = D:\OspFavs)" ($favoritesValue -eq 'D:\OspFavs') "实际=$favoritesValue"
    Check '路径值是 REG_EXPAND_SZ' (($folders | Select-String -Pattern 'REG_EXPAND_SZ').Count -ge 2)
    & reg.exe unload 'HKU\OSPTCHECK' 2>&1 | Out-Null
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue

Write-Host ''
if ($fail -gt 0) {
    Write-Host "RESULT: FAILED（$fail 项失败）"
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
Write-Host 'RESULT: ALL PASSED'
try { Stop-Transcript | Out-Null } catch { }
exit 0
