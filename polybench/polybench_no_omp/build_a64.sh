#!/bin/bash
set -euo pipefail

BUILD_DIR=build
mkdir -p "${BUILD_DIR}"
bash cmake_a64.sh
make -C "${BUILD_DIR}/gcc" -j"$(nproc)"
make -C "${BUILD_DIR}/gcc-o2" -j"$(nproc)"
#pushd ${BUILD_DIR}/gcc-of && make -j && popd
make -C "${BUILD_DIR}/clang" -j"$(nproc)"
make -C "${BUILD_DIR}/clang-o2" -j"$(nproc)"
#pushd ${BUILD_DIR}/clang-of && make -j && popd
bash copy_bin.sh "${BUILD_DIR}" "${BUILD_DIR}/bin"
#bash disass_dir.sh ${BUILD_DIR}/bin ${BUILD_DIR}/asm
