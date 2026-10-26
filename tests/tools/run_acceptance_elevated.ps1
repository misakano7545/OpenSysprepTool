<#
  run_acceptance_elevated.ps1 —— 本机验收统一入口（在提权会话里跑）

  为什么需要它：UI 的常驻关窗验收需要在提权会话里执行，避免 UIPI 拒绝跨权限窗口消息；
  需要真实执行的部署/驱动操作也要求管理员。只读生成与 dry-run 验收不要求提权。
  把全部闸门放进**一个**提权会话里跑，只需提权一次。

  用法：
      powershell -ExecutionPolicy Bypass -File tests/tools/run_acceptance_elevated.ps1
      powershell ... -BuildDir build-x86 -LogPath build-x86/acceptance.log

  未提权时脚本会用 runas 自我重启（UAC 设成「从不通知」的机器不弹窗）。
#>
param(
    [string]$BuildDir = 'build',
    [string]$LogPath  = '',
    [string]$Python   = 'python'
)

$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
if ([string]::IsNullOrWhiteSpace($LogPath)) { $LogPath = Join-Path $BuildDir 'acceptance-elevated.log' }

function Test-Elevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Elevated)) {
    Write-Host '当前会话未提权，改用 runas 重新启动（UAC 可能会弹一次）...'
    # 参数必须全部转发：漏掉 -Python 时提权会话里 python 不在 PATH，后半程全部 9009
    $args = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', ('"' + $PSCommandPath + '"'),
        '-BuildDir', $BuildDir, '-LogPath', $LogPath, '-Python', $Python
    )
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $args -Verb RunAs -Wait -PassThru
    Write-Host ("提权会话退出码 = " + $proc.ExitCode)
    if (Test-Path $LogPath) { Get-Content $LogPath -Tail 25 }
    exit $proc.ExitCode
}

$bin = Join-Path $repo (Join-Path $BuildDir 'bin\Release')
$ospt = Join-Path $bin 'ospt.exe'
$osptd = Join-Path $bin 'osptd.exe'
$tests = Join-Path $bin 'osp_unit_tests.exe'
$work = Join-Path $repo (Join-Path $BuildDir 'acceptance')
New-Item -ItemType Directory -Force -Path $work | Out-Null
if (Test-Path $LogPath) { Remove-Item $LogPath -Force }

$script:results = @()
function Step([string]$name, [scriptblock]$body) {
    Write-Host ("`n===== " + $name)
    $code = 0
    try { & $body; $code = $LASTEXITCODE } catch { Write-Host ("异常: " + $_); $code = 99 }
    if ($null -eq $code) { $code = 0 }
    $script:results += [pscustomobject]@{ Step = $name; ExitCode = $code }
    Add-Content -Path $LogPath -Value ("===== " + $name + "  exit=" + $code)
    Write-Host ("----- exit=" + $code)
}

Write-Host ('提权会话：' + (Test-Elevated) + '  repo=' + $repo + '  build=' + $BuildDir)

Step '构建' { cmake --build $BuildDir --config Release }
Step 'ctest' { & $tests }
Step 'selftest' { & $ospt --selftest }
Step 'UI 冒烟' { & $ospt --ui-smoke-test }
Step '系统体检' { & $ospt --checkup --json (Join-Path $work 'checkup.json')
                  & $Python tests/integration/verify_checkup.py (Join-Path $work 'checkup.json') }
Step 'unattend' { & $ospt --gen-unattend tests/integration/fixtures/m3_cfg.json --out (Join-Path $work 'unattend.xml')
                  & $Python tests/integration/verify_unattend.py (Join-Path $work 'unattend.xml') tests/integration/fixtures/m3_cfg.json }
Step 'runplan' { & $ospt --gen-runplan tests/integration/fixtures/m4_plan.json --out (Join-Path $work 'run')
                 & $Python tests/integration/verify_runplan.py tests/integration/fixtures/m4_plan.json (Join-Path $work 'run') }
Step '部署运行时' { & $Python tests/integration/verify_deploy_exec.py $ospt $osptd }
Step '部署前闸门' { & $Python tests/integration/verify_deploy_gate.py $osptd }
Step 'Win7 兼容' { & $Python tests/integration/verify_win7_compat.py $ospt $osptd $tests }
Step 'Win7 闸门负例' { & $Python tests/integration/verify_win7_compat_negative.py $osptd }
Step '驱动注入' { & $Python tests/integration/verify_drivers.py $ospt }
Step '封装前设置' { & $Python tests/integration/verify_prepare.py $ospt }
Step '图形部署页' { & $Python tests/integration/verify_presentation.py $osptd }
Step 'OEM 识别' { & $Python tests/integration/verify_oem.py $ospt }
Step '系统优化与修复' { & $Python tests/integration/verify_tweaks.py $ospt }
Step '离线 hive 三项' { & powershell -NoProfile -ExecutionPolicy Bypass -File tests/tools/verify_hive.ps1 $ospt }
Step '界面验收' { & $Python tests/integration/verify_ui.py $ospt }
Step '管理员权限' { & $Python tests/integration/verify_elevation.py $ospt $osptd $tests }
Step '发布契约' { & $Python tests/integration/verify_release.py --check-tag v0.1.2 --check-workflow .github/workflows/release.yml --check-changelog CHANGELOG.md --check-readme README.md }

Write-Host "`n==================== 汇总 ===================="
$failed = 0
foreach ($r in $script:results) {
    $mark = 'OK  '
    if ($r.ExitCode -ne 0) { $mark = 'FAIL'; $failed++ }
    Write-Host ("  {0}  {1,-16} exit={2}" -f $mark, $r.Step, $r.ExitCode)
}
Write-Host ("失败步骤数: " + $failed + "   日志: " + $LogPath)
exit ([int]($failed -gt 0))
