BTOR2RW_BIN = ./build/btor2rw
TEST_DIR = ../bitwuzla/test/regress
TEST_SCRIPT = ./regress/test_regress.sh

.PHONY: all build test clean help

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

clean:
	@rm -rf build
	@rm -f regress/*.txt
	@echo "Cleaned build and test result files"

help:
	@echo "BTOR2 -> SMT2 Build and Test"
	@echo "=============================="
	@echo "Available targets:"
	@echo "  make build           - Build the project (cmake + make)"
	@echo "  make test           - Run the regression test suite"
	@echo "  make test-verbose   - Run tests with verbose output"
	@echo "  make clean          - Remove build and test result files"
	@echo "  make help           - Show this help message"
	@echo ""
	@echo "Binary location: $(BTOR2RW_BIN)"
	@echo "Test directory:  $(TEST_DIR)"
