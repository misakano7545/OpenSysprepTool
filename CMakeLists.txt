cmake_minimum_required(VERSION 3.25)

project(OpenSysprepTool
  VERSION 0.1.1
  DESCRIPTION "SC 对标封装工具（去掉全部流氓行为）"
  LANGUAGES CXX
)

# ---------------------------------------------------------------- 语言标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------- 产物位置
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------- 编译选项
if(MSVC)
  # 宪章 1：静态 /MT，PE 里不能依赖 VC 运行库
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
  add_compile_definitions(_UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

  # ------------------------------------------------------------ Windows 7 SP1
  # 宪章 §8 M5.1 第一道闸：编译期拦住 Windows 8 及以上的 API。
  # 这三个宏必须在任何 Windows 头文件（尤其是 sdkddkver.h）之前生效，
  # 用 /D 传最稳妥 —— 命令行定义永远先于源码里的 #include。
  #
  # 定在 0x0601（Win7 SP1）后，SDK 头会按 Win7 的能力给声明：
  #   - Win8+ 独有 API 直接编译报错（第一道闸）
  #   - VS2022 STL 会走条件变量回退路径，不再静态导入 WaitOnAddress /
  #     WakeByAddressSingle / WakeByAddressAll（Win8+）
  #   - GetSystemTimePreciseAsFileTime 等不回退时也不可用
  add_compile_definitions(
    WINVER=0x0601
    _WIN32_WINNT=0x0601
    NTDDI_VERSION=0x06010000
  )

  # 第二道闸的可见前提：PE 的 MajorSubsystemVersion 必须是 6.1。
  # 默认由 SDK 决定（实测停在 6.0/Vista），显式钉死。
  # 这是链接器选项，不是编译定义 —— 见本文件末尾的边界说明。
  add_link_options("/SUBSYSTEM:CONSOLE,6.01")
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- 构建卫生
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "禁止在源码目录内构建，请用: cmake -S . -B build")
endif()

option(OSP_BUILD_TESTS "构建单元测试" ON)

add_subdirectory(core)
add_subdirectory(host)
add_subdirectory(deploy)

if(OSP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------- 摘要
message(STATUS "")
message(STATUS "OpenSysprepTool ${PROJECT_VERSION}")
message(STATUS "  compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  generator  : ${CMAKE_GENERATOR}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  tests      : ${OSP_BUILD_TESTS}")
message(STATUS "")
