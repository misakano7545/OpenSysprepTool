# host —— 主程序 ospt.exe
#
# 注意子系统：仍然是 CONSOLE（顶层 CMakeLists 的 /SUBSYSTEM:CONSOLE,6.01）。
# 窗口在运行期创建，CLI 与 --ui-smoke-test 的输出照常打到 stdout。
# 这样既避开宪章 §10 记的「GUI 子系统与 6.01 冲突」，也保住 M5 那条
# 「产物子系统版本必须是 6.1」的闸门。

add_executable(ospt
  main.cpp
  # M7a：主界面框架与配置页（原生控件 + WTL 薄封装）
  ui/ui_frame.cpp
  ui/ui_pages.cpp
)

target_link_libraries(ospt PRIVATE osp::core)

# WTL 10 头文件库（MS-PL，见 third_party/WTL/MANIFEST.json 与 third_party/LICENSES.md）。
# 依赖 ATL：VS 装了 Microsoft.VisualStudio.Component.VC.ATL 之后，ATL 的头与库
# 会随 VC 工具集一起进入包含/库搜索路径，不必在这里额外指。
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/WTL/Include)

# --ui-smoke-test 的 JSON 报告用 nlohmann/json（MIT）
target_include_directories(ospt PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
