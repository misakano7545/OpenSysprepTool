# deploy —— 部署运行时 osptd.exe
add_executable(osptd
  main.cpp
)

target_link_libraries(osptd PRIVATE osp::core)
