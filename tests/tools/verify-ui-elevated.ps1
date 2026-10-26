<#
  verify-ui-elevated.ps1 —— 在一个提权会话里跑完整的 verify_ui.py。

  ospt 的 UI 启动会自动提权；未提权调用者不能向提权窗口发送 WM_CLOSE（UIPI 会拒绝）。
  CI 把只读冒烟与这个交互验收分开，避免把权限错误误报成关窗故障。
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [Parameter(Mandatory = $true)][string]$Ospt,
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Continue'
$PSNativeCommandUseErrorActionPreference = $false
$log = Join-Path $Repo 'ui-elevated.log'
$done = Join-Path $env:TEMP 'osp-ui-elevated.exit'

function Test-Elevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Elevated)) {
    if (Test-Path $done) { Remove-Item $done -Force }
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
        '-Repo', ('"' + $Repo + '"'),
        '-Ospt', ('"' + $Ospt + '"'),
        '-Python', ('"' + $Python + '"')
    ) -Verb RunAs -Wait -PassThru
    if (Test-Path $log) {
        Get-Content $log | ForEach-Object { Write-Host $_ }
    }
    if (Test-Path $done) { exit [int](Get-Content $done) }
    Write-Host '::error::提权 UI 验收没有产出退出码'
    exit 1
}

Set-Location -LiteralPath $Repo
if (Test-Path $done) { Remove-Item $done -Force }
& $Python tests\integration\verify_ui.py $Ospt *>&1 | Out-File -FilePath $log -Encoding utf8
$code = $LASTEXITCODE
if ($null -eq $code) { $code = 1 }
Set-Content -Path $done -Value $code -Encoding ascii
Get-Content $log | ForEach-Object { Write-Host $_ }
exit $code
