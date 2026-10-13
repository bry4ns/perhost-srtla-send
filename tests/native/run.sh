#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT
cc=${CC:-cc}
"$cc" -DANDROID=1 -DVERSION='"handover-test"' -pthread -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$repo_dir/tests/native/include" -I"$repo_dir/srtla" \
    "$repo_dir/tests/native/srtla_handover_test.c" "$repo_dir/srtla/common.c" \
    -o "$test_build/srtla_handover_test"
"$test_build/srtla_handover_test"
