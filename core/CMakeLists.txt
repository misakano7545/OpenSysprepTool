# core —— 静态库，ospt 与 osptd 共用
add_library(osp_core STATIC
  src/version.cpp
  config/config.cpp
  config/ini.cpp
  log/log.cpp
  sysinfo/sysinfo.cpp
  sysinfo/tweakscheck.cpp
  sysinfo/envcheck.cpp
  runplan/runplan.cpp
  unattend/unattend.cpp
  # M5 新增：Windows 7 兼容层与部署运行时内核
  win7/win7compat.cpp
  deploy/deploy.cpp
  # M6 新增：驱动注入（两种模式）与 sysprep 编排
  dism/dism.cpp
  srsinject/srsinject.cpp
  sysprep/sysprep.cpp
  # M9 新增：注册表 RAII 与封装前设置应用
  regio/regio.cpp
  prepare/prepare.cpp
  # M10 新增：OEM 信息识别（读 BIOS 标识 + 导入 SC 的 nt6/nt5.ini）
  oem/oem.cpp
  # M10 新增：SC 自带的系统优化表（27 条原始数据）
  tweaks/tweaks.cpp
  # pugixml 以源码形式编入（MIT，见 third_party/LICENSES.md）
  ${CMAKE_SOURCE_DIR}/third_party/pugixml/pugixml.cpp
)
add_library(osp::core ALIAS osp_core)

target_include_directories(osp_core PUBLIC include)
# 第三方单头文件（nlohmann/json、pugixml）只在实现里用，不对外暴露
target_include_directories(osp_core PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party
  ${CMAKE_SOURCE_DIR}/third_party/pugixml
)
target_compile_features(osp_core PUBLIC cxx_std_20)

# 杀软状态走 WMI（root\SecurityCenter2），需要 COM 与 WMI 的运行库
# advapi32：《ProductName》要从注册表读（RegGetValueW，Vista+）
# netapi32：NetGetJoinInformation（体检的域加入检查）
target_link_libraries(osp_core PRIVATE wbemuuid ole32 oleaut32 advapi32 netapi32 iphlpapi ws2_32)
