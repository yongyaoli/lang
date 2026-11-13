#!/bin/bash
set -u
cd /mnt/d/github/lang_g || exit 1
export ASAN_OPTIONS=detect_leaks=0
fail=0
for f in tests/[0-9][0-9]_*.l; do
    o="${f%.l}.out"
    ./build-test/l_asan "$f" >build-test/ast_out.txt 2>build-test/ast_err.txt
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "CRASH rc=$rc $f"
        head -3 build-test/ast_err.txt
        fail=$((fail+1))
    elif ! diff -q build-test/ast_out.txt "$o" >/dev/null 2>&1; then
        echo "DIFF   $f"
        fail=$((fail+1))
    fi
done
echo "problems: $fail"
