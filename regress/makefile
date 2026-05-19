BTOR2RW_BIN = ../build/btor2rw
TEST_DIR = ../../bitwuzla/test/regress
OUTPUT_FILE = test_results_$(shell date +%Y%m%d_%H%M%S).txt

.PHONY: all test clean run help

all: test

test: $(BTOR2RW_BIN)
	@echo "Running BTOR2 -> SMT2 Regression Test Suite..."
	@./test_regress.sh

run: $(BTOR2RW_BIN)
	@echo "Running tests with verbose output..."
	@./test_regress.sh -v

$(BTOR2RW_BIN):
	@echo "Error: btor2rw binary not found at $(BTOR2RW_BIN)"
	@echo "Please build the project first: cd ../build && cmake .. && make"
	@exit 1

clean:
	@rm -f *.txt
	@echo "Cleaned test result files"

help:
	@echo "BTOR2 -> SMT2 Regression Test Suite"
	@echo "==================================="
	@echo "Available targets:"
	@echo "  make test   - Run the test suite"
	@echo "  make run    - Run tests with verbose output"
	@echo "  make clean  - Remove test result files"
	@echo "  make help   - Show this help message"
	@echo ""
	@echo "Binary location: $(BTOR2RW_BIN)"
	@echo "Test directory:  $(TEST_DIR)"
	@echo ""
	@echo "Test coverage:"
	@echo "  1. Loader: exit code check (no crash)"
	@echo "  2. SMT2 emitter: file generation + syntax validation via bitwuzla"
	@echo "  3. Semantic: BTOR2 vs SMT2 sat/unsat match via bitwuzla"
