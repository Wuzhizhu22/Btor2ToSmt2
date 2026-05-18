#!/bin/bash

BTOR2RW_BIN="../build/btor2rw"
TEST_DIR="../../bitwuzla/test/regress"
OUTPUT_FILE="test_results_$(date +%Y%m%d_%H%M%S).txt"

if [ ! -x "$BTOR2RW_BIN" ]; then
    echo "Error: btor2rw binary not found at $BTOR2RW_BIN"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

echo "=========================================="
echo "BTOR2 Loader Test Suite"
echo "=========================================="
echo "Binary: $BTOR2RW_BIN"
echo "Test directory: $TEST_DIR"
echo "Output file: $OUTPUT_FILE"
echo "=========================================="

total=0
success=0
fail=0
fail_array=0
fail_overflow=0
fail_parser=0
fail_other=0

failed_files=()
array_files=()
overflow_files=()
parser_files=()
other_files=()

echo "Scanning for .btor2 files..."
btor2_files=$(find "$TEST_DIR" -name "*.btor2" | sort)
total=$(echo "$btor2_files" | wc -l)

echo "Found $total .btor2 files. Starting test..."
echo ""

for f in $btor2_files; do
    "$BTOR2RW_BIN" "$f" > /dev/null 2>&1
    exit_code=$?
    
    if [ $exit_code -ne 0 ]; then
        fail=$((fail + 1))
        err_line=$("$BTOR2RW_BIN" "$f" 2>&1 | grep "Error")
        
        if echo "$err_line" | grep -q "array sort not supported"; then
            fail_array=$((fail_array + 1))
            array_files+=("$f")
        elif echo "$err_line" | grep -q "Overflow opcodes not supported"; then
            fail_overflow=$((fail_overflow + 1))
            overflow_files+=("$f")
        elif echo "$f" | grep -q "btor2perr"; then
            fail_parser=$((fail_parser + 1))
            parser_files+=("$f")
        else
            fail_other=$((fail_other + 1))
            other_files+=("$f")
            failed_files+=("$f -> $err_line")
        fi
        
        printf "F"
    else
        success=$((success + 1))
        printf "."
    fi
done

echo ""
echo ""
echo "=========================================="
echo "TEST RESULTS SUMMARY"
echo "=========================================="
printf "Total files:      %4d\n" $total
printf "Success:          %4d (%3.1f%%)\n" $success $(echo "scale=1; $success/$total*100" | bc)
printf "Failures:         %4d (%3.1f%%)\n" $fail $(echo "scale=1; $fail/$total*100" | bc)
echo "------------------------------------------"
printf "  Array sort:     %4d\n" $fail_array
printf "  Overflow ops:   %4d\n" $fail_overflow
printf "  Parser errors:  %4d\n" $fail_parser
printf "  Other errors:   %4d\n" $fail_other
echo "=========================================="

if [ ${#failed_files[@]} -gt 0 ]; then
    echo ""
    echo "UNEXPECTED FAILURES:"
    echo "------------------------------------------"
    for err in "${failed_files[@]}"; do
        echo "$err"
    done
fi

if [ "$1" = "-v" ]; then
    if [ ${#array_files[@]} -gt 0 ]; then
        echo ""
        echo "ARRAY SORT FAILURES (expected - not supported):"
        echo "------------------------------------------"
        for f in "${array_files[@]}"; do
            echo "$f"
        done
    fi

    if [ ${#overflow_files[@]} -gt 0 ]; then
        echo ""
        echo "OVERFLOW OPCODE FAILURES (expected - not supported):"
        echo "------------------------------------------"
        for f in "${overflow_files[@]}"; do
            echo "$f"
        done
    fi
fi

echo ""
echo "Results saved to: $OUTPUT_FILE"

cat > "$OUTPUT_FILE" << EOF
==========================================
BTOR2 Loader Test Suite Results
Date: $(date)
Binary: $BTOR2RW_BIN
Test directory: $TEST_DIR
==========================================
Total files:      $total
Success:          $success ($(echo "scale=1; $success/$total*100" | bc)%)
Failures:         $fail ($(echo "scale=1; $fail/$total*100" | bc)%)
------------------------------------------
  Array sort:     $fail_array
  Overflow ops:   $fail_overflow
  Parser errors:  $fail_parser
  Other errors:   $fail_other
==========================================
EOF

if [ ${#failed_files[@]} -gt 0 ]; then
    echo "" >> "$OUTPUT_FILE"
    echo "UNEXPECTED FAILURES:" >> "$OUTPUT_FILE"
    for err in "${failed_files[@]}"; do
        echo "$err" >> "$OUTPUT_FILE"
    done
fi

echo "Done!"