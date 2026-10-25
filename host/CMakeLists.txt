# host —— 主程序 ospt.exe
#
# 子系统是 WINDOWS（不是 CONSOLE）：正常启动只出现配置界面，不再附带命令行窗口。
# /ENTRY:mainCRTStartup 让入口仍是标准的 main()，CLI 与 --ui-smoke-test 的输出
# 靠 main.cpp 里 attachParentConsoleIfNeeded() 回落到父控制台（仅当 stdout 不可用时）。
#
# manifest 声明 requireAdministrator：装驱动 / 跑 sysprep 都需要管理员权限。
# 代价是无法提权时 Windows 直接拒绝启动（CreateProcess 报 740），本机验收与 CI
# 因此需要在已提权的会话里跑 —— 见 tests/tools/run_acceptance_elevated.ps1。

add_executable(ospt
  main.cpp
  # M7a：主界面框架与配置页（原生控件 + WTL 薄封装）
  ui/ui_frame.cpp
  ui/ui_pages.cpp
)

target_link_options(ospt PRIVATE
  "/SUBSYSTEM:WINDOWS,6.01"
  "/ENTRY:mainCRTStartup"
  "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)

target_link_libraries(ospt PRIVATE osp::core comctl32 comdlg32)

# WTL 10 头文件库（MS-PL，见 third_party/WTL/MANIFEST.json 与 third_party/LICENSES.md）。
# 依赖 ATL：VS 装了 Microsoft.VisualStudio.Component.VC.ATL 之后，ATL 的头与库
# 会随 VC 工具集一起进入包含/库搜索路径，不必在这里额外指。
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/WTL/Include)

# --ui-smoke-test 的 JSON 报告用 nlohmann/json（MIT）
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
