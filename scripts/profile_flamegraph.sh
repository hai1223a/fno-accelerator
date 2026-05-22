#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "--" ]]; then
  shift
fi

if [[ $# -eq 0 ]]; then
  echo "usage: $0 -- <command> [args...]" >&2
  exit 2
fi

out_dir="${PROFILE_OUT_DIR:-build/profile}"
flamegraph_dir="${FLAMEGRAPH_DIR:-/home/yunhai/FlameGraph}"
perf_freq="${PERF_FREQ:-997}"
perf_event="${PERF_EVENT:-cycles:u}"

mkdir -p "$out_dir"

if [[ ! -x "$flamegraph_dir/flamegraph.pl" || ! -x "$flamegraph_dir/stackcollapse-perf.pl" ]]; then
  if [[ -e "$flamegraph_dir" ]]; then
    echo "FlameGraph directory exists but required scripts are missing: $flamegraph_dir" >&2
    exit 1
  fi
  git clone --depth=1 https://github.com/brendangregg/FlameGraph.git "$flamegraph_dir"
fi

stamp="$(date +%Y%m%d-%H%M%S)"
perf_data="$out_dir/perf-$stamp.data"
perf_script="$out_dir/perf-$stamp.script"
folded="$out_dir/perf-$stamp.folded"
svg="$out_dir/flamegraph-$stamp.svg"

echo "+ perf record -> $perf_data"
perf record -F "$perf_freq" -e "$perf_event" -g --call-graph dwarf -o "$perf_data" -- "$@"

echo "+ perf script -> $perf_script"
perf script -i "$perf_data" > "$perf_script"

echo "+ stackcollapse -> $folded"
"$flamegraph_dir/stackcollapse-perf.pl" "$perf_script" > "$folded"

echo "+ flamegraph -> $svg"
"$flamegraph_dir/flamegraph.pl" "$folded" > "$svg"

echo "Generated flame graph: $svg"
