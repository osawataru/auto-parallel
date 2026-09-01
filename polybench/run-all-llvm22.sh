#!/usr/bin/env bash

set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "${script_dir}/benchmarks-llvm22.sh"
result_dir=${LLVM22_RESULT_DIR:-"${script_dir}/prog/llvm22-no-omp"}
summary="${result_dir}/run-summary.tsv"

mkdir -p "${result_dir}"
printf 'benchmark\tstatus\tlog\n' > "${summary}"
passed=0
failed=0

for benchmark in "${benchmarks[@]}"; do
  work_dir="${result_dir}/${benchmark}"
  log="${work_dir}/run.log"
  mkdir -p "${work_dir}"
  echo "===== run: ${benchmark} ====="

  # tee keeps the complete result visible on screen and saves it for later.
  if "${script_dir}/run-llvm22.sh" "${benchmark}" 2>&1 | tee "${log}"; then
    printf '%s\tPASS\t%s\n' "${benchmark}" "${log}" >> "${summary}"
    passed=$((passed + 1))
  else
    status=${PIPESTATUS[0]}
    printf '%s\tFAIL(%s)\t%s\n' "${benchmark}" "${status}" "${log}" >> "${summary}"
    failed=$((failed + 1))
  fi
done

echo "run PASS: ${passed}"
echo "run FAIL: ${failed}"
echo "summary: ${summary}"
(( failed == 0 ))
