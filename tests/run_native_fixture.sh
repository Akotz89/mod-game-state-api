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
core_root="${1:-$module_root/../azerothcore-wotlk}"
output="$(realpath -m -- "${2:?Provide a task-owned output directory as the second argument}")"
work_root="$(realpath -m -- "$project_root/work")"
if [[ "$output" != "$work_root/"* ]]; then
 printf 'Output must be inside the operations work directory.\n' >&2
 exit 2
fi
mkdir -p "$(dirname "$output")"
if ! mkdir "$output"; then
 printf 'Output directory must be new; existing artifacts were not overwritten.\n' >&2
 exit 2
fi
sha256sum "$module_root"/src/*.cpp "$module_root"/src/*.h "$module_root"/tests/native/* "$module_root"/include/nlohmann/json.hpp "$module_root"/include/yhirose/httplib.h "$core_root"/deps/fmt/include/fmt/*.h > "$output/source.sha256"
c++ -std=c++20 -O0 -pthread -DFMT_HEADER_ONLY -I"$core_root/deps/fmt/include" -I"$module_root/tests/native" -I"$module_root/include" -I"$module_root/src" \
 "$module_root/tests/native/inventory_fixture.cpp" "$module_root/src/GameStateUtilities.cpp" \
 "$module_root/src/GameStateAPI.cpp" "$module_root/src/HttpGameStateServer.cpp" -o "$output/inventory_fixture"
timeout 60s "$output/inventory_fixture" "$output/inventory-contract.json"
sha256sum --check --status "$output/source.sha256"
printf 'PASS source identity unchanged during compile and execution\n'
