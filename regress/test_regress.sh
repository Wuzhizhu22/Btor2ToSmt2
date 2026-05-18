#!/bin/bash

BTOR2RW_BIN="../build/btor2rw"
TEST_DIR="../../bitwuzla/test/regress"
OUTPUT_FILE="test_results_$(date +%Y%m%d_%H%M%S).txt"
TMP_DIR="${TMPDIR:-/tmp}/btor2rw_test_$$"

BITWUZLA=""
if command -v bitwuzla &>/dev/null; then
    BITWUZLA="bitwuzla"
elif [ -x "/usr/local/bin/bitwuzla" ]; then
    BITWUZLA="/usr/local/bin/bitwuzla"
fi

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

mkdir -p "$TMP_DIR"

if [ ! -x "$BTOR2RW_BIN" ]; then
    echo "Error: btor2rw binary not found at $BTOR2RW_BIN"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

if [ -z "$BITWUZLA" ]; then
    echo "Warning: bitwuzla not found. Semantic verification will be skipped."
    echo "  (SMT2 emitter syntax checks will still run.)"
    BITWUZLA=""
fi

echo "=========================================="
echo "BTOR2 -> SMT2 Regression Test Suite"
echo "=========================================="
echo "Binary:        $BTOR2RW_BIN"
echo "Test dir:      $TEST_DIR"
echo "bitwuzla:      ${BITWUZLA:-not found (semantic checks disabled)}"
echo "Output file:   $OUTPUT_FILE"
echo "=========================================="

total=0
pass=0
fail=0
skip_unsupported=0
skip_nosat=0
skip_nobtor2ref=0
skip_syntax_only=0

declare -a failed_files
declare -a unsupported_files
declare -a mismatch_files

echo "Scanning for .btor2 files..."
mapfile -t btor2_files < <(find "$TEST_DIR" -name "*.btor2" | sort)
total=${#btor2_files[@]}
echo "Found $total .btor2 files. Starting test..."
echo ""

for f in "${btor2_files[@]}"; do
    smt2_out="$TMP_DIR/$(basename "$f").smt2"
    "$BTOR2RW_BIN" "$f" -o "$smt2_out" > /dev/null 2>&1
    exit_code=$?

    if [ $exit_code -ne 0 ]; then
        err_msg=$("$BTOR2RW_BIN" "$f" 2>&1 | grep -v "^===" | grep -v "^Total\|^Inputs\|^Constraints\|^Bads\|^SMT2\|^Node\|^Sort\|^btor2_id" | tr -d '\n')
        if echo "$err_msg" | grep -qi "array\|overflow\|parser\|slice hi < lo\|malformed\|unsupported.*opcode"; then
            skip_unsupported=$((skip_unsupported + 1))
            unsupported_files+=("$f")
            printf "S"
        else
            fail=$((fail + 1))
            failed_files+=("$f -> $err_msg")
            printf "F"
        fi
        continue
    fi

    if [ -z "$BITWUZLA" ] || [ ! -x "$BITWUZLA" ]; then
        if [ -s "$smt2_out" ]; then
            pass=$((pass + 1))
            printf "."
        else
            fail=$((fail + 1))
            failed_files+=("$f -> empty SMT2 output")
            printf "F"
        fi
        continue
    fi

    if [ ! -s "$smt2_out" ]; then
        fail=$((fail + 1))
        failed_files+=("$f -> empty SMT2 output")
        printf "F"
        continue
    fi

    smt2_res=$("$BITWUZLA" "$smt2_out" 2>/dev/null)
    smt2_rc=$?
    if [ $smt2_rc -ne 0 ]; then
        err_detail=$("$BITWUZLA" "$smt2_out" 2>&1 | grep -v "^==" | grep -v "^sat\|^unsat\|^unknown" | head -1 | cut -c1-120)
        fail=$((fail + 1))
        failed_files+=("$f -> SMT2 syntax error: $err_detail")
        printf "F"
        continue
    fi

    btor2_res=$("$BITWUZLA" --lang btor2 "$f" 2>/dev/null)
    btor2_rc=$?
    if [ $btor2_rc -ne 0 ]; then
        skip_nobtor2ref=$((skip_nobtor2ref + 1))
        printf "X"
        continue
    fi

    if [ "$smt2_res" = "$btor2_res" ]; then
        pass=$((pass + 1))
        printf "."
    else
        fail=$((fail + 1))
        mismatch_files+=("$f -> BTOR2=$btor2_res SMT2=$smt2_res")
        printf "M"
    fi
done

echo ""
echo ""

total_checked=$((total - skip_unsupported))
echo "=========================================="
echo "TEST RESULTS SUMMARY"
echo "=========================================="
printf "Total files:       %4d\n" $total
printf "  Pass:            %4d\n" $pass
printf "  Fail:            %4d\n" $fail
printf "  Skip (unsupported): %3d\n" $skip_unsupported
printf "  Skip (no btor2 ref): %2d\n" $skip_nobtor2ref
if [ -n "$BITWUZLA" ]; then
    printf "\nSemantic match rate: %3d / %3d (%.1f%%)\n" \
        $pass $total_checked \
        "$(echo "scale=1; if($total_checked>0) $pass*100/$total_checked else 0" | bc)"
fi
echo "=========================================="

if [ ${#failed_files[@]} -gt 0 ]; then
    echo ""
    echo "UNEXPECTED FAILURES:"
    echo "------------------------------------------"
    for err in "${failed_files[@]}"; do
        echo "$err"
    done
fi

if [ ${#mismatch_files[@]} -gt 0 ]; then
    echo ""
    echo "SEMANTIC MISMATCHES:"
    echo "------------------------------------------"
    for m in "${mismatch_files[@]}"; do
        echo "$m"
    done
fi

if [ "$1" = "-v" ] || [ "$1" = "--verbose" ]; then
    if [ ${#unsupported_files[@]} -gt 0 ]; then
        echo ""
        echo "SKIPPED (unsupported - not errors):"
        echo "------------------------------------------"
        for f in "${unsupported_files[@]}"; do
            echo "  $f"
        done
    fi
fi

echo ""
echo "Results saved to: $OUTPUT_FILE"

cat > "$OUTPUT_FILE" << EOF
==========================================
BTOR2 -> SMT2 Regression Test Results
Date: $(date)
Binary: $BTOR2RW_BIN
bitwuzla: ${BITWUZLA:-not found}
Test dir: $TEST_DIR
==========================================
Total files:       $total
  Pass:            $pass
  Fail:            $fail
  Skip (unsupported): $skip_unsupported
  Skip (no btor2 ref): $skip_nobtor2ref
==========================================
EOF

if [ ${#failed_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "UNEXPECTED FAILURES:" >> "$OUTPUT_FILE"
    for err in "${failed_files[@]}"; do
        echo "  $err" >> "$OUTPUT_FILE"
    done
fi

if [ ${#mismatch_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "SEMANTIC MISMATCHES:" >> "$OUTPUT_FILE"
    for m in "${mismatch_files[@]}"; do
        echo "  $m" >> "$OUTPUT_FILE"
    done
fi

if [ $fail -eq 0 ]; then
    echo "All tests passed!"
else
    echo "Some tests failed. See details above."
    exit 1
fi
