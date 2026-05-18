# BTOR2 to SMT2 Converter

A tool for loading and processing BTOR2 format files.

## Project Structure

```
Btor2ToSmt2/
├── CMakeLists.txt       # CMake build configuration
├── src/                 # Source code
│   ├── main.cpp
│   ├── IR.cpp / IR.h    # Intermediate representation
│   └── Btor2Loader.cpp / Btor2Loader.h  # BTOR2 file loader
├── build/               # Build directory
├── regress/            # Regression tests
    ├── test_loader.sh  # Test script
    └── Makefile        # Test Makefile
```

## Building

```bash
mkdir -p build
cd build
cmake ..
make
```

The binary will be created at `build/btor2rw`.

## Usage

```bash
# Basic usage - loads and prints summary
./build/btor2rw <input.btor2>

# Verbose mode - prints all node details
./build/btor2rw -v <input.btor2>
./build/btor2rw --verbose <input.btor2>

# Show help
./build/btor2rw -h
./build/btor2rw --help
```

### Options

| Option | Description |
|--------|-------------|
| `-v`, `--verbose` | Enable verbose output (print all nodes) |
| `-h`, `--help` | Show help message |

### Output

**Default mode** (summary only):
```
=== BTOR2 Loaded Successfully ===
Total nodes:    120371
Inputs:         15
Constraints:    1
Bads:           1
```

**Verbose mode** (includes node details):
```
=== BTOR2 Loaded Successfully ===
Total nodes:    5
Inputs:         2
Constraints:    1
Bads:           1

=== Nodes ===
Node 0: Input [width=1] name=input1 (btor2_id=2)
Node 1: Input [width=1] name=input2 (btor2_id=3)
Node 2: And [width=1] ops=[0, 1] (btor2_id=4)
...
```

## Running Tests

```bash
cd regress
make test      # Run test suite
make run       # Run with verbose output
make clean     # Clean test results
```

## Supported BTOR2 Operations

- **Constants**: `const`, `constd`, `consth`, `zero`, `one`, `ones`
- **Unary**: `not`, `neg`, `inc`, `dec`, `redand`, `redor`, `redxor`
- **Binary**: `and`, `or`, `xor`, `xnor`, `nand`, `nor`, `add`, `sub`, `mul`, `udiv`, `sdiv`, `urem`, `srem`, `smod`, `sll`, `srl`, `sra`, `rol`, `ror`
- **Comparison**: `eq`, `neq`, `ult`, `ulte`, `ugt`, `ugte`, `slt`, `slte`, `sgt`, `sgte`
- **Misc**: `ite`, `concat`, `slice`, `uext`, `sext`, `iff`, `implies`

## Unsupported Features

- Array sort operations
- Overflow detection operations (`uaddo`, `umulo`, etc.)
- Read/Write operations on arrays
