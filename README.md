# Qumir

Qumir is a tiny experimental programming language and toolchain with Russian keywords, inspired by KUMIR and the educational language designed by Academician Andrei P. Ershov. It includes a parser, semantic passes, a simple IR, an interpreter, and an LLVM-based code generator/JIT and ahead-of-time compiler.

- Online playground: https://qumir.dev

- Interpreter: `qumiri` (IR or LLVM JIT execution)
- Compiler driver: `qumirc` (emit LLVM IR/ASM/OBJ, link native; can also target WebAssembly)

The language is intentionally small and approachable, borrowing many ideas and surface syntax from Ershov-style teaching languages, while experimenting with a modern implementation.

## Features

- Russian-keyword syntax: переменные и операторы как в классическом «КуМир»/языке Ершова
- Variables and basic types: `цел` (int), `вещ` (float), `лог` (bool), `лит` (string), arrays `таб`
- Expressions and operators: `+ - * / **`, comparisons `= <> < <= > >=`, logical `и`, `или`, `не`
- Control flow: `если/то/иначе/все`, loops `нц/кц`, `нц пока`, `нц для ... от ... до ... шаг ...`, `кц при` (repeat-until)
- Switch-like multi-branch: `выбор` / `при` / `иначе`
- I/O: `ввод`, `вывод`, simple formatting with string literals and `нс` (newline)
- Functions: `алг функция`, with arguments `арг`, result `знач`
- Backends: IR interpreter and LLVM (JIT or AOT)
- Optional WebAssembly output (wasm32)

## Build

Prerequisites:
- CMake >= 3.30
- C++23 compiler (GCC/Clang); some subtargets enable C++20 features
- LLVM >= 20 (required for the LLVM codegen/JIT)
- Ninja (recommended), or any Makefile generator supported by CMake
- For tests: pkg-config and GoogleTest (gtest)
- For WebAssembly (optional): `wasm-ld` from LLVM/binutils toolchain

Build steps (Release):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binaries will be in `build/bin/`:
- `qumiri` — interpreter / JIT runner
- `qumirc` — compiler driver

Run tests (optional):

```bash
cd build
ctest --output-on-failure
```

## Usage

### Interpreter (`qumiri`)

```
qumiri [options]
  --jit                 Enable LLVM JIT (default is IR interpreter)
  --time-us             Print evaluation time in microseconds
  --print-ast           Print AST after parsing
  --print-ir            Print IR after lowering
  --print-llvm          Print LLVM IR after codegen (JIT only)
  --input-file|-i FILE  Read program from FILE (default: stdin)
  -O[0|1|2|3]           Optimization level for JIT
  --help, -h            Show help
```

Examples:

```bash
# Run a program from a file with the IR interpreter
build/bin/qumiri -i test/regtest/cases/if/if.kum

# Run with LLVM JIT and show timing
build/bin/qumiri --jit --time-us -i test/regtest/cases/while/while_loop.kum
```

### Compiler (`qumirc`)

```
qumirc [options] <input.kum>
  -c                 Compile only (emit .o or, with -S, .s)
  -o FILE            Output file (default: a.out or <input>.{o,s,ll,ir,ast,wasm})
  --ast              Dump AST to <input>.ast
  --ir               Dump IR to <input>.ir
  --llvm             Dump LLVM IR to <input>.ll
  -S                 Emit assembly (implies -c)
  -O[0|1|2|3]        Optimization level
  --wasm             Target wasm32 (produces .wasm when linking)
  --version, -v      Print version
  --help, -h         Show help
```

Examples:

```bash
# Compile and link a native executable (a.out by default)
build/bin/qumirc test/regtest/cases/io/output.kum

# Emit LLVM IR only
build/bin/qumirc --llvm -o out.ll test/regtest/cases/for/for_step1.kum

# Object file and assembly
build/bin/qumirc -c test/regtest/cases/assign/int_expr.kum    # emits .o
build/bin/qumirc -S test/regtest/cases/assign/int_expr.kum    # emits .s

# Target WebAssembly (requires wasm-ld)
build/bin/qumirc --wasm -o func.wasm test/regtest/cases/io/output.kum
```

## Language overview (examples)

Below are small, self-contained examples reflecting the language surface. Many are taken or adapted from `test/regtest/cases`.

### Hello world / basic I/O

```text
алг
нач
    вывод "Привет, мир!", нс
кон
```

### Variables and assignment

```text
алг функция нач
    цел i
    вещ f
    лог b
    i := 2
    f := -2.0
    b := истина
кон
```

### If / else

```text
алг
нач
    цел i
    если истина
    то
        i := 1
    иначе
        i := 2
    все
кон
```

### While loop

```text
алг
нач
    цел ф
    ф := 0
    нц пока ф < 10
        ф := ф + 1
    кц
кон
```

### For loop (with/without step)

```text
алг функция нач
    цел i
    вещ j
    j := 0
    нц для i от 0 до 10
        j := j + 1.0
    кц
кон
```

With explicit step (can be negative):

```text
алг функция нач
    цел i
    вещ j
    j := 0
    нц для i от 0 до 10 шаг 1
        j := j + 1.0
    кц
кон
```

### Repeat–until (post-condition)

```text
алг
нач
    цел ф
    ф := 0
    нц
        ф := ф + 1
    кц при ф = 10
кон
```

### Switch-like branching

```text
алг
нач
    цел ф
    ф := 0
    выбор
        при ф < 10:
            ф := ф + 1
        при ф > 20:
            ф := 102
        иначе ф := -202
    все
кон
```

### Functions, arguments, and result

```text
алг функция(рез цел r, арг вещ x, лог y) нач
    цел i, j, k, вещ a, b, c
кон
```

### Arrays (declaration)

```text
цел таб a[0:10], b[0:20], вещ таб l[-1:234], лог таб t[-10:10]
```

## Repository layout

- `qumir/` — core library: parser, IR, semantics, runtime, codegen
  - `parser/` — lexer and parser
  - `ir/` — IR, lowering from AST
  - `codegen/llvm/` — LLVM codegen, runner, and initialization
  - `runtime/` — standard runtime (math, I/O)
  - `runner/` — IR and LLVM runners
- `bin/` — CLI tools: `qumiri` (interpreter/JIT), `qumirc` (compiler)
- `test/` — unit and regression tests; see `test/regtest/cases/*.kum`

## License

This project is licensed under the BSD 2-Clause License (BSD-2-Clause). See the LICENSE file in the repository root for details.

## Acknowledgements

- Based on and inspired by the educational language of Academician Andrei P. Ershov (Ершов) and the KUMIR tradition.
- Thanks to the LLVM project for the compiler infrastructure tools.

## Feature Comparison with KUMIR

Legend: ✔ = supported, ✖ = not supported / not yet implemented. Notes highlight semantic differences.

### Data Types & Core

| Feature | KUMIR | Qumir | Notes |
|---------|:-----:|:-----:|-------|
| `цел` (int) | ✔ | ✔ | |
| `вещ` (float) | ✔ | ✔ | |
| `лит` (string) | ✔ | ✔ | |
| `сим` (char) | ✔ | ✖ | Indexing a string returns a one‑character `лит` |
| `лог` (bool) | ✔ | ✖ | |
| `таб` (arrays) | ✔ | ✔ | Dimension count not limited to 3 in Qumir |
| Index ranges `a[L:R]` | ✔ | ✔ | |

### Operators & Expressions

| Operator / Group | KUMIR | Qumir | Notes |
|------------------|:-----:|:-----:|-------|
| Arithmetic `+ - * /` | ✔ | ✔ | |
| Exponentiation `**` | ✔ | ✔ | |
| Comparisons `= <> < <= > >=` | ✔ | ✔ | |
| Logical `и` (and) | ✔ | ✔ | Short‑circuit |
| Logical `или` (or) | ✔ | ✔ | Short‑circuit |
| Logical `не` (not) | ✔ | ✔ | Prefix operator |
| String concatenation | ✔ | ✔ | Same `+` operator |
| Precedence & parentheses | ✔ | ✔ | C‑like precedence ordering |

### Function Parameters

| Feature | KUMIR | Qumir | Notes |
|---------|:-----:|:-----:|-------|
| Input `арг` | ✔ | ✔ | Passed by value |
| Output `рез/знач` | ✔ | ✔ | Result via out parameter |
| Inout `аргрез` | ✔ | ✔ | Combines input & modification |
| Overloading | ✖ | ✖ | Not supported |

### Control Flow

| Construct | KUMIR | Qumir | Notes |
|-----------|:-----:|:-----:|-------|
| `нц для ... от .. до .. шаг ..` | ✔ | ✔ | Negative step allowed |
| `нц пока` (while) | ✔ | ✔ | |
| `нц` ... `кц при` (repeat‑until) | ✔ | ✔ | Post‑condition loop |
| `нц N раз` (repeat N times) | ✔ | ✖ | Emulate with `нц для` |
| Infinite `нц .. кц` | ✔ | ✖ | Emulate with `нц пока истина` |
| `выбор / при / иначе` (switch) | ✔ | ✔ | Multi‑branch selection |
| `если/то/иначе/все` (if/else) | ✔ | ✔ | |

### Executors (Turtle & Others)

| Executor / Function | KUMIR | Qumir | Notes |
|---------------------|:-----:|:-----:|-------|
| Turtle base | ✔ | ✔ | Movement & rotation |
| `вперёд(len)` / `назад(len)` | ✔ | ✔ | |
| `влево(угол)` / `вправо(угол)` | ✔ | ✔ | |
| Pen up/down | ✔ | ✔ | Functions: `поднять хвост`, `опустить хвост` (tail up/down) |
| Save/restore state | ✖ | ✔ | State stack (pos, angle, pen). Functions: `сохранить состояние`, `восстановить состояние` |
| Other classic executors (Robot, Drawing, etc.) | ✔ | ✖ | Not implemented |

### String Algorithms / Functions

| Function | KUMIR | Qumir | Notes |
|----------|:-----:|:-----:|-------|
| `длин(лит s)` length | ✔ | ✔ | Returns number of characters |
| `код(сим c)` CP‑1251 code | ✔ | ✖ | Not implemented (use `юникод` for Unicode) |
| `юникод(сим c)` Unicode code point | ✔ | ✔ | Qumir returns Unicode scalar value |
| `символ(цел n)` CP‑1251 to char | ✔ | ✖ | Not implemented (Unicode: use `юнисимвол`) |
| `юнисимвол(цел n)` Unicode to char | ✔ | ✔ | Creates single‑char string from code point |
| `верхний регистр(лит s)` to upper | ✔ | ✖ | Not implemented (future: Unicode upper) |
| `нижний регистр(лит s)` to lower | ✔ | ✖ | Not implemented (future: Unicode lower) |
| `позиция(лит s, лит frag)` find | ✔ | ✔ | 1‑based index or 0 if not found |
| `поз(лит s, лит frag)` alias | ✔ | ✔ | Alias of `позиция` |
| `позиция после(цел start, лит s, лит frag)` find from | ✔ | ✔ | Search starting at position `start` |
| `поз после(цел start, лит s, лит frag)` alias | ✔ | ✔ | Alias of `позиция после` |
| `вставить(лит frag, аргрез лит s, арг цел pos)` insert | ✔ | ✖ | Not implemented |
| `заменить(аргрез лит s, арг лит old, арг лит neu, арг лог каждый)` replace | ✔ | ✖ | Not implemented |
| `удалить(аргрез лит s, арг цел pos, арг цел count)` delete | ✔ | ✖ | Not implemented |

> Qumir focuses on core search/length/code‑point operations first; mutation helpers (`вставить/заменить/удалить`) and case conversion may be added later with full Unicode support.

### Miscellaneous

| Capability | KUMIR | Qumir | Notes |
|------------|:-----:|:-----:|-------|
| LLVM JIT / AOT | ✖ | ✔ | IR & LLVM codegen |
| WebAssembly target | ✖ | ✔ | Optional (`--wasm`) |
| Streaming online playground | ✖ | ✔ | https://qumir.dev |
| Lazy logical evaluation | ✖ | ✔ | Short‑circuit like C/C++ |
| Multi‑dim arrays > 3 | ✖ | ✔ | No dimension cap |

> If a Qumir status looks wrong, please open an issue or PR — the table will evolve with the language.

---

Try it online: https://qumir.dev
