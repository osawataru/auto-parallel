#!/bin/bash
IN_DIR=$1
OUT_DIR=$2
mkdir -p ${OUT_DIR}
OBJDUMP=${OBJDUMP:-llvm-objdump}
for entry in ${IN_DIR}/*
do
  NAME=`basename "$entry"`
  # OLD: host objdump may not recognize AArch64 binaries.
  # objdump -d ${entry} > ${OUT_DIR}/${NAME}.asm
  ${OBJDUMP} -d ${entry} > ${OUT_DIR}/${NAME}.asm
done
