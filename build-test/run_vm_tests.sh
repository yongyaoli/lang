#!/bin/bash
set -u
cd /mnt/d/github/lang_g || exit 1
OUT=build-test/l_asan
gcc -g -O0 -fsanitize=address -std=gnu11 -D_GNU_SOURCE -I. \
    l_heap.c lexer.c ast.c parser.c value.c gc.c interp.c builtins.c \
    bytecode.c compiler.c vm.c stdlib/*.c main.c -o "$OUT" -lm -lpthread 2>&1 | grep -E "error" | head -10
if [ ! -x "$OUT" ]; then echo "BUILD FAILED"; exit 1; fi
echo "BUILD OK"
export ASAN_OPTIONS=detect_leaks=0
fail=0
for f in tests/[0-9][0-9]_*.l; do
    o="${f%.l}.out"
    ./"$OUT" --vm "$f" >build-test/vm_out.txt 2>build-test/vm_err.txt
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "CRASH rc=$rc $f"
        head -5 build-test/vm_err.txt
        fail=$((fail+1))
    elif ! diff -q build-test/vm_out.txt "$o" >/dev/null 2>&1; then
        echo "DIFF   $f"
        diff "$o" build-test/vm_out.txt | head -8
        fail=$((fail+1))
    fi
done
echo "problems: $fail"
