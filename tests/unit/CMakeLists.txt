# 单元测试：doctest 单头文件（BSL-1.0）
add_executable(osp_unit_tests
  doctest_main.cpp
  test_version.cpp
  test_config.cpp
  test_sysinfo.cpp
  test_unattend.cpp
  test_runplan.cpp
  test_win7.cpp
  test_deploy.cpp
  test_dism.cpp
  test_srsinject.cpp
  test_sysprep.cpp
  test_ui.cpp
)

# 界面数据层（页面与字段）被单测直接覆盖：它不依赖窗口，也不依赖 ATL/WTL，
# 所以能编进测试目标而不必把 ATL 拖进来。窗口那一层由 --ui-smoke-test 负责。
target_sources(osp_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/host/ui/ui_pages.cpp)
target_include_directories(osp_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/host/ui)

target_link_libraries(osp_unit_tests PRIVATE osp::core)
target_include_directories(osp_unit_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party
  ${CMAKE_SOURCE_DIR}/third_party/pugixml
)

add_test(NAME unit COMMAND osp_unit_tests)
