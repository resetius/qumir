# Qumir

Qumir is a compiler and runtime for the Kumir educational programming language. It accepts the familiar Russian-keyword `.kum` syntax and can execute programs with its own IR interpreter, compile them with LLVM, or produce WebAssembly for the browser.

The same compiler pipeline also exposes **core lang** (historically called **oz-lang**): a small typed S-expression language intended for compiler experiments and embedding in C++ applications. Both frontends share the AST, semantic passes, IR, interpreter, LLVM JIT/AOT backend, and WebAssembly target.

- Try it online: [qumir.dev](https://qumir.dev)
- User documentation: [docs/ru/index.md](docs/ru/index.md)
- Language syntax: [docs/ru/syntax.md](docs/ru/syntax.md)
- Examples: [docs/ru/examples.md](docs/ru/examples.md)
- Compiler architecture: [docs/arch/overview.md](docs/arch/overview.md)
- Core lang reference: [docs/arch/core-lang.md](docs/arch/core-lang.md)

## Contents

- [What is included](#what-is-included)
- [Quick start](#quick-start)
- [Command-line tools](#command-line-tools)
- [Compiler architecture](#compiler-architecture)
- [Core lang and embedding](#core-lang-and-embedding)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [License](#license)

## What is included

Qumir provides:

- the Kumir-compatible `.kum` frontend with Russian keywords;
- scalar types, strings, arrays, functions, references, control flow, and file I/O;
- the Robot, Turtle, Painter, Drawer, Colors, Complex Numbers, and Keyboard modules;
- an IR interpreter for fast execution and compiler debugging;
- LLVM JIT and ahead-of-time native compilation;
- WebAssembly output and the browser runtime used by the online playground;
- core lang, a lower-level textual representation of the compiler AST.

The project aims for practical compatibility with Kumir programs and teaching materials. If a `.kum` program behaves differently from Kumir, please [open an issue](https://github.com/resetius/qumir/issues) with a minimal example.

## Quick start

Requirements:

- CMake 3.30 or newer;
- a C++23 compiler;
- LLVM 20 or newer;
- Ninja or another CMake-supported build tool;
- `wasm-ld` when building WebAssembly programs.

Build the project:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The resulting tools are `build/bin/qumiri` and `build/bin/qumirc`.

Run a program directly:

```bash
build/bin/qumiri -i test/regtest/cases/io/output.kum
```

Compile a native executable:

```bash
build/bin/qumirc hello.kum -o hello
./hello
```

A minimal `.kum` program looks like this:

```text
алг main
нач
    вывод "Привет, мир!", нс
кон
```

Run the test suite with:

```bash
cmake -S . -B build -DQUMIR_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Command-line tools

### `qumiri`

`qumiri` parses and executes source from a file or standard input. It uses the IR interpreter by default; pass `--jit` to execute through LLVM.

```bash
build/bin/qumiri -i program.kum
build/bin/qumiri --jit -O2 -i program.kum
build/bin/qumiri --print-ast --print-ir -i program.kum
build/bin/qumiri --core -i test/regtest/cases/corelang/implicit_return.oz
```

Use `build/bin/qumiri --help` for the complete and current option list. See [docs/ru/interpreter.md](docs/ru/interpreter.md) for a guided reference.

### `qumirc`

`qumirc` compiles source to a native executable, object file, assembly, LLVM IR, the internal IR, an AST dump, or WebAssembly.

```bash
build/bin/qumirc program.kum -o program
build/bin/qumirc --llvm program.kum -o program.ll
build/bin/qumirc --wasm program.kum -o program.wasm
```

Use `build/bin/qumirc --help` for all output modes and optimization flags. See [docs/ru/compiler.md](docs/ru/compiler.md) for more examples.

## Compiler architecture

Both source languages use the same pipeline:

```text
.kum or core lang source
        -> lexer and parser
        -> name resolution and type annotation
        -> definite-assignment checks and AST transforms
        -> custom SSA-like IR
        -> IR interpreter, LLVM native code, or WebAssembly
```

The command-line tools are thin wrappers around the reusable `qumir` library. Built-in modules register external types and functions with the name resolver; native LLVM execution uses ordinary function pointers, while the IR interpreter uses packed thunks.

For the component map, IR model, runtime ABI, and module system, start with [docs/arch/overview.md](docs/arch/overview.md). Separate notes cover [arrays](docs/arch/arrays.md), [strings](docs/arch/strings.md), [structs](docs/arch/structs.md), and [coroutines](docs/arch/coroutine.md).

## Core lang and embedding

Core lang, also known as oz-lang after the `.oz` files in [test/regtest/cases/corelang](test/regtest/cases/corelang), is the internal surface syntax for the Qumir AST. It is useful when a host application needs a compact typed language without adopting the Russian `.kum` syntax.

It supports typed and overloaded functions, generic specialization, references, managed strings, arrays, structs, and coroutines. The full grammar and type syntax are documented in [docs/arch/core-lang.md](docs/arch/core-lang.md).

Example:

```core
(block
  (fun <main> ()
    (block
      (output (call square (: 5 i64)) "\n")))

  (fun square ((var x i64)) -> i64
    (block
      (* x x))))
```

Run it through the interpreter or JIT:

```bash
build/bin/qumiri --core -i program.oz
build/bin/qumiri --core --jit -O2 -i program.oz
```

### Embedding in C++

The public library exposes two high-level runners:

- `NQumir::TIRRunner` parses, resolves, lowers, and executes through the IR interpreter;
- `NQumir::TLLVMRunner` runs the same pipeline through LLVM and can compile a core-lang kernel to a function pointer with `CompileKernel()` or `CompileKernelAst()`.

For core input, set `CoreInput = true` in `TIRRunnerOptions` or `TLLVMRunnerOptions`:

```cpp
#include <qumir/runner/runner_ir.h>

#include <iostream>
#include <sstream>

std::istringstream source(R"(
(block
  (fun <main> ()
    (block
      (output "hello from core lang\n"))))
)");

NQumir::TIRRunner runner(
    std::cout,
    std::cin,
    NQumir::TIRRunnerOptions {
        .CoreInput = true,
    });

auto result = runner.Run(source);
if (!result) {
    std::cerr << result.error().ToString();
    return 1;
}
```

To expose host functionality to the language, implement `NQumir::NRegistry::IModule` from `qumir/modules/module.h`. Register it with `TLLVMRunner::RegisterModule()` for the LLVM path, or with `TNameResolver::RegisterModule()` when assembling the lower-level IR pipeline:

- `Ptr` is the native function pointer used by LLVM;
- `Packed` is the interpreter thunk using `uint64_t` argument and result slots;
- `ArgTypes` and `ReturnType` define the signature visible to semantic analysis;
- `ExternalTypes`, `LiteralSuffixes`, and `Dependencies` extend the language surface when needed.

The built-in [Turtle module](qumir/modules/turtle/turtle.cpp) is a compact example. [System module](qumir/modules/system/system.cpp) shows strings, I/O, operators, and a broader set of function signatures.

For lower-level control, a host can parse core lang with `qumir/parser/core`, construct or transform `NAst::TExpr` trees directly, run `NTransform::Pipeline`, lower with `NIR::TAstLowerer`, and then choose the interpreter or LLVM backend. This is the same pipeline used by the runners and command-line tools.

For browser embedding, compile with `qumirc --wasm` and provide the imported host functions. The JavaScript runtime used by the playground is in [service/static/runtime](service/static/runtime); an HTTP compiler service is only needed when source must be compiled on demand.

## Repository layout

- `qumir/` contains the reusable compiler, runtime interfaces, modules, runners, IR, and LLVM backend.
- `bin/` contains `qumiri` and `qumirc`.
- `test/` contains unit and regression tests, including `.kum` and `.oz` programs.
- `examples/` contains complete Kumir programs grouped by topic and executor.
- `service/` contains the online playground service and browser runtime.
- `docs/` contains user documentation, architecture notes, and design issues.

## Documentation

- [User documentation](docs/ru/index.md)
- [Syntax reference](docs/ru/syntax.md)
- [Standard functions](docs/ru/standard-functions.md)
- [Examples](docs/ru/examples.md)
- [FAQ](docs/ru/faq.md)
- [Architecture overview](docs/arch/overview.md)
- [Core lang reference](docs/arch/core-lang.md)
- [Code style](docs/arch/codestyle.md)

## License

Qumir is distributed under the BSD 2-Clause License. See [LICENSE](LICENSE).
