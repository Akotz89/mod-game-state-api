#!/usr/bin/env bash
set -Eeuo pipefail
ulimit -c 0
module_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
project_root="${WOW_OPERATIONS_ROOT:-$(cd -- "$module_root/../../.." && pwd)}"
if [[ ! -x "$project_root/scripts/wow-build-host-guard" ]]; then
 printf 'Set WOW_OPERATIONS_ROOT to the owning operations checkout containing scripts/wow-build-host-guard.\n' >&2
 exit 78
fi
"$project_root/scripts/wow-build-host-guard"
core_root="${1:-/home/aaron/Documents/ChatGPT/wow private server/work/cpi225-integration-20260912/source/work/cpi194-owner-binding-20260918/source}"
output="$project_root/work/api-restoration-20261011/native-build"
mkdir -p "$output"
sha256sum "$module_root"/src/*.cpp "$module_root"/src/*.h "$module_root"/tests/native/* "$module_root"/include/nlohmann/json.hpp "$module_root"/include/yhirose/httplib.h "$core_root"/deps/fmt/include/fmt/*.h > "$output/source.sha256"
c++ -std=c++20 -O0 -pthread -DFMT_HEADER_ONLY -I"$core_root/deps/fmt/include" -I"$module_root/tests/native" -I"$module_root/include" -I"$module_root/src" \
 "$module_root/tests/native/api_fixture.cpp" "$module_root/src/GameStateUtilities.cpp" \
 "$module_root/src/GameStateAPI.cpp" "$module_root/src/HttpGameStateServer.cpp" -o "$output/api_fixture"
timeout 30s "$output/api_fixture"
sha256sum --check --status "$output/source.sha256"
printf 'PASS source identity unchanged during compile and execution\n'
