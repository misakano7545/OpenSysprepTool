# deploy —— 部署运行时 osptd.exe
#
# 子系统保持 CONSOLE（部署时要在命令行看单页进度）。
# 清单保持 asInvoker，真实执行时由 main() 自检并用 runas 提权；dry-run 不需要提权。
add_executable(osptd
  main.cpp
  # 单页进度页：Win32 控制台原地重绘（Win7 的 conhost 不认 ANSI/VT）
  progress.cpp
)

target_link_options(osptd PRIVATE
  "/SUBSYSTEM:CONSOLE,6.01"
  # 同 ospt.exe：清单保持 asInvoker，启动时自检 + runas 重启提权
  "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
)

target_link_libraries(osptd PRIVATE osp::core)

# --dry-run 的机器可读报告与 --win7-compat 用 nlohmann/json（MIT，见 third_party/LICENSES.md）
target_include_directories(osptd PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
