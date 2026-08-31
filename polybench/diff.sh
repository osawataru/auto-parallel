#!/bin/bash

echo "" >> diff.txt

# all
# correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm cholesky durbin gramschmidt lu ludcmp trisolv deriche floyd-warshall nussinov adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d

# openmpで並列化できないものを除く
# correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm durbin gramschmidt ludcmp deriche adi fdtd-2d heat-3d jacobi-1d jacobi-2d

for benchmark in correlation 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm durbin ludcmp deriche adi fdtd-2d heat-3d jacobi-1d jacobi-2d
do
  echo ${benchmark} >> diff.txt
  diff prog/hagane/out/${benchmark}.dump.txt prog/seq/out/${benchmark}.dump.txt >> diff.txt
done