# BTOR2 to SMT2 Converter

A tool for loading BTOR2 format files and emitting SMT-LIB 2 files.

## Project Structure

```
Btor2ToSmt2/
├── CMakeLists.txt       # CMake build configuration
├── src/                 # Source code
│   ├── main.cpp
│   ├── IR.cpp / IR.h    # Intermediate representation
│   ├── Btor2Loader.cpp / Btor2Loader.h  # BTOR2 file loader
│   └── Smt2Emitter.cpp / Smt2Emitter.h  # SMT2 file emitter
├── build/               # Build directory
├── regress/             # Regression tests
│   ├── test_regress.sh   # Regression test script
│   └── Makefile          # Test Makefile
```

## Building

```bash
make build         # Build the project (cmake + make)
make test          # Run the regression test suite
make test-verbose  # Run with verbose output (shows skipped files)
make clean         # Remove build and test result files
make help          # Show all available targets
```

The binary will be created at `build/btor2rw`.

## Usage

```bash
# Load and emit SMT2 output
./build/btor2rw <input.btor2> -o <output.smt2>

# Load only (print summary)
./build/btor2rw <input.btor2>

# Verbose mode - prints all node details
./build/btor2rw -v <input.btor2> -o <output.smt2>

# Show help
./build/btor2rw -h
./build/btor2rw --help
```

### Options

| Option | Description |
|--------|-------------|
| `-o <file>` | Output SMT2 file path |
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
SMT2 emitted to: out.smt2
```

**Verbose mode** (includes node details):
```
=== BTOR2 Loaded Successfully ===
Total nodes:    5
Inputs:         2
Constraints:    1
Bads:           1
SMT2 emitted to: out.smt2

=== Nodes ===
Node 0: Input [width=1] name=input1 (btor2_id=2)
Node 1: Input [width=1] name=input2 (btor2_id=3)
Node 2: And [width=1] ops=[0, 1] (btor2_id=4)
...
```

### Test Coverage

The regression suite performs **three-layer verification** for each BTOR2 file:

```
Layer 1 — Loader:  exit code check (no crash)
Layer 2 — Emitter: SMT2 syntax validation via bitwuzla
Layer 3 — Semantic: sat/unsat match between BTOR2 and SMT2 via bitwuzla
```

Results are classified as:
- **Pass** — all three layers succeed, sat/unsat matches
- **Skip (unsupported)** — array sorts, overflow ops, parser errors, array read/write
- **Fail** — unexpected error or semantic mismatch

Requires `bitwuzla` in PATH (semantic checks are skipped if unavailable).

## Supported BTOR2 Operations

- **Constants**: `const`, `constd`, `consth`, `zero`, `one`, `ones`
- **Unary**: `not`, `neg`, `inc`, `dec`, `redand`, `redor`, `redxor`
- **Binary**: `and`, `or`, `xor`, `xnor`, `nand`, `nor`, `add`, `sub`, `mul`, `udiv`, `sdiv`, `urem`, `srem`, `smod`, `sll`, `srl`, `sra`, `rol`, `ror`
- **Comparison**: `eq`, `neq`, `ult`, `ulte`, `ugt`, `ugte`, `slt`, `slte`, `sgt`, `sgte`
- **Misc**: `ite`, `concat`, `slice`, `uext`, `sext`, `iff`, `implies`

## SMT2 Output Mapping

| BTOR2 | SMT-LIB | Notes |
|-------|---------|-------|
| `const/constd/consth/zero/one/ones` | `#b...` | Binary literal |
| `not` | `bvnot` | |
| `and` | `bvand` | |
| `or` | `bvor` | |
| `xor` | `bvxor` | |
| `add` | `bvadd` | |
| `sub` | `bvsub` | |
| `mul` | `bvmul` | |
| `udiv` | `bvudiv` | |
| `sdiv` | `bvsdiv` | |
| `urem` | `bvurem` | |
| `srem` | `bvsrem` | |
| `smod` | `bvsmod` | |
| `neg` | `bvneg` | |
| `sll` | `bvshl` | |
| `srl` | `bvlshr` | |
| `sra` | `bvashr` | |
| `inc` | `bvadd x (_ bv1 w)` | |
| `dec` | `bvsub x (_ bv1 w)` | |
| `ult` | `(ite (bvult a b) #b1 #b0)` | Comparison → Bool, wrap to `BitVec(1)` |
| `ulte` | `(ite (bvule a b) #b1 #b0)` | |
| `ugt` | `(ite (bvugt a b) #b1 #b0)` | |
| `ugte` | `(ite (bvuge a b) #b1 #b0)` | |
| `slt` | `(ite (bvslt a b) #b1 #b0)` | |
| `slte` | `(ite (bvsle a b) #b1 #b0)` | |
| `sgt` | `(ite (bvsgt a b) #b1 #b0)` | |
| `sgte` | `(ite (bvsge a b) #b1 #b0)` | |
| `eq` | `(ite (= a b) #b1 #b0)` | `=` → Bool, wrap to `BitVec(1)` |
| `neq` | `(ite (= a b) #b0 #b1)` | |
| `iff` | `(ite (bvcomp a b) #b1 #b0)` | `bvcomp` → `BitVec(1)`, wrap to Bool then `#b1/#b0` |
| `implies` | `(ite (bvor (bvnot a) b) #b1 #b0)` | `bvor` → Bool, wrap to `BitVec(1)` |
| `nand` | `bvnot (bvand a b)` | |
| `nor` | `bvnot (bvor a b)` | |
| `xnor` | `bvnot (bvxor a b)` | |
| `ite` | `ite` | **Condition must be Bool**; `BitVec(1)` condition needs `(= cond #b1)` |
| `concat` | `concat` | |
| `slice` | `((_ extract hi lo) x)` | |
| `uext` | `((_ zero_extend k) x)` | |
| `sext` | `((_ sign_extend k) x)` | |
| `redand` | `bvredand` | |
| `redor` | `bvredor` | |
| `redxor` | Handled via extract/xor | |
| `rol` | `bvor (bvshl ...) (bvlshr ...)` | |
| `ror` | `bvor (bvlshr ...) (bvshl ...)` | |
| `constraint` | `(assert (= <expr> #b1))` | |
| `bad` | `(assert (= <expr> #b1))` | |

## Unsupported Features

- Array sort operations
- Overflow detection operations (`uaddo`, `umulo`, etc.)
- Read/Write operations on arrays
