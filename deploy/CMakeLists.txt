# deploy —— 部署运行时 osptd.exe
add_executable(osptd
  main.cpp
  # 单页进度页：Win32 控制台原地重绘（Win7 的 conhost 不认 ANSI/VT）
  progress.cpp
)

target_link_libraries(osptd PRIVATE osp::core)

# --dry-run 的机器可读报告与 --win7-compat 用 nlohmann/json（MIT，见 third_party/LICENSES.md）
target_include_directories(osptd PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
