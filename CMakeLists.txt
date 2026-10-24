cmake_minimum_required(VERSION 3.25)

project(OpenSysprepTool
  VERSION 0.1.0
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
