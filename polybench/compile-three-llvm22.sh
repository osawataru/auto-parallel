#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/.." && pwd)
sequential_source_dir=${SEQUENTIAL_SOURCE_DIR:-"${script_dir}/polybench_no_omp"}
manual_source_dir=${MANUAL_SOURCE_DIR:-"${script_dir}/polybench-c-4.2"}
result_dir=${THREE_RESULT_DIR:-"${script_dir}/prog/llvm22-three"}
work_dir="${result_dir}/${benchmark}"
llvm22_build_dir=${LLVM22_BUILD_DIR:-"${project_dir}/build-llvm22-dev"}
clang_bin=${CLANG_BIN:-"${llvm22_build_dir}/bin/clang"}
omp_include_dir=${OMP_INCLUDE_DIR:-"${llvm22_build_dir}/projects/openmp/runtime/src"}
omp_library_dir=${OMP_LIBRARY_DIR:-"${llvm22_build_dir}/lib"}

manual_source=$(find "${manual_source_dir}" -type f \
  -path "*/${benchmark}/${benchmark}.c" -print -quit)
if [[ -z "${manual_source}" ]]; then
  echo "manual OpenMP source not found: ${benchmark}" >&2
  exit 1
fi

mkdir -p "${work_dir}"

if [[ ! -x "${clang_bin}" ]]; then
  echo "LLVM 22 clang not found: ${clang_bin}" >&2
  echo "build it with: make -C ${llvm22_build_dir} clang -j\$(nproc)" >&2
  exit 1
fi
if [[ ! -f "${omp_library_dir}/libomp.so" ]]; then
  echo "LLVM 22 libomp not found: ${omp_library_dir}/libomp.so" >&2
  echo "build it with: make -C ${llvm22_build_dir} omp -j\$(nproc)" >&2
  exit 1
fi

echo "===== sequential and automatic: ${benchmark} ====="
POLYBENCH_DIR="${sequential_source_dir}" LLVM22_RESULT_DIR="${result_dir}" \
  "${script_dir}/compile-llvm22.sh" "${benchmark}"

manual_defines=()
if ! grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+POLYBENCH_TIME\b' \
    "${manual_source_dir}/utilities/polybench.h"; then
  manual_defines+=(-DPOLYBENCH_TIME)
fi
if ! grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+POLYBENCH_DUMP_ARRAYS\b' \
    "${manual_source_dir}/utilities/polybench.h"; then
  manual_defines+=(-DPOLYBENCH_DUMP_ARRAYS)
fi

echo "===== manual OpenMP: ${benchmark} ====="
"${clang_bin}" -fopenmp "${manual_defines[@]}" \
  -I "${manual_source_dir}/utilities" \
  -I "${omp_include_dir}" -L "${omp_library_dir}" \
  -Wl,-rpath,"${omp_library_dir}" \
  "${manual_source_dir}/utilities/polybench.c" \
  "${manual_source}" -lm -o "${work_dir}/${benchmark}.manual"

echo "compiled three variants: ${work_dir}"
