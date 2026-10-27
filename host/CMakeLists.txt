# host —— 主程序 ospt.exe
#
# 子系统是 WINDOWS（不是 CONSOLE）：正常启动只出现配置界面，不再附带命令行窗口。
# /ENTRY:mainCRTStartup 让入口仍是标准的 main()，CLI 与 --ui-smoke-test 的输出
# 靠 main.cpp 里 attachParentConsoleIfNeeded() 回落到父控制台（仅当 stdout 不可用时）。
#
# manifest 保持 asInvoker：界面和真正需要权限的操作由 main() 自检后用 runas 提权。
# 若写 requireAdministrator，未提权的 CreateProcess 会直接以 740 拒绝，代码没机会执行。
# 提权副本的退出码和 CLI 输出会回传给调用方；只读开关不自动提权。

add_executable(ospt
  main.cpp
  # M9：封装前设置的命令行（--prepare / --rollback / --prepare-selftest）
  cli_prepare.cpp
  # M10：OEM 识别与 SC OEM 档案导入（--oem-detect / --import-oem）
  cli_oem.cpp
  # M10：一键修复（--repair，等同 SC 的 Btn_OsRepair）
  cli_repair.cpp
  # M7a：主界面框架与配置页（原生控件 + WTL 薄封装）
  ui/ui_frame.cpp
  ui/ui_pages.cpp
  # M10：在界面里拉起 osptd 并读回结构化进度（运行页 / 完成页）
  ui/deploy_runner.cpp
  # M10：SC 的小确认弹窗 TSBOX（重启方式 / 宽带创建方式）
  ui/popups.cpp
)

target_link_options(ospt PRIVATE
  "/SUBSYSTEM:WINDOWS,6.01"
  "/ENTRY:mainCRTStartup"
  # GUI 子系统保证双击/无参数只有 UI，没有命令行黑框。
  # 要命令行窗口时用 ospt --console；CLI 输出由 main.cpp 按需挂回父控制台。
  # 清单保持 asInvoker：requireAdministrator 会让 CreateProcess 在未提权时直接拒绝
  # （740），代码里的自动提权逻辑就没机会跑。改成 asInvoker + 启动时自检 + runas 重启。
  "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
)

target_link_libraries(ospt PRIVATE osp::core comctl32 comdlg32 shell32)

# WTL 10 头文件库（MS-PL，见 third_party/WTL/MANIFEST.json 与 third_party/LICENSES.md）。
# 依赖 ATL：VS 装了 Microsoft.VisualStudio.Component.VC.ATL 之后，ATL 的头与库
# 会随 VC 工具集一起进入包含/库搜索路径，不必在这里额外指。
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/WTL/Include)

# --ui-smoke-test 的 JSON 报告用 nlohmann/json（MIT）
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
