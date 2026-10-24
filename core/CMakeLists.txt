# core —— 静态库，ospt 与 osptd 共用
add_library(osp_core STATIC
  src/version.cpp
  config/config.cpp
  log/log.cpp
  sysinfo/sysinfo.cpp
  unattend/unattend.cpp
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
target_link_libraries(osp_core PRIVATE wbemuuid ole32 oleaut32)
