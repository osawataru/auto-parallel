#!/bin/bash
BUILD_DIR=$1
OUT_DIR=$2
mkdir -p ${OUT_DIR}
COMPILER_NAMES=("gcc" "clang" "gcc-o2" "clang-o2" "gcc-of" "clang-of")
for compiler in ${COMPILER_NAMES[@]}
do
  COMPILER_BIN_DIR=${BUILD_DIR}/${compiler}/bin
  if [ ! -d "${COMPILER_BIN_DIR}" ]; then
    continue
  fi
for entry in "${COMPILER_BIN_DIR}"/*
do
  [ -e "${entry}" ] || continue
  NAME=`basename "${entry}"`-${compiler}
  cp "${entry}" "${OUT_DIR}/${NAME}"
done
done
