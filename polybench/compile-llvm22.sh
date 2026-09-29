#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/.." && pwd)
polybench_dir=${POLYBENCH_DIR:-"${script_dir}/polybench_no_omp"}
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${project_dir}/build-llvm22-dev"}
clang_bin=${CLANG_BIN:-"${llvm22_build_dir}/bin/clang"}
opt_bin=${OPT_BIN:-"${llvm22_build_dir}/bin/opt"}
result_dir=${LLVM22_RESULT_DIR:-"${script_dir}/prog/llvm22-no-omp"}
work_dir="${result_dir}/${benchmark}"
omp_include_dir=${OMP_INCLUDE_DIR:-"${llvm22_build_dir}/projects/openmp/runtime/src"}
omp_library_dir=${OMP_LIBRARY_DIR:-"${llvm22_build_dir}/lib"}

source_file=$(find "${polybench_dir}" -type f -path "*/${benchmark}/${benchmark}.c" -print -quit)
if [[ -z "${source_file}" ]]; then
  echo "benchmark source not found: ${benchmark}" >&2
  exit 1
fi
for tool in "${clang_bin}" "${opt_bin}"; do
  if [[ ! -x "${tool}" ]]; then
    echo "required LLVM 22 tool not found: ${tool}" >&2
    echo "build it with: make -C ${llvm22_build_dir} clang opt omp -j\$(nproc)" >&2
    exit 1
  fi
done
if [[ ! -f "${omp_library_dir}/libomp.so" ]]; then
  echo "LLVM 22 libomp not found: ${omp_library_dir}/libomp.so" >&2
  echo "build it with: make -C ${llvm22_build_dir} omp -j\$(nproc)" >&2
  exit 1
fi

mkdir -p "${work_dir}"
common_defines=()
if ! grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+POLYBENCH_TIME\b' \
    "${polybench_dir}/utilities/polybench.h"; then
  common_defines+=(-DPOLYBENCH_TIME)
fi
if ! grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+POLYBENCH_DUMP_ARRAYS\b' \
    "${polybench_dir}/utilities/polybench.h"; then
  common_defines+=(-DPOLYBENCH_DUMP_ARRAYS)
fi
common_includes=(-I "${polybench_dir}/utilities")

echo "[1/5] Generate LLVM IR: ${benchmark}"
"${clang_bin}" -S -emit-llvm -Xclang -disable-O0-optnone \
  "${common_includes[@]}" "${common_defines[@]}" \
  "${source_file}" -o "${work_dir}/${benchmark}.ll"

echo "[2/5] Normalize IR"
"${opt_bin}" -passes='mem2reg,early-cse,loop-mssa(licm)' -S \
  "${work_dir}/${benchmark}.ll" -o "${work_dir}/${benchmark}.normalized.ll"

echo "[3/5] Insert auto-parallel directive"
"${opt_bin}" -bugpoint-enable-legacy-pm -directiveinsertion \
  -ditarget=kernel -S \
  "${work_dir}/${benchmark}.normalized.ll" \
  -o "${work_dir}/${benchmark}.directive.ll" 2> "${work_dir}/directive.log"

echo "[4/5] Generate OpenMP runtime IR"
"${opt_bin}" -bugpoint-enable-legacy-pm -paramget -S \
  "${work_dir}/${benchmark}.directive.ll" \
  -o "${work_dir}/${benchmark}.parallel.ll" 2> "${work_dir}/paramget.log"
"${opt_bin}" -passes=verify -disable-output \
  "${work_dir}/${benchmark}.parallel.ll"

echo "[5/5] Build sequential and transformed executables"
"${clang_bin}" "${common_includes[@]}" "${common_defines[@]}" \
  "${polybench_dir}/utilities/polybench.c" \
  "${work_dir}/${benchmark}.normalized.ll" -lm \
  -o "${work_dir}/${benchmark}.sequential"
"${clang_bin}" "${common_includes[@]}" "${common_defines[@]}" -fopenmp \
  -I "${omp_include_dir}" -L "${omp_library_dir}" \
  -Wl,-rpath,"${omp_library_dir}" \
  "${polybench_dir}/utilities/polybench.c" \
  "${work_dir}/${benchmark}.parallel.ll" -lm \
  -o "${work_dir}/${benchmark}.parallel"

echo "compiled: ${work_dir}"
