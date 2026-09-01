#!/bin/bash
BUILD_DIR=build
mkdir -p ${BUILD_DIR}
bash cmake_native.sh
pushd ${BUILD_DIR}/gcc && make -j && popd
pushd ${BUILD_DIR}/gcc-o2 && make -j && popd
pushd ${BUILD_DIR}/gcc-of && make -j && popd
pushd ${BUILD_DIR}/clang && make -j && popd
pushd ${BUILD_DIR}/clang-o2 && make -j && popd
pushd ${BUILD_DIR}/clang-of && make -j && popd
bash copy_bin.sh ${BUILD_DIR} ${BUILD_DIR}/bin
bash disass_dir.sh ${BUILD_DIR}/bin ${BUILD_DIR}/asm