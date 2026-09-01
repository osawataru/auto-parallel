#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
result_dir=${LLVM22_RESULT_DIR:-"${script_dir}/prog/llvm22-no-omp"}
work_dir="${result_dir}/${benchmark}"
sequential="${work_dir}/${benchmark}.sequential"
parallel="${work_dir}/${benchmark}.parallel"

if [[ ! -x "${sequential}" || ! -x "${parallel}" ]]; then
  echo "executables not found for ${benchmark}" >&2
  echo "compile first: ${script_dir}/compile-llvm22.sh ${benchmark}" >&2
  exit 1
fi

echo "===== sequential: ${benchmark} ====="
set +e
"${sequential}" > "${work_dir}/sequential.time" \
  2> "${work_dir}/sequential.dump"
sequential_status=$?
set -e
cat "${work_dir}/sequential.time"

echo "===== parallel: ${benchmark} (OMP_NUM_THREADS=${OMP_NUM_THREADS:-4}) ====="
set +e
OMP_NUM_THREADS=${OMP_NUM_THREADS:-4} "${parallel}" \
  > "${work_dir}/parallel.time" \
  2> "${work_dir}/parallel.dump"
parallel_status=$?
set -e
cat "${work_dir}/parallel.time"

echo "===== array result comparison ====="
if cmp -s "${work_dir}/sequential.dump" "${work_dir}/parallel.dump"; then
  echo "PASS: sequential and parallel arrays are identical"
else
  diff -u "${work_dir}/sequential.dump" "${work_dir}/parallel.dump" \
    > "${work_dir}/result.diff" || true
  echo "FAIL: result arrays differ" >&2
  echo "diff: ${work_dir}/result.diff" >&2
  exit 2
fi

echo "sequential time: $(cat "${work_dir}/sequential.time")"
echo "parallel time:   $(cat "${work_dir}/parallel.time")"
echo "sequential exit: ${sequential_status}"
echo "parallel exit:   ${parallel_status}"
echo "sequential dump: ${work_dir}/sequential.dump"
echo "parallel dump:   ${work_dir}/parallel.dump"

if (( sequential_status != 0 || parallel_status != 0 )); then
  exit 1
fi
