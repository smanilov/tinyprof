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
# clang++ -fno-rtti -std=c++11 -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS main.cpp -lclangBasic -lclangAST -lclangTooling -lLLVMSupport -lclangASTMatchers -lclangFrontend -lclangToolingCore

set(CMAKE_CXX_FLAGS "-fno-rtti")
