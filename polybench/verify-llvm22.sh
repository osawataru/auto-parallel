#!/usr/bin/env bash

set -euo pipefail

benchmark=${1:-bicg}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

"${script_dir}/compile-llvm22.sh" "${benchmark}"
"${script_dir}/run-llvm22.sh" "${benchmark}"
