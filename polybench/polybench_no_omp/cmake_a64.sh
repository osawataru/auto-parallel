#!/bin/bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/../.." && pwd)
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${project_dir}/build-llvm22-dev"}
clang_bin=${CLANG_BIN:-"${llvm22_build_dir}/bin/clang"}
clangxx_bin=${CLANGXX_BIN:-"${llvm22_build_dir}/bin/clang++"}

configure() {
  local build_dir=$1
  shift

  mkdir -p "${build_dir}"
  # The workspace persists when the Dev Container architecture changes.
  # Always refresh so cached host architecture and sysroot values cannot leak
  # from an amd64 container into an arm64 build (or vice versa).
  cmake --fresh -G "Unix Makefiles" -S . -B "${build_dir}" "$@"
}

configure build/gcc -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
configure build/clang -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${clang_bin}" -DCMAKE_CXX_COMPILER="${clangxx_bin}"
configure build/gcc-o2 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
configure build/clang-o2 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="${clang_bin}" -DCMAKE_CXX_COMPILER="${clangxx_bin}"
#mkdir -p build/gcc-of && cmake -S. -Bbuild/gcc-of -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
#mkdir -p build/clang-of && cmake -S. -Bbuild/clang-of -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
