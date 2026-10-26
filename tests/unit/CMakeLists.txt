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
  test_regio.cpp
  test_prepare.cpp
  test_oem.cpp
  test_envcheck.cpp
  test_netcfg.cpp
  test_md5.cpp
  test_deploy_preflight.cpp
  test_taskbar.cpp
  test_pppoe.cpp
  test_oem_wallpaper.cpp
  test_ui_layout.cpp
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

# 测试跑在控制台，且**不提权**：测试目标要能在任何会话（含 CI 的非提权步骤）里直接跑，
# 所以显式钉 asInvoker，避免误继承产品那边的 requireAdministrator。
target_link_options(osp_unit_tests PRIVATE
  "/SUBSYSTEM:CONSOLE,6.01"
  "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
)

add_test(NAME unit COMMAND osp_unit_tests)
