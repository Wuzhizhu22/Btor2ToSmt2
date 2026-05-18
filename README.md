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
./build/btor2rw <input.btor2>
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
