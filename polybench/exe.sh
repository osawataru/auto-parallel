#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd "${script_dir}/.." && pwd)
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${root_dir}/build-llvm22-dev"}
export LD_LIBRARY_PATH="${llvm22_build_dir}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# all
# correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm cholesky durbin gramschmidt lu ludcmp trisolv deriche floyd-warshall nussinov adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d

# openmpで並列化できないものを除く
# correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm durbin gramschmidt ludcmp deriche adi fdtd-2d heat-3d jacobi-1d jacobi-2d

#for benchmark in covariance doitgen mvt gemm gemver gesummv symm syr2k syrk trmm durbin gramschmidt ludcmp adi fdtd-2d heat-3d jacobi-1d jacobi-2d
for benchmark in 3mm
do
  echo ${benchmark}
  ./prog/hagane/src/${benchmark}.out 2> prog/hagane/out/${benchmark}.dump.txt
  #./prog/clevel/src/${benchmark}.out 2> prog/clevel/out/${benchmark}.dump.txt
  ./prog/seq/src/${benchmark}.out 2> prog/seq/out/${benchmark}.dump.txt
done
