# host —— 主程序 ospt.exe
add_executable(ospt
  main.cpp
)

target_link_libraries(ospt PRIVATE osp::core)
