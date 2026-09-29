#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd "${script_dir}/.." && pwd)
polybench_dir=${POLYBENCH_DIR:-"${script_dir}/polybench_no_omp"}
llvm8_build_dir=${LLVM8_BUILD_DIR:-"${project_dir}/build"}
clang_bin=${LLVM8_CLANG_BIN:-"${llvm8_build_dir}/bin/clang"}
opt_bin=${LLVM8_OPT_BIN:-"${llvm8_build_dir}/bin/opt"}
directive_plugin=${LLVM8_DIRECTIVE_PLUGIN:-"${llvm8_build_dir}/lib/LLVMDirectiveInsertion.so"}
result_dir=${LLVM8_RESULT_DIR:-"${script_dir}/prog/llvm8"}
work_dir="${result_dir}/${benchmark}"
compat_include_dir=${LLVM8_COMPAT_INCLUDE_DIR:-"${script_dir}/llvm8-compat-include"}

source_file=$(find "${polybench_dir}" -type f \
  -path "*/${benchmark}/${benchmark}.c" -print -quit)
if [[ -z "${source_file}" ]]; then
  echo "benchmark source not found: ${benchmark}" >&2
  exit 1
fi
for required in "${clang_bin}" "${opt_bin}" "${directive_plugin}"; do
  if [[ ! -e "${required}" ]]; then
    echo "required LLVM 8 file not found: ${required}" >&2
    exit 1
  fi
done

mkdir -p "${work_dir}"
common_flags=(-I "${compat_include_dir}" -I "${polybench_dir}/utilities"
  -DPOLYBENCH_TIME -DPOLYBENCH_DUMP_ARRAYS)

echo "[1/3] Generate LLVM 8 IR: ${benchmark}"
"${clang_bin}" -S -emit-llvm -Xclang -disable-O0-optnone \
  "${common_flags[@]}" "${source_file}" -o "${work_dir}/${benchmark}.ll"

echo "[2/3] Normalize LLVM 8 IR"
"${opt_bin}" -licm -early-cse -mem2reg -S \
  "${work_dir}/${benchmark}.ll" -o "${work_dir}/${benchmark}.normalized.ll"

echo "[3/3] Insert LLVM 8 directive"
"${opt_bin}" -load "${directive_plugin}" -directiveinsertion \
  -ditarget=kernel -S "${work_dir}/${benchmark}.normalized.ll" \
  -o "${work_dir}/${benchmark}.directive.ll" 2> "${work_dir}/directive.log"

echo "generated: ${work_dir}/${benchmark}.directive.ll"
