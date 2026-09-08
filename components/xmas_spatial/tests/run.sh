#!/bin/sh
set -eu
spatial_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/xmas-spatial-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
# Apple's bundled ASan runtime can deadlock in dyld initialization on macOS
# 26.5, before main. UBSan works there; Linux defaults to both. A compatible
# compiler/runtime can opt in with SANITIZERS=address,undefined.
case $(uname -s) in
    Darwin) default_sanitizers=undefined ;;
    *) default_sanitizers=address,undefined ;;
esac
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
    -fsanitize="${SANITIZERS:-$default_sanitizers}" -fno-omit-frame-pointer \
    -I"$spatial_dir/include" "$spatial_dir/xmas_spatial_math.c" \
    "$spatial_dir/xmas_spatial_cluster.c" "$spatial_dir/tests/test_spatial.c" \
    -lm -o "$test_dir/test_spatial"
"$test_dir/test_spatial"
