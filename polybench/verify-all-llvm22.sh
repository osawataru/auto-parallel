#!/usr/bin/env bash

set -uo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
result_dir=${LLVM22_RESULT_DIR:-"${script_dir}/prog/llvm22-no-omp"}
summary="${result_dir}/summary.tsv"
source "${script_dir}/benchmarks-llvm22.sh"

mkdir -p "${result_dir}"
printf 'benchmark\tstatus\tdetail\n' > "${summary}"

passed=0
failed=0

for benchmark in "${benchmarks[@]}"; do
  log="${result_dir}/${benchmark}/verify.log"
  mkdir -p "${result_dir}/${benchmark}"
  echo "===== ${benchmark} ====="

  if "${script_dir}/verify-llvm22.sh" "${benchmark}" > "${log}" 2>&1; then
    seq_time=$(sed -n 's/^sequential time:[[:space:]]*//p' "${log}" | tail -1)
    par_time=$(sed -n 's/^parallel time:[[:space:]]*//p' "${log}" | tail -1)
    printf '%s\tPASS\tsequential=%s parallel=%s dumps=%s/{sequential,parallel}.dump\n' \
      "${benchmark}" "${seq_time:-unknown}" "${par_time:-unknown}" \
      "${result_dir}/${benchmark}" >> "${summary}"
    echo "PASS"
    passed=$((passed + 1))
  else
    status=$?
    detail=$(grep -E 'error:|LLVM ERROR:|Segmentation fault|FAIL:' "${log}" | tail -1 | tr '\t' ' ')
    if [[ -z "${detail}" ]]; then
      detail=$(tail -1 "${log}" | tr '\t' ' ')
    fi
    printf '%s\tFAIL(%s)\t%s\n' "${benchmark}" "${status}" "${detail}" >> "${summary}"
    echo "FAIL (exit ${status})"
    failed=$((failed + 1))
  fi
done

echo
echo "PASS: ${passed}"
echo "FAIL: ${failed}"
echo "summary: ${summary}"

if (( failed != 0 )); then
  exit 1
fi
