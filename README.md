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

## Feature Comparison with KUMIR

Legend: ✔ = supported, ~ = partially supported, ✖ = not supported / not yet implemented. Notes highlight semantic differences.

### Data Types & Core

| Feature | KUMIR | Qumir | Notes |
|---------|:-----:|:-----:|-------|
| `цел` (int) | ✔ | ✔ | |
| `вещ` (float) | ✔ | ✔ | |
| `лит` (string) | ✔ | ✔ | |
| `сим` (char) | ✔ | ✔ | |
| `лог` (bool) | ✔ | ✔ | |
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
| Overloading | ✖ | ✔ | Opt-in via `# qumir language: overloads` pragma (`(pragma language overloads)` in oz-lang); multiple `алг`/`fun` definitions with distinct parameter types form an overload set |

### Control Flow

| Construct | KUMIR | Qumir | Notes |
|-----------|:-----:|:-----:|-------|
| `нц для ... от .. до .. шаг ..` | ✔ | ✔ | Negative step allowed |
| `нц пока` (while) | ✔ | ✔ | |
| `нц` ... `кц при` (repeat‑until) | ✔ | ✔ | |
| `нц N раз` (repeat N times) | ✔ | ✔ | |
| Infinite `нц .. кц` | ✔ | ✔ |  |
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
| Other classic executors (Robot, Painter, Drawer, ...) | ✔ | ~ | Робот, Рисователь, Чертежник implemented; Водолей, Вертун, and a few minor executors not yet |

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
| `позиция(лит frag, лит s)` find | ✔ | ✔ | 1‑based index or 0 if not found |
| `поз(лит s, лит frag)` alias | ✔ | ✔ | Alias of `позиция` |
| `позиция после(цел start, лит frag, лит s)` find from | ✔ | ✔ | Search starting at position `start` |
| `поз после(цел start, лит frag, лит s)` alias | ✔ | ✔ | Alias of `позиция после` |
| `вставить(лит frag, аргрез лит s, арг цел pos)` insert | ✔ | ✖ | Not implemented |
| `заменить(аргрез лит s, арг лит old, арг лит neu, арг лог каждый)` replace | ✔ | ✖ | Not implemented |
| `удалить(аргрез лит s, арг цел pos, арг цел count)` delete | ✔ | ✔  |  |

> Qumir focuses on core search/length/code‑point operations first; mutation helpers (`вставить/заменить/удалить`) and case conversion may be added later with full Unicode support.

### Miscellaneous

| Capability | KUMIR | Qumir | Notes |
|------------|:-----:|:-----:|-------|
| LLVM JIT / AOT | ✖ | ✔ | IR & LLVM codegen |
| WebAssembly target | ✖ | ✔ | Optional (`--wasm`) |
| Streaming online playground | ✖ | ✔ | https://qumir.dev |
| Lazy logical evaluation | ✖ | ✔ | Short‑circuit like C/C++ |
| Multi‑dim arrays > 3 | ✖ | ✔ | No dimension cap |

> Anything marked ✖ here is a current gap, not a deliberate omission — Qumir
> aims for KUMIR-level compatibility in the `.kum` frontend wherever
> practical. If a `.kum` program doesn't behave like KUMIR, or a status in
> this table looks wrong, that's a bug: please
> [open an issue](https://github.com/resetius/qumir/issues) (or a PR) — the
> table will evolve with the language.

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

## From a Kumir compiler to an embeddable core

Qumir started as a from-scratch implementation of the **KUMIR** educational
language designed by Academician Andrei P. Ershov. The `.kum` frontend, its
semantics, and the executor modules (Turtle, Robot, Painter, ...) are
described in [docs/ru/about.md](docs/ru/about.md) (project story, in Russian)
and [docs/arch/overview.md](docs/arch/overview.md) (compiler architecture).

While building the compiler, the *internal* AST/IR — independent of the
Cyrillic `.kum` syntax — turned out to be useful on its own. In the codebase
and tests it's called **core lang** (a.k.a. **oz-lang**, after the `.oz`
extension of its test sources, e.g.
[test/regtest/cases/corelang](test/regtest/cases/corelang)): a small,
statically-typed, S-expression-like language backed by the same IR
interpreter, LLVM JIT/AOT backend, and WebAssembly output, also exposed as a
"core syntax" mode in the online playground. That makes the project
interesting beyond the Kumir frontend, e.g. for embedding a tiny typed
scripting/expression language in a C++ host, or JIT-compiling small
data-processing kernels via LLVM.

### Core lang (oz-lang) feature highlights

- **Typed functions** with explicit (`(return expr)`) and implicit
  (last-expression) returns: `(fun name (params) -> return_type body)`
- **Function overloading** — multiple `(fun name ...)` forms with distinct
  parameter types form an overload set, resolved by argument types
- **Generic functions** — `<named K (template)>` placeholders are
  monomorphized per call site, so one definition can be specialized for
  `i64`, `f64`, `string`, structs, etc.
- **References** — `<ref T>` parameters let a function mutate the caller's
  variable, field, or array element in place
- **Managed strings, arrays, and structs** — reference-counted, with
  compiler-inserted destructor calls, including on early exit via
  `break`/`continue`/`return`
- **Coroutines** — `<future T>`/`await` (see
  [docs/arch/coroutine.md](docs/arch/coroutine.md)) for streaming/async
  pipelines

### oz-lang examples

A few small, complete programs from
[test/regtest/cases/corelang](test/regtest/cases/corelang) — each is an
s-expression, and `(fun <main> () ...)` is the entry point.

**Typed functions, implicit return** (`corelang/implicit_return.oz`):

```core
(block
  (fun <main> ()
    (block
      (output (call square (: 5 i64)) "\n")
      (output (call cube (: 3 i64)) "\n")))

  (fun square ((var x i64)) -> i64
    (block
      (* x x)))

  (fun cube ((var x i64)) -> i64
    (block
      (var sq i64)
      (= sq (* x x))
      (* sq x))))
```

`square` and `cube` have no `(return ...)` — the value of the block's last
expression is returned implicitly. Output: `25` then `27`.

**Generic functions** (`corelang/generic_identity.oz`):

```core
(block
  (pragma language overloads)

  (fun <main> ()
    (block
      (output (call identity (: 5 i64)) "\n")
      (output (call identity "hello") "\n")
      (output (call identity (: 3.5 f64)) "\n")))

  (fun identity ((var x <named K (template readable mutable)>))
        -> <named K (template readable mutable)>
    (block
      (return x))))
```

One `identity` definition is monomorphized per call site into
`__generic_identity$Int`, `__generic_identity$String`, and
`__generic_identity$Float`. Output: `5`, `hello`, `3.5`.

**References into arrays** (`corelang/array_ref.oz`):

```core
(block
  (fun <main> ()
    (block
      (var a <array i64 1> [0 2])
      (= a [0] (: 10 i64))
      (= a [1] (: 20 i64))
      (call bump (index a (: 1 i64)))
      (output (index a (: 0 i64)) " " (index a (: 1 i64)) "\n")))

  (fun bump ((var x <ref i64>))
    (block
      (= x (+ x (: 7 i64))))))
```

`bump` takes `<ref i64>`, so `(= x ...)` writes back through to `a[1]` in the
caller. Output: `10 27`.

See [docs/arch/core-lang.md](docs/arch/core-lang.md) for the full syntax, AST
forms, and type system.

### Embedding example

A host application can register its own module — a set of *external
functions* with C++ implementations — and then run oz-lang source against it.

**1. A module with one external function** (`f64 -> f64`):

```cpp
#include <qumir/modules/module.h>
#include <qumir/parser/ast.h>

namespace NQumir::NRegistry {

double Square(double x) { return x * x; }

class MyModule : public IModule {
public:
    MyModule() {
        auto f64 = std::make_shared<NAst::TFloatType>();
        ExternalFunctions_ = {{
            .Name = "square",
            .MangledName = "my_square",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(Square)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return std::bit_cast<uint64_t>(Square(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { f64 },
            .ReturnType = f64,
        }};
    }

    const std::string& Name() const override { static std::string n = "Square"; return n; }
    const std::vector<TExternalFunction>& ExternalFunctions() const override { return ExternalFunctions_; }
    const std::vector<TExternalType>& ExternalTypes() const override { return ExternalTypes_; }
    const std::vector<TLiteralSuffix>& LiteralSuffixes() const override { return LiteralSuffixes_; }
    const std::vector<std::string>& Dependencies() const override { return Dependencies_; }

private:
    std::vector<TExternalFunction> ExternalFunctions_;
    std::vector<TExternalType> ExternalTypes_ = {};
    std::vector<TLiteralSuffix> LiteralSuffixes_ = {};
    std::vector<std::string> Dependencies_ = {};
};

} // namespace NQumir::NRegistry
```

`Ptr` is the native function pointer used by the LLVM JIT/AOT and WASM
backends; `Packed` is the IR interpreter's calling convention — arguments and
the return value travel as bit-cast `uint64_t`s. See
`qumir/modules/module.h` and e.g. `qumir/modules/system/system.cpp` for more
field combinations (string args, inline expansion, operators, ...).

**2. Run oz-lang source against it**, mirroring the pipeline used by
`TIRRunner` (`qumir/runner/runner_ir.cpp`): parse → resolve names → run
semantic transforms → lower to IR → interpret.

```cpp
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/eval.h>

const char* src = R"__(
(block
  (use "Square")
  (fun <main> ()
    (block
      (output (call square 6.0) "\n"))))
)__";

NQumir::NSemantics::TNameResolver resolver;
NQumir::NRegistry::MyModule mySquare;
resolver.RegisterModule(&mySquare); // makes "Square" available to (use "Square")

std::istringstream in(src);
NQumir::NAst::NCore::TTokenStream ts(in);
NQumir::NAst::NCore::TParser parser;
auto ast = parser.Parse(ts).value();   // error handling omitted for brevity
resolver.ApplyPragmas(parser.Pragmas);
resolver.GetOrCreateRootScope()->RootLevel = false;
resolver.Resolve(ast);
NQumir::NTransform::Pipeline(ast, resolver);

NQumir::NIR::TModule module;
NQumir::NIR::TBuilder builder(module);
NQumir::NIR::TAstLowerer(module, builder, resolver).LowerTop(ast);

NQumir::NIR::TInterpreter interp(module, std::cout, std::cin);
interp.Eval(*module.GetEntryPoint(), {}, {}); // prints square(6.0)
```

Swap the last block for the pipeline in `qumir/runner/runner_llvm.cpp` to
JIT-compile and run the same program through LLVM instead of the IR
interpreter.

**3. Build (and modify) the AST directly — no oz-lang text at all.**

AST nodes are plain `shared_ptr<TExpr>` trees with ordinary public fields, so
a host program can construct or rewrite a program without going through the
lexer/parser at all. The same `(block (use ...) (fun <main> () ...))` program
from example 2 looks like this in C++:

```cpp
using namespace NQumir::NAst;
TLocation loc{};

// square(6.0)
auto arg  = std::make_shared<TNumberExpr>(loc, 6.0);
auto call = std::make_shared<TCallExpr>(loc,
    std::make_shared<TIdentExpr>(loc, "square"), std::vector<TExprPtr>{ arg });

// output(square(6.0), "\n")
auto out = std::make_shared<TOutputExpr>(loc, std::vector<TOutputArg>{
    { call, nullptr, nullptr },
    { std::make_shared<TStringLiteralExpr>(loc, "\n"), nullptr, nullptr },
});

// (fun <main> () (block (output ...)))
auto body = std::make_shared<TBlockExpr>(loc, std::vector<TExprPtr>{ out });
auto mainFun = std::make_shared<TFunDecl>(
    loc, "<main>", std::vector<TParam>{}, body, std::make_shared<TVoidType>());

TExprPtr program = std::make_shared<TBlockExpr>(loc, std::vector<TExprPtr>{ mainFun });

NQumir::NSemantics::TNameResolver resolver;
NQumir::NRegistry::MyModule mySquare;
resolver.RegisterModule(&mySquare);
resolver.ImportModule(mySquare.Name()); // no (use "Square") node needed

resolver.GetOrCreateRootScope()->RootLevel = false;
resolver.Resolve(program);
NQumir::NTransform::Pipeline(program, resolver);

NQumir::NIR::TModule module;
NQumir::NIR::TBuilder builder(module);
NQumir::NIR::TAstLowerer(module, builder, resolver).LowerTop(program);
NQumir::NIR::TInterpreter(module, std::cout, std::cin)
    .Eval(*module.GetEntryPoint(), {}, {}); // prints square(6.0)

// --- edit the AST in place and rebuild — no re-lexing/re-parsing ---
arg->FloatValue = 7.0; // square(6.0) -> square(7.0)

NQumir::NIR::TModule module2;
NQumir::NIR::TBuilder builder2(module2);
NQumir::NIR::TAstLowerer(module2, builder2, resolver).LowerTop(program);
NQumir::NIR::TInterpreter(module2, std::cout, std::cin)
    .Eval(*module2.GetEntryPoint(), {}, {}); // prints square(7.0)
```

Swapping `arg->FloatValue` is a one-line field assignment on a shared AST
node — the same mechanism the compiler itself uses internally for
AST-level rewrites: the `transform` pass, generic-function monomorphization,
and the `Inline`/`InlineFactory` hooks that modules use to splice in
hand-built subtrees (see `qumir/modules/colors/colors.cpp` for an extensive
example of building expression trees programmatically).

## Repository layout

- `qumir/` — core library: parser, IR, semantics, runtime, codegen
  - `parser/` — lexer and parser
  - `ir/` — IR, lowering from AST
  - `codegen/llvm/` — LLVM codegen, runner, and initialization
  - `runtime/` — standard runtime (math, I/O)
  - `runner/` — IR and LLVM runners
- `bin/` — CLI tools: `qumiri` (interpreter/JIT), `qumirc` (compiler)
- `test/` — unit and regression tests; see `test/regtest/cases/*.kum`
- `docs/arch/` — architecture notes: pipeline overview, [core lang](docs/arch/core-lang.md), coroutines, and IR/runtime data representation (arrays, strings, structs)

## License

This project is licensed under the BSD 2-Clause License (BSD-2-Clause). See the LICENSE file in the repository root for details.

## Acknowledgements

- Based on and inspired by the educational language of Academician Andrei P. Ershov (Ершов) and the KUMIR tradition.
- Thanks to the LLVM project for the compiler infrastructure tools.

---

Try it online: https://qumir.dev
