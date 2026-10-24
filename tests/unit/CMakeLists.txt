# 单元测试：doctest 单头文件（BSL-1.0）
add_executable(osp_unit_tests
  doctest_main.cpp
  test_version.cpp
  test_config.cpp
  test_sysinfo.cpp
)

target_link_libraries(osp_unit_tests PRIVATE osp::core)
target_include_directories(osp_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

add_test(NAME unit COMMAND osp_unit_tests)
