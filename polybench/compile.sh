#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root_dir=$(cd "${script_dir}/.." && pwd)
build_dir=${LLVM22_BUILD_DIR:-"${root_dir}/build-llvm22-dev"}
clang_bin=${CLANG_BIN:-"${build_dir}/bin/clang"}
opt_bin=${OPT_BIN:-"${build_dir}/bin/opt"}
polybench_dir="${script_dir}/polybench-c-4.2"
output_dir="${script_dir}/prog"
omp_include_dir=${OMP_INCLUDE_DIR:-"${build_dir}/projects/openmp/runtime/src"}
omp_library_dir=${OMP_LIBRARY_DIR:-"${build_dir}/lib"}

for tool in "${clang_bin}" "${opt_bin}"; do
  if [[ ! -x "${tool}" ]]; then
    echo "required LLVM 22 tool not found: ${tool}" >&2
    echo "build it with: make -C ${build_dir} clang opt omp -j\$(nproc)" >&2
    exit 1
  fi
done
if [[ ! -f "${omp_library_dir}/libomp.so" ]]; then
  echo "LLVM 22 libomp not found: ${omp_library_dir}/libomp.so" >&2
  echo "build it with: make -C ${build_dir} omp -j\$(nproc)" >&2
  exit 1
fi

mkdir -p \
  "${output_dir}/hagane/src" \
  "${output_dir}/hagane/log" \
  "${output_dir}/hagane/out" \
  "${output_dir}/clevel/src" \
  "${output_dir}/clevel/out" \
  "${output_dir}/seq/src" \
  "${output_dir}/seq/out"

#for benchmark in bicg
benchmarks=(correlation covariance 2mm 3mm atax bicg doitgen mvt gemm gemver gesummv symm syr2k syrk trmm cholesky durbin gramschmidt lu ludcmp trisolv deriche floyd-warshall nussinov adi fdtd-2d heat-3d jacobi-1d jacobi-2d seidel-2d)
if (( $# > 0 )); then
  benchmarks=("$@")
fi

for benchmark in "${benchmarks[@]}"
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
  "${clang_bin}" -S -emit-llvm -Xclang -disable-O0-optnone -I "${polybench_dir}/utilities/" -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/hagane/src/${benchmark}.ll" "${polybench_dir}/${src_dir}/${benchmark}.c"
  "${opt_bin}" -passes='mem2reg,early-cse,loop-mssa(licm)' -S -o "${output_dir}/hagane/src/${benchmark}.licm.ll" "${output_dir}/hagane/src/${benchmark}.ll"
  "${opt_bin}" -bugpoint-enable-legacy-pm -directiveinsertion -ditarget=kernel -S -o "${output_dir}/hagane/src/${benchmark}.directive.ll" "${output_dir}/hagane/src/${benchmark}.licm.ll" 2> "${output_dir}/hagane/log/${benchmark}.txt"
  "${opt_bin}" -bugpoint-enable-legacy-pm -paramget -S -o "${output_dir}/hagane/src/${benchmark}.paramget.ll" "${output_dir}/hagane/src/${benchmark}.directive.ll"
  "${opt_bin}" -passes=verify -disable-output "${output_dir}/hagane/src/${benchmark}.paramget.ll"
  "${clang_bin}" -fopenmp -I "${omp_include_dir}" -L "${omp_library_dir}" -Wl,-rpath,"${omp_library_dir}" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/hagane/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${output_dir}/hagane/src/${benchmark}.paramget.ll"

  # openmp(ソースコードレベルの並列化)
  "${clang_bin}" -fopenmp -I "${omp_include_dir}" -I "${polybench_dir}/utilities/" -L "${omp_library_dir}" -Wl,-rpath,"${omp_library_dir}" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/clevel/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${polybench_dir}/${src_dir}/${benchmark}.c"
  
  # 逐次
  "${clang_bin}" -S -emit-llvm -Xclang -disable-O0-optnone -I "${polybench_dir}/utilities/" -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/seq/src/${benchmark}.ll" "${polybench_dir}/${src_dir}/${benchmark}.c"
  "${opt_bin}" -passes='mem2reg,early-cse,loop-mssa(licm)' -S -o "${output_dir}/seq/src/${benchmark}.licm.ll" "${output_dir}/seq/src/${benchmark}.ll"
  "${clang_bin}" -I "${polybench_dir}/utilities/" -lm -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS -o "${output_dir}/seq/src/${benchmark}.out" "${polybench_dir}/utilities/polybench.c" "${output_dir}/seq/src/${benchmark}.licm.ll"
done
