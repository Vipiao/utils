#!/usr/bin/env bash
# Configures, builds and runs the utils test suite. Configuration is skipped on
# a build tree that already has it, so a re-run is just a rebuild and the tests.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build/tests"

cmake -S "${script_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure
