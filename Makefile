BTOR2RW_BIN = ./build/btor2rw
TEST_DIR = ../bitwuzla/test/regress
TEST_SCRIPT = ./regress/test_regress.sh

.PHONY: all build test test-verbose test_all_solvers clean help

all: build test

build:
	@mkdir -p build
	@cd build && cmake .. && make

test: $(BTOR2RW_BIN)
	@echo "Running BTOR2 -> SMT2 Regression Test Suite..."
	@cd regress && ./test_regress.sh

test-verbose: $(BTOR2RW_BIN)
	@echo "Running tests with verbose output..."
	@cd regress && ./test_regress.sh -v

test_all_solvers: $(BTOR2RW_BIN)
	@echo "============================================="
	@echo "Running regression tests for ALL solvers..."
	@echo "============================================="
	@cd regress && ./test_regress.sh -s bitwuzla -b 5 -t 5
	@echo ""
	@cd regress && ./test_regress.sh -s z3 -b 5 -t 5
	@echo ""
	@cd regress && ./test_regress.sh -s cvc5 -b 5 -t 5
	@echo ""
	@cd regress && ./test_regress.sh -s yices -b 5 -t 5
	@echo ""
	@echo "============================================="
	@echo "All solver tests completed!"
	@echo "============================================="

clean:
	@rm -rf build
	@rm -f regress/*.txt
	@echo "Cleaned build and test result files"

help:
	@echo "BTOR2 -> SMT2 Build and Test"
	@echo "=============================="
	@echo "Available targets:"
	@echo "  make build             - Build the project (cmake + make)"
	@echo "  make test              - Run the regression test suite (default: bitwuzla)"
	@echo "  make test-verbose      - Run tests with verbose output"
	@echo "  make test_all_solvers  - Run tests for all solvers (bitwuzla, z3, cvc5, yices)"
	@echo "  make clean             - Remove build and test result files"
	@echo "  make help              - Show this help message"
	@echo ""
	@echo "Binary location: $(BTOR2RW_BIN)"
	@echo "Test directory:  $(TEST_DIR)"
