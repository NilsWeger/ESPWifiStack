#!/usr/bin/env bash
# Runs the protocol unit tests with plain g++ + Unity, without PlatformIO.
# Fallback for environments where the PlatformIO registry is not reachable
# (the preferred command is `pio test -e native`).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
unity_dir="$root/.pio/host-unity"
unity_tag="v2.6.0"

if [ ! -f "$unity_dir/unity.c" ]; then
  mkdir -p "$unity_dir"
  for f in unity.c unity.h unity_internals.h; do
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/$unity_tag/src/$f" -o "$unity_dir/$f"
  done
fi

out="$unity_dir/test_protocol"
gcc -c "$unity_dir/unity.c" -I"$unity_dir" -o "$unity_dir/unity.o"
g++ -std=c++17 -Wall -Wextra -Werror -I"$root/include" -I"$unity_dir" \
  "$root/test/test_protocol/test_main.cpp" "$unity_dir/unity.o" -o "$out"
"$out"
