cmake_minimum_required(VERSION 3.20)

project(AutoParallelPlugin LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)

if(NOT LLVM_VERSION_MAJOR EQUAL 22)
  message(FATAL_ERROR
    "AutoParallelPlugin requires LLVM 22, but found ${LLVM_PACKAGE_VERSION}")
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

add_llvm_library(AutoParallelLegacy MODULE
  lib/DependencyCheck.cpp
  lib/ReductionDetect.cpp
  lib/AllPrivateDetect.cpp
  lib/DirectiveInsertion.cpp
  lib/ParamGet.cpp
  PLUGIN_TOOL
  opt
)

target_include_directories(AutoParallelLegacy PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  ${LLVM_INCLUDE_DIRS}
)

target_compile_features(AutoParallelLegacy PRIVATE cxx_std_17)

add_llvm_executable(auto-parallel-legacy-driver
  tools/LegacyDriver.cpp
  lib/DependencyCheck.cpp
  lib/ReductionDetect.cpp
  lib/AllPrivateDetect.cpp
  lib/DirectiveInsertion.cpp
  lib/ParamGet.cpp
)

target_include_directories(auto-parallel-legacy-driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  ${LLVM_INCLUDE_DIRS}
)

target_compile_features(auto-parallel-legacy-driver PRIVATE cxx_std_17)
