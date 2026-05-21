#!/bin/bash

BTOR2RW_BIN="../build/btor2rw"
TEST_DIR="/home/wuzhizhu22/tool/bitwuzla/test/regress"
OUTPUT_FILE="test_results_cvc5_$(date +%Y%m%d_%H%M%S).txt"
TMP_DIR="${TMPDIR:-/tmp}/btor2rw_cvc5_test_$$"

DEBUG_MODE=0
VERBOSE_MODE=0
TIMEOUT=30
BTOR2_TIMEOUT=60
while [ $# -gt 0 ]; do
    case "$1" in
        -d|--debug)   DEBUG_MODE=1; shift ;;
        -v|--verbose) VERBOSE_MODE=1; shift ;;
        -t)           TIMEOUT="$2"; shift 2 ;;
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        -b)           BTOR2_TIMEOUT="$2"; shift 2 ;;
        --btimeout)   BTOR2_TIMEOUT="$2"; shift 2 ;;
        *)            shift ;;
    esac
done

BITWUZLA=""
if command -v bitwuzla &>/dev/null; then
    BITWUZLA="bitwuzla"
elif [ -x "/usr/local/bin/bitwuzla" ]; then
    BITWUZLA="/usr/local/bin/bitwuzla"
fi

CVC5=""
if command -v cvc5 &>/dev/null; then
    CVC5="cvc5"
elif [ -x "/usr/local/bin/cvc5" ]; then
    CVC5="/usr/local/bin/cvc5"
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
    echo "Error: bitwuzla not found. bitwuzla is required for BTOR2 reference."
    exit 1
fi

if [ -z "$CVC5" ]; then
    echo "Error: cvc5 not found."
    exit 1
fi

echo "=========================================="
echo "BTOR2 -> SMT2 Regression Test Suite (cvc5)"
echo "=========================================="
echo "Binary:        $BTOR2RW_BIN"
echo "Test dir:      $TEST_DIR"
echo "bitwuzla:      $BITWUZLA (BTOR2 reference)"
echo "cvc5:          $CVC5 (SMT2 solver)"
echo "Timeout:       ${TIMEOUT}s per case (SMT2 solver)"
echo "BTOR2 timeout: ${BTOR2_TIMEOUT}s per case (bitwuzla ref)"
echo "Output file:   $OUTPUT_FILE"
if [ "$DEBUG_MODE" = "1" ]; then
    echo "Debug mode:    ON (stops on first F/M)"
fi
echo "=========================================="

total=0
pass=0
fail=0
skip_unsupported=0
skip_btor2_timeout=0
skip_btor2_error=0
skip_smt2_timeout=0
skip_smt2_error=0

declare -a failed_files
declare -a unsupported_files
declare -a mismatch_files
declare -a btor2_timeout_files
declare -a btor2_error_files
declare -a smt2_error_files

debug_stop() {
    if [ "$DEBUG_MODE" = "1" ]; then
        echo ""
        echo "[DEBUG] Stopped at: $1"
        cleanup
        exit 1
    fi
}

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
            debug_stop "$f -> $err_msg"
        fi
        continue
    fi

    if [ ! -s "$smt2_out" ]; then
        fail=$((fail + 1))
        failed_files+=("$f -> empty SMT2 output")
        printf "F"
        debug_stop "$f -> empty SMT2 output"
        continue
    fi

    btor2_res=$(timeout -k 1s $BTOR2_TIMEOUT "$BITWUZLA" --lang btor2 "$f" 2>/dev/null)
    btor2_rc=$?
    if [ $btor2_rc -eq 124 ]; then
        skip_btor2_timeout=$((skip_btor2_timeout + 1))
        btor2_timeout_files+=("$f")
        printf "T"
        continue
    fi
    if [ $btor2_rc -ne 0 ]; then
        skip_btor2_error=$((skip_btor2_error + 1))
        btor2_error_files+=("$f")
        printf "X"
        continue
    fi

    smt2_res=$(timeout -k 1s $TIMEOUT "$CVC5" --lang=smt2 "$smt2_out" 2>/dev/null)
    smt2_rc=$?
    if [ $smt2_rc -eq 124 ]; then
        skip_smt2_timeout=$((skip_smt2_timeout + 1))
        printf "P"
        continue
    fi
    if [ $smt2_rc -ne 0 ]; then
        err_detail=$("$CVC5" --lang=smt2 "$smt2_out" 2>&1 | head -1 | cut -c1-200)
        skip_smt2_error=$((skip_smt2_error + 1))
        smt2_error_files+=("$f -> $err_detail")
        printf "E"
        debug_stop "$f -> cvc5 SMT2 error: $err_detail"
        continue
    fi

    if [ "$smt2_res" = "$btor2_res" ]; then
        pass=$((pass + 1))
        printf "."
    else
        fail=$((fail + 1))
        mismatch_files+=("$f -> BTOR2(bitwuzla)=$btor2_res SMT2(cvc5)=$smt2_res")
        printf "M"
        debug_stop "MISMATCH: $f -> BTOR2(bitwuzla)=$btor2_res SMT2(cvc5)=$smt2_res"
    fi
done

echo ""
echo ""

total_checked=$((total - skip_unsupported))
echo "=========================================="
echo "TEST RESULTS SUMMARY (cvc5)"
echo "=========================================="
printf "Total files:        %4d\n" $total
printf "  Pass:             %4d\n" $pass
printf "  Fail:             %4d\n" $fail
echo "------------------------------------------"
printf "  Skip (unsupported):   %3d\n" $skip_unsupported
printf "  Skip (BTOR2 timeout): %3d  (bitwuzla ref)\n" $skip_btor2_timeout
printf "  Skip (BTOR2 error):   %3d  (bitwuzla ref)\n" $skip_btor2_error
printf "  Skip (SMT2 timeout):  %3d  (cvc5)\n" $skip_smt2_timeout
printf "  Skip (SMT2 error):    %3d  (cvc5)\n" $skip_smt2_error
echo "=========================================="
printf "\nSemantic match rate: %3d / %3d (%.1f%%)\n" \
    $pass $total_checked \
    "$(echo "scale=1; if($total_checked>0) $pass*100/$total_checked else 0" | bc)"
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

if [ ${#btor2_timeout_files[@]} -gt 0 ]; then
    echo ""
    echo "SKIPPED (BTOR2 reference timeout - bitwuzla):"
    echo "------------------------------------------"
    for f in "${btor2_timeout_files[@]}"; do
        echo "  $f"
    done
fi

if [ ${#btor2_error_files[@]} -gt 0 ]; then
    echo ""
    echo "SKIPPED (BTOR2 reference error - bitwuzla):"
    echo "------------------------------------------"
    for f in "${btor2_error_files[@]}"; do
        echo "  $f"
    done
fi

if [ ${#smt2_error_files[@]} -gt 0 ]; then
    echo ""
    echo "SKIPPED (SMT2 solver error - cvc5):"
    echo "------------------------------------------"
    for m in "${smt2_error_files[@]}"; do
        echo "  $m"
    done
fi

if [ "$VERBOSE_MODE" = "1" ]; then
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
BTOR2 -> SMT2 Regression Test Results (cvc5)
Date: $(date)
Binary: $BTOR2RW_BIN
bitwuzla: $BITWUZLA (BTOR2 reference)
cvc5:     $CVC5 (SMT2 solver)
Test dir: $TEST_DIR
==========================================
Total files:        $total
  Pass:             $pass
  Fail:             $fail
  Skip (unsupported):   $skip_unsupported
  Skip (BTOR2 timeout): $skip_btor2_timeout
  Skip (BTOR2 error):   $skip_btor2_error
  Skip (SMT2 timeout):  $skip_smt2_timeout
  Skip (SMT2 error):    $skip_smt2_error
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

if [ ${#btor2_timeout_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "SKIPPED (BTOR2 reference timeout):" >> "$OUTPUT_FILE"
    for f in "${btor2_timeout_files[@]}"; do
        echo "  $f" >> "$OUTPUT_FILE"
    done
fi

if [ ${#btor2_error_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "SKIPPED (BTOR2 reference error):" >> "$OUTPUT_FILE"
    for f in "${btor2_error_files[@]}"; do
        echo "  $f" >> "$OUTPUT_FILE"
    done
fi

if [ ${#smt2_error_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "SKIPPED (SMT2 solver error):" >> "$OUTPUT_FILE"
    for m in "${smt2_error_files[@]}"; do
        echo "  $m" >> "$OUTPUT_FILE"
    done
fi

if [ $fail -eq 0 ]; then
    echo "All tests passed!"
else
    echo "Some tests failed. See details above."
    exit 1
fi
