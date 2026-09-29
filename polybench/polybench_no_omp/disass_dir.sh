#!/bin/bash
IN_DIR=$1
OUT_DIR=$2
mkdir -p ${OUT_DIR}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/../.." && pwd)
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${project_dir}/build-llvm22-dev"}
OBJDUMP=${OBJDUMP:-"${llvm22_build_dir}/bin/llvm-objdump"}
for entry in ${IN_DIR}/*
do
  NAME=`basename "$entry"`
  # OLD: host objdump may not recognize AArch64 binaries.
  # objdump -d ${entry} > ${OUT_DIR}/${NAME}.asm
  ${OBJDUMP} -d ${entry} > ${OUT_DIR}/${NAME}.asm
done
