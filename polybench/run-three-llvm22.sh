#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
runs=${RUNS:-3}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
result_dir=${THREE_RESULT_DIR:-"${script_dir}/prog/llvm22-three"}
work_dir="${result_dir}/${benchmark}"
threads=${OMP_NUM_THREADS:-4}
summary="${work_dir}/timings.tsv"

if ! [[ "${runs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "RUNS must be a positive integer: ${runs}" >&2
  exit 1
fi

variants=(sequential manual automatic)
executables=(
  "${work_dir}/${benchmark}.sequential"
  "${work_dir}/${benchmark}.manual"
  "${work_dir}/${benchmark}.parallel"
)

for executable in "${executables[@]}"; do
  if [[ ! -x "${executable}" ]]; then
    echo "executable not found: ${executable}" >&2
    echo "compile first: ${script_dir}/compile-three-llvm22.sh ${benchmark}" >&2
    exit 1
  fi
done

printf 'variant\trun\tseconds\texit_status\n' > "${summary}"

for index in "${!variants[@]}"; do
  variant=${variants[$index]}
  executable=${executables[$index]}
  echo "===== ${benchmark}: ${variant} ====="

  for ((run = 1; run <= runs; run++)); do
    time_file="${work_dir}/${variant}.time.${run}"
    dump_file="${work_dir}/${variant}.dump"
    set +e
    if [[ "${variant}" == sequential ]]; then
      "${executable}" > "${time_file}" 2> "${dump_file}"
    else
      OMP_NUM_THREADS="${threads}" "${executable}" \
        > "${time_file}" 2> "${dump_file}"
    fi
    status=$?
    set -e

    seconds=$(awk '
      /^TIME:[[:space:]]*/ {print $2; found=1; exit}
      /^[[:space:]]*[0-9]+([.][0-9]+)?[[:space:]]*$/ {print $1; found=1; exit}
      END {if (!found) exit 1}
    ' "${time_file}") || seconds=unknown
    printf '%s\t%s\t%s\t%s\n' \
      "${variant}" "${run}" "${seconds}" "${status}" >> "${summary}"
    echo "run ${run}/${runs}: ${seconds} sec (exit ${status})"
  done
done

echo "===== result comparison ====="
comparison_status=0
for variant in manual automatic; do
  if cmp -s "${work_dir}/sequential.dump" "${work_dir}/${variant}.dump"; then
    echo "PASS: sequential == ${variant}"
  else
    echo "FAIL: sequential != ${variant}" >&2
    diff -u "${work_dir}/sequential.dump" "${work_dir}/${variant}.dump" \
      > "${work_dir}/sequential-vs-${variant}.diff" || true
    echo "diff: ${work_dir}/sequential-vs-${variant}.diff" >&2
    comparison_status=2
  fi
done

echo "timings: ${summary}"
exit "${comparison_status}"
