#!/usr/bin/env bash

set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "${script_dir}/benchmarks-llvm22.sh"
result_dir=${THREE_RESULT_DIR:-"${script_dir}/prog/llvm22-three"}
summary="${result_dir}/run-summary.tsv"
mkdir -p "${result_dir}"
printf 'benchmark\tstatus\ttimings\tlog\n' > "${summary}"

passed=0
failed=0
for benchmark in "${benchmarks[@]}"; do
  mkdir -p "${result_dir}/${benchmark}"
  log="${result_dir}/${benchmark}/run-three.log"
  echo "===== run three: ${benchmark} ====="
  if "${script_dir}/run-three-llvm22.sh" "${benchmark}" 2>&1 | tee "${log}"; then
    status_text=PASS
    passed=$((passed + 1))
  else
    status=${PIPESTATUS[0]}
    status_text="FAIL(${status})"
    failed=$((failed + 1))
  fi
  printf '%s\t%s\t%s\t%s\n' \
    "${benchmark}" "${status_text}" \
    "${result_dir}/${benchmark}/timings.tsv" "${log}" >> "${summary}"
done
echo "run PASS: ${passed}"
echo "run FAIL: ${failed}"
echo "summary: ${summary}"
(( failed == 0 ))
