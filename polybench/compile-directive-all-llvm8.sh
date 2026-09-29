#!/usr/bin/env bash

set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "${script_dir}/benchmarks-llvm22.sh"
result_dir=${LLVM8_RESULT_DIR:-"${script_dir}/prog/llvm8"}
summary="${result_dir}/directive-summary.tsv"

mkdir -p "${result_dir}"
printf 'benchmark\tstatus\tlog\n' > "${summary}"
passed=0
failed=0

for benchmark in "${benchmarks[@]}"; do
  work_dir="${result_dir}/${benchmark}"
  log="${work_dir}/compile-directive.log"
  mkdir -p "${work_dir}"
  if "${script_dir}/compile-directive-llvm8.sh" "${benchmark}" \
      > "${log}" 2>&1; then
    printf '%s\tPASS\t%s\n' "${benchmark}" "${log}" >> "${summary}"
    passed=$((passed + 1))
  else
    status=$?
    printf '%s\tFAIL(%s)\t%s\n' "${benchmark}" "${status}" "${log}" >> "${summary}"
    failed=$((failed + 1))
  fi
done

echo "LLVM 8 directive PASS: ${passed}"
echo "LLVM 8 directive FAIL: ${failed}"
echo "summary: ${summary}"
(( failed == 0 ))
