cmake_minimum_required(VERSION 3.1.0)

add_executable(tinyprof
  tinyprof.cpp
  )

target_link_libraries(tinyprof
  clangBasic
  clangTooling
  clangASTMatchers
  clangAST
  clangToolingCore
  clangFrontend
  LLVMSupport
  )

target_compile_features(tinyprof PRIVATE cxx_range_for)

add_definitions(-D__STDC_LIMIT_MACROS)
add_definitions(-D__STDC_CONSTANT_MACROS)

set(CMAKE_CXX_FLAGS "-fno-rtti")
set(CMAKE_CXX_FLAGS "-fno-rtti")
