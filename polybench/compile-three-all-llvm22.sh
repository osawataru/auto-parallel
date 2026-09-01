#!/usr/bin/env bash

set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "${script_dir}/benchmarks-llvm22.sh"
result_dir=${THREE_RESULT_DIR:-"${script_dir}/prog/llvm22-three"}
summary="${result_dir}/compile-summary.tsv"
mkdir -p "${result_dir}"
printf 'benchmark\tstatus\tlog\n' > "${summary}"

passed=0
failed=0
for benchmark in "${benchmarks[@]}"; do
  mkdir -p "${result_dir}/${benchmark}"
  log="${result_dir}/${benchmark}/compile-three.log"
  if "${script_dir}/compile-three-llvm22.sh" "${benchmark}" 2>&1 | tee "${log}"; then
    printf '%s\tPASS\t%s\n' "${benchmark}" "${log}" >> "${summary}"
    passed=$((passed + 1))
  else
    status=${PIPESTATUS[0]}
    printf '%s\tFAIL(%s)\t%s\n' "${benchmark}" "${status}" "${log}" >> "${summary}"
    failed=$((failed + 1))
  fi
done
echo "compile PASS: ${passed}"
echo "compile FAIL: ${failed}"
echo "summary: ${summary}"
(( failed == 0 ))
