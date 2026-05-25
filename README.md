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
│   ├── test_regress.sh   # Unified regression test script (-s <solver>)
│   └── Makefile          # Test Makefile
```

## Building

```bash
make build             # Build the project (cmake + make)
make test              # Run the regression test suite (default: bitwuzla)
make test-verbose      # Run with verbose output (shows skipped files)
make test_all_solvers  # Run tests for all solvers (bitwuzla, z3, cvc5, yices)
make clean             # Remove build and test result files
make help              # Show all available targets
```

The binary will be created at `build/btor2rw`.

### Regression Test Suite

The unified test script supports multiple SMT2 solvers via `-s`:

```bash
# Default: bitwuzla (both reference and SMT2 solver)
./regress/test_regress.sh

# Z3 as SMT2 solver
./regress/test_regress.sh -s z3

# cvc5 as SMT2 solver
./regress/test_regress.sh -s cvc5

# Yices as SMT2 solver
./regress/test_regress.sh -s yices
```

**Common options:**

| Option | Description |
|--------|-------------|
| `-s`, `--solver <name>` | SMT2 solver: `bitwuzla` (default), `z3`, `cvc5`, `yices` |
| `-t <seconds>` | SMT2 solver timeout per case (default: 30) |
| `-b <seconds>` | BTOR2 reference timeout per case (default: 60) |
| `-d`, `--debug` | Debug mode — stops on first failure or mismatch |
| `-v`, `--verbose` | Verbose mode — lists all skipped files |

## Usage

```bash
# Default mode: combine all bads via OR into one assert (single file)
./build/btor2rw <input.btor2> -o <output.smt2>

# No-OR mode: each bad generates a separate file (_bad0.smt2, _bad1.smt2, ...)
./build/btor2rw --no-or-assert <input.btor2> -o <output.smt2>

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
| `--no-strict-smtlib` | Allow solver-extended constructs |
| `--no-or-assert` | Emit each bad to a separate file (default: combine via OR) |
| `-h`, `--help` | Show help message |

### Strict SMT-LIB Mode

By default, the converter emits only standard SMT-LIB `QF_BV` constructs for maximum cross-solver compatibility. Reduction operations are lowered to:
- `redand`: `(ite (= x ALL_ONES_w) #b1 #b0)`
- `redor`: `(ite (distinct x (_ bv0 w)) #b1 #b0)`

To allow solver-extended constructs like `bvredor` and `bvredand` (e.g., for Bitwuzla), use the `--no-strict-smtlib` flag.

### Symbol Comment Preservation

BTOR2 property lines may carry optional symbols, for example:

```btor2
7610 bad 7609 target_property
7611 constraint 7510 valid_env
```

The converter preserves these symbols as SMT-LIB comments (lines starting with `;`), which do not affect solver semantics but keep the original BTOR2 source information for debugging and result traceability.

**SMT2 output example:**

```smt2
; constraint btor2_id=7611 symbol=valid_env operand_ir=7509
(assert (= n7510 #b1))
; bad btor2_id=7610 symbol=target_property operand_ir=7609
(assert (= n7609 #b1))
```

**OR-combined mode** (default, multiple bads → single assert):

```smt2
; bad properties combined by OR:
;   bad[0] btor2_id=7610 symbol=target_property
;   bad[1] btor2_id=7620 symbol=another_property
(assert (or
  (= n7609 #b1)
  (= n7619 #b1)))
```

**`--no-or-assert` mode** — each `_badN.smt2` file includes the corresponding symbol comment:

```smt2
; bad btor2_id=7610 symbol=target_property operand_ir=7609
(assert (= n7609 #b1))
(check-sat)
```

Without a symbol on the BTOR2 line, the comment omits the `symbol=` field but still records the `btor2_id` and `operand_ir` for traceability.

### Output

**Default mode** (OR all bads, single file):
```
=== BTOR2 Loaded Successfully ===
Total nodes:    120371
Inputs:         15
Constraints:    1
Bads:           3
SMT2 emitted to: out.smt2
```

**No-OR mode** (`--no-or-assert`, each bad → separate file):
```
=== BTOR2 Loaded Successfully ===
Total nodes:    120371
Inputs:         15
Constraints:    1
Bads:           3
SMT2 emitted to: out_bad0.smt2
SMT2 emitted to: out_bad1.smt2
SMT2 emitted to: out_bad2.smt2
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

The regression suite performs **semantic equivalence verification** for each BTOR2 file:

```
1. BTOR2 → SMT2 conversion (btor2rw)
2. BTOR2 reference: bitwuzla --lang btor2 → sat/unsat
3. SMT2 solving:         selected solver → sat/unsat
4. Compare reference vs. SMT2 result
```

Results are classified as:

| Mark | Category | Meaning |
|:---:|------|------|
| `.` | **Pass** | Semantic match — reference and SMT2 agree |
| `S` | **Skip (unsupported)** | btor2rw doesn't support this feature |
| `T` | **Skip (BTOR2 timeout)** | bitwuzla reference timed out |
| `X` | **Skip (BTOR2 error)** | bitwuzla reference returned an error |
| `P` | **Skip (SMT2 timeout)** | SMT2 solver timed out |
| `E` | **Skip (SMT2 error)** | SMT2 solver rejected the syntax |
| `F` | **Fail** | btor2rw error or empty output |
| `M` | **Fail** | Semantic mismatch — results differ |

**Semantic match rate** is calculated only on cases with a valid reference:
```
match_rate = pass / (total - unsupported - BTOR2_timeout - BTOR2_error)
```

### Bitwuzla Reference Timeout

Some BTOR2 test cases (e.g., `lazyitex.btor2`, `rw60.btor2`) have highly variable bitwuzla solving times
in BTOR2 mode. The default 60-second BTOR2 reference timeout may occasionally skip these cases with `T`.
Adjust with `-b` if needed.

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
| `redand` | `(ite (= x ALL_ONES_w) #b1 #b0)` | Use `--no-strict-smtlib` to emit `bvredand` |
| `redor` | `(ite (distinct x (_ bv0 w)) #b1 #b0)` | Use `--no-strict-smtlib` to emit `bvredor` |
| `redxor` | Handled via extract/xor | |
| `rol` | `bvor (bvshl ...) (bvlshr ...)` | |
| `ror` | `bvor (bvlshr ...) (bvshl ...)` | |
| `constraint` | `(assert (= <expr> #b1))` | Symbol preserved as `; constraint` comment |
| `bad` | `(assert (= <expr> #b1))` | Symbol preserved as `; bad` comment |

## Unsupported Features

- Array sort operations
- Overflow detection operations (`uaddo`, `umulo`, etc.)
- Read/Write operations on arrays
