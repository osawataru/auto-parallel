#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/../.." && pwd)
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${project_dir}/build-llvm22-dev"}
clang_bin=${CLANG_BIN:-"${llvm22_build_dir}/bin/clang"}
clangxx_bin=${CLANGXX_BIN:-"${llvm22_build_dir}/bin/clang++"}

cmake --fresh -G "Unix Makefiles" -S . -B build/gcc -DCMAKE_BUILD_TYPE=Debug
cmake --fresh -G "Unix Makefiles" -S . -B build/clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${clang_bin}" -DCMAKE_CXX_COMPILER="${clangxx_bin}"
cmake --fresh -G "Unix Makefiles" -S . -B build/gcc-o2 -DCMAKE_BUILD_TYPE=Release
cmake --fresh -G "Unix Makefiles" -S . -B build/clang-o2 -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="${clang_bin}" -DCMAKE_CXX_COMPILER="${clangxx_bin}"
cmake --fresh -G "Unix Makefiles" -S . -B build/gcc-of -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON
cmake --fresh -G "Unix Makefiles" -S . -B build/clang-of -DCMAKE_BUILD_TYPE=Release -DOPTIMIZE_FAST=ON -DCMAKE_C_COMPILER="${clang_bin}" -DCMAKE_CXX_COMPILER="${clangxx_bin}"
