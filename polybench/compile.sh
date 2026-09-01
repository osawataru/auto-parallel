#!/bin/bash

set -e

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd "${script_dir}/.." && pwd)
build_dir="${root_dir}/build"
polybench_dir="${script_dir}/polybench-c-4.2"
output_dir="${script_dir}/prog"
omp_include_dir="${root_dir}/llvm-project-8.0.0.src/openmp/runtime/exports/common.dia.50.ompt.optional/include"

mkdir -p \
  "${output_dir}/hagane/src" \
  "${output_dir}/hagane/log" \
  "${output_dir}/hagane/out" \
  "${output_dir}/clevel/src" \
  "${output_dir}/clevel/out" \
  "${output_dir}/seq/src" \
  "${output_dir}/seq/out"

#for benchmark in bicg
for benchmark in correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm cholesky durbin gramschmidt lu ludcmp trisolv deriche floyd-warshall nussinov adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d
do
  src_dir=${benchmark}
  if [ "$benchmark" = "correlation" ] \
    || [ "$benchmark" = "covariance" ] ; then
    src_dir=datamining/${src_dir}
  elif [ "$benchmark" = "gemm" ] \
    || [ "$benchmark" = "gemver" ] \
    || [ "$benchmark" = "gesummv" ] \
    || [ "$benchmark" = "symm" ]\
    || [ "$benchmark" = "syr2k" ] \
    || [ "$benchmark" = "syrk" ]\
    || [ "$benchmark" = "trmm" ] ; then
    src_dir=linear-algebra/blas/${src_dir}
  elif [ "$benchmark" = "2mm" ] \
    || [ "$benchmark" = "3mm" ] \
    || [ "$benchmark" = "atax" ] \
    || [ "$benchmark" = "bicg" ] \
    || [ "$benchmark" = "doitgen" ] \
    || [ "$benchmark" = "mvt" ] ; then
    src_dir=linear-algebra/kernels/${src_dir}
  elif [ "$benchmark" = "cholesky" ] \
    || [ "$benchmark" = "durbin" ] \
    || [ "$benchmark" = "gramschmidt" ]\
    || [ "$benchmark" = "lu" ] \
    || [ "$benchmark" = "ludcmp" ]\
    || [ "$benchmark" = "trisolv" ] ; then
    src_dir=linear-algebra/solvers/${src_dir}
  elif [ "$benchmark" = "deriche" ] \
    || [ "$benchmark" = "floyd-warshall" ]\
    || [ "$benchmark" = "nussinov" ]  ; then
    src_dir=medley/${src_dir}
  elif [ "$benchmark" = "adi" ]\
    || [ "$benchmark" = "fdtd-2d" ] \
    || [ "$benchmark" = "heat-3d" ] \
    || [ "$benchmark" = "jacobi-1d" ] \
    || [ "$benchmark" = "jacobi-2d" ] \
    || [ "$benchmark" = "seidel-2d" ] ; then
    src_dir=stencils/${src_dir}
  fi

  # hagane
  "${build_dir}/bin/clang" -S -emit-llvm -Xclang -disable-O0-optnone -I "${polybench_dir}/utilities/" -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/hagane/src/${benchmark}.ll" "${polybench_dir}/${src_dir}/${benchmark}.c"
  "${build_dir}/bin/opt" -licm -mem2reg -early-cse -S -o "${output_dir}/hagane/src/${benchmark}.licm.ll" "${output_dir}/hagane/src/${benchmark}.ll"
  "${build_dir}/bin/opt" -S -load "${build_dir}/lib/LLVMDirectiveInsertion.so" -directiveinsertion -ditarget kernel -o "${output_dir}/hagane/src/${benchmark}.directive.ll" "${output_dir}/hagane/src/${benchmark}.licm.ll" 2> "${output_dir}/hagane/log/${benchmark}.txt"
  "${build_dir}/bin/opt" -paramget -S -o "${output_dir}/hagane/src/${benchmark}.paramget.ll" "${output_dir}/hagane/src/${benchmark}.directive.ll"
  "${build_dir}/bin/clang" -fopenmp -I "${omp_include_dir}" -L "${build_dir}/lib" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/hagane/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${output_dir}/hagane/src/${benchmark}.paramget.ll"

  # openmp(ソースコードレベルの並列化)
  "${build_dir}/bin/clang" -fopenmp -I "${omp_include_dir}" -I "${polybench_dir}/utilities/" -L "${build_dir}/lib" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/clevel/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${polybench_dir}/${src_dir}/${benchmark}.c"
  
  # 逐次
  "${build_dir}/bin/clang" -S -emit-llvm -Xclang -disable-O0-optnone -I "${polybench_dir}/utilities/" -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/seq/src/${benchmark}.ll" "${polybench_dir}/${src_dir}/${benchmark}.c"
  "${build_dir}/bin/opt" -licm -mem2reg -early-cse -S -o "${output_dir}/seq/src/${benchmark}.licm.ll" "${output_dir}/seq/src/${benchmark}.ll"
  "${build_dir}/bin/clang" -I "${polybench_dir}/utilities/" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/seq/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${output_dir}/seq/src/${benchmark}.licm.ll"
done
