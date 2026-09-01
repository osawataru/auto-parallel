#!/usr/bin/env bash

# PolyBench/C 4.2 benchmark names used by the LLVM 22 validation scripts.
benchmarks=(
  correlation covariance
  2mm 3mm atax bicg doitgen mvt
  gemm gemver gesummv symm syr2k syrk trmm
  cholesky durbin gramschmidt lu ludcmp trisolv
  deriche floyd-warshall nussinov
  adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d
)
