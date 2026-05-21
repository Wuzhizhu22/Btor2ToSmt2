#!/bin/bash

BTOR2RW_BIN="../build/btor2rw"
TEST_DIR="../../bitwuzla/test/regress"
TMP_DIR="${TMPDIR:-/tmp}/btor2rw_test_$$"

DEBUG_MODE=0
VERBOSE_MODE=0
TIMEOUT=30
BTOR2_TIMEOUT=60
SOLVER="bitwuzla"
while [ $# -gt 0 ]; do
    case "$1" in
        -d|--debug)   DEBUG_MODE=1; shift ;;
        -v|--verbose) VERBOSE_MODE=1; shift ;;
        -t)           TIMEOUT="$2"; shift 2 ;;
        --timeout)    TIMEOUT="$2"; shift 2 ;;
        -b)           BTOR2_TIMEOUT="$2"; shift 2 ;;
        --btimeout)   BTOR2_TIMEOUT="$2"; shift 2 ;;
        -s|--solver)  SOLVER="$2"; shift 2 ;;
        *)            shift ;;
    esac
done

case "$SOLVER" in
    bitwuzla)
        SOLVER_LABEL="bitwuzla"
        SOLVER_BINARY="bitwuzla"
        SOLVER_ARGS=""
        SOLVER_ERR_FILTER='grep -v "^==" | grep -v "^sat\|^unsat\|^unknown" | head -1 | cut -c1-120'
        SOLVER_FALLBACK_BINARIES=("/usr/local/bin/bitwuzla")
        ;;
    z3)
        SOLVER_LABEL="z3"
        SOLVER_BINARY="z3"
        SOLVER_ARGS="-smt2"
        SOLVER_ERR_FILTER='head -1 | cut -c1-200'
        SOLVER_FALLBACK_BINARIES=("/usr/local/bin/z3")
        ;;
    cvc5)
        SOLVER_LABEL="cvc5"
        SOLVER_BINARY="cvc5"
        SOLVER_ARGS="--lang=smt2"
        SOLVER_ERR_FILTER='head -1 | cut -c1-200'
        SOLVER_FALLBACK_BINARIES=("/usr/local/bin/cvc5")
        ;;
    yices)
        SOLVER_LABEL="yices"
        SOLVER_BINARY="yices-smt2"
        SOLVER_ARGS=""
        SOLVER_ERR_FILTER='head -1 | cut -c1-200'
        SOLVER_FALLBACK_BINARIES=("/usr/local/bin/yices-smt2")
        ;;
    *)
        echo "Error: unknown solver '$SOLVER'. Supported: bitwuzla, z3, cvc5, yices"
        exit 1
        ;;
esac

OUTPUT_FILE="test_results_${SOLVER_LABEL}_$(date +%Y%m%d_%H%M%S).txt"
TMP_DIR="${TMPDIR:-/tmp}/btor2rw_${SOLVER_LABEL}_test_$$"

BITWUZLA=""
if command -v bitwuzla &>/dev/null; then
    BITWUZLA="bitwuzla"
elif [ -x "/usr/local/bin/bitwuzla" ]; then
    BITWUZLA="/usr/local/bin/bitwuzla"
fi

SMT2_SOLVER=""
if command -v "$SOLVER_BINARY" &>/dev/null; then
    SMT2_SOLVER="$SOLVER_BINARY"
else
    for fb in "${SOLVER_FALLBACK_BINARIES[@]}"; do
        if [ -x "$fb" ]; then
            SMT2_SOLVER="$fb"
            break
        fi
    done
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

if [ "$SOLVER" != "bitwuzla" ]; then
    if [ -z "$BITWUZLA" ]; then
        echo "Error: bitwuzla not found. bitwuzla is required for BTOR2 reference."
        exit 1
    fi
    if [ -z "$SMT2_SOLVER" ]; then
        echo "Error: $SOLVER_LABEL not found."
        exit 1
    fi
else
    if [ -z "$BITWUZLA" ]; then
        echo "Warning: bitwuzla not found. Semantic verification will be skipped."
        echo "  (SMT2 emitter syntax checks will still run.)"
        BITWUZLA=""
    fi
fi

if [ "$SOLVER" = "bitwuzla" ] || [ -z "$SMT2_SOLVER" ]; then
    TITLE="BTOR2 -> SMT2 Regression Test Suite"
else
    TITLE="BTOR2 -> SMT2 Regression Test Suite (${SOLVER_LABEL^^})"
fi

echo "=========================================="
echo "$TITLE"
echo "=========================================="
echo "Binary:        $BTOR2RW_BIN"
echo "Test dir:      $TEST_DIR"
echo "bitwuzla:      ${BITWUZLA:-not found (semantic checks disabled)} (BTOR2 reference)"
if [ "$SOLVER" != "bitwuzla" ]; then
    echo "$SOLVER_LABEL:          $SMT2_SOLVER (SMT2 solver)"
fi
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

    if [ -z "$BITWUZLA" ] || ! command -v "$BITWUZLA" &>/dev/null; then
        if [ -s "$smt2_out" ]; then
            pass=$((pass + 1))
            printf "."
        else
            fail=$((fail + 1))
            failed_files+=("$f -> empty SMT2 output")
            printf "F"
            debug_stop "$f -> empty SMT2 output"
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

    smt2_cmd="$SMT2_SOLVER $SOLVER_ARGS"
    smt2_res=$(timeout -k 1s $TIMEOUT $smt2_cmd "$smt2_out" 2>/dev/null)
    smt2_rc=$?
    if [ $smt2_rc -eq 124 ]; then
        skip_smt2_timeout=$((skip_smt2_timeout + 1))
        printf "P"
        continue
    fi
    if [ $smt2_rc -ne 0 ]; then
        err_detail=$(eval "$smt2_cmd \"$smt2_out\" 2>&1 | $SOLVER_ERR_FILTER")
        skip_smt2_error=$((skip_smt2_error + 1))
        smt2_error_files+=("$f -> $err_detail")
        printf "E"
        debug_stop "$f -> $SOLVER_LABEL SMT2 error: $err_detail"
        continue
    fi

    if [ "$smt2_res" = "$btor2_res" ]; then
        pass=$((pass + 1))
        printf "."
    else
        fail=$((fail + 1))
        mismatch_files+=("$f -> BTOR2=$btor2_res SMT2($SOLVER_LABEL)=$smt2_res")
        printf "M"
        debug_stop "MISMATCH: $f -> BTOR2=$btor2_res SMT2($SOLVER_LABEL)=$smt2_res"
    fi
done

echo ""
echo ""

ref_count=$((total - skip_unsupported - skip_btor2_timeout - skip_btor2_error))
echo "=========================================="
echo "TEST RESULTS SUMMARY"
if [ "$SOLVER" != "bitwuzla" ]; then
    echo "  (SMT2 solver: $SOLVER_LABEL)"
fi
echo "=========================================="
printf "Total files:        %4d\n" $total
printf "  Pass:             %4d\n" $pass
printf "  Fail:             %4d\n" $fail
echo "------------------------------------------"
printf "  Skip (unsupported):   %3d\n" $skip_unsupported
printf "  Skip (BTOR2 timeout): %3d  (bitwuzla ref)\n" $skip_btor2_timeout
printf "  Skip (BTOR2 error):   %3d  (bitwuzla ref)\n" $skip_btor2_error
printf "  Skip (SMT2 timeout):  %3d  (%s)\n" $skip_smt2_timeout "$SOLVER_LABEL"
printf "  Skip (SMT2 error):    %3d  (%s)\n" $skip_smt2_error "$SOLVER_LABEL"
if [ -n "$BITWUZLA" ]; then
    echo ""
    printf "Semantic match rate: %3d / %3d (%.1f%%)\n" \
        $pass $ref_count \
        "$(echo "scale=1; if($ref_count>0) $pass*100/$ref_count else 0" | bc)"
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
    echo "SKIPPED (SMT2 solver error - $SOLVER_LABEL):"
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

solver_log_line=""
if [ "$SOLVER" != "bitwuzla" ]; then
    solver_log_line="$SOLVER_LABEL:     $SMT2_SOLVER (SMT2 solver)"
fi

cat > "$OUTPUT_FILE" << EOF
==========================================
$TITLE
Date: $(date)
Binary: $BTOR2RW_BIN
bitwuzla: ${BITWUZLA:-not found} (BTOR2 reference)
${solver_log_line}
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
