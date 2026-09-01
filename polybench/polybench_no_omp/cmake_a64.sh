#!/bin/bash

configure() {
  local build_dir=$1
  shift

  mkdir -p "${build_dir}"
  # The workspace persists when the Dev Container architecture changes.
  # Always refresh so cached host architecture and sysroot values cannot leak
  # from an amd64 container into an arm64 build (or vice versa).
  cmake --fresh -S . -B "${build_dir}" "$@"
}

configure build/gcc -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
configure build/clang -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
configure build/gcc-o2 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
configure build/clang-o2 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
#mkdir -p build/gcc-of && cmake -S. -Bbuild/gcc-of -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
#mkdir -p build/clang-of && cmake -S. -Bbuild/clang-of -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
