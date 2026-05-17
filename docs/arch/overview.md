# Qumir Compiler — Architecture Overview

Qumir is an educational programming language compiler with Russian keywords,
inspired by the Soviet-era KUMIR/Ershov teaching language.  It supports three
execution backends — LLVM native, WASM, and an IR interpreter (VM) — and an
online playground at <https://qumir.dev>.

---

## 1. Pipeline at a glance

```
Source (.kum / core-lang)
        │
        ▼
    [ Lexer / Parser ]
        │  raw AST
        ▼
 ┌─ Semantic passes ──────────────────────┐
 │  1. Name resolution                    │
 │  2. Type annotation                    │
 │  3. Definite assignment                │
 │  4. Transform (AST rewrites)           │
 └────────────────────────────────────────┘
        │  annotated AST
        ▼
  [ IR lowering ]  →  TModule (SSA IR)
        │
        ├── [ IR passes ]  (SSA, const-fold, de-SSA, …)
        │
        ├──────────────────────────────────────┐
        ▼                                      ▼
  [ VM / interpreter ]              [ LLVM codegen ]
  direct execution                  native binary / WebAssembly
```

The CLI tools are:
- **`qumiri`** — interpreter / LLVM JIT
- **`qumirc`** — compiler driver

The web service wraps `qumirc` as a subprocess and serves the playground
frontend.

---

## 2. Two surface languages

### 2.1 Kumir (`.kum`)

The primary user-facing language.  Keywords are Cyrillic:

| Concept       | Keyword(s)                                   |
|---------------|----------------------------------------------|
| Algorithm     | `алг` / `нач` / `кон`                        |
| Types         | `цел` `вещ` `лог` `лит` `сим` `таб`         |
| If / else     | `если` / `то` / `иначе` / `все`             |
| Loop          | `нц` / `кц` / `пока` / `для` / `шаг`       |
| Switch        | `выбор` / `при`                              |
| Param modes   | `арг` (in)  `рез`/`знач` (out)  `аргрез`   |
| Module import | `использовать`                               |

### 2.2 Core lang (internal)

A lower-level surface syntax for the same AST used for tests, golden files, and
debugging.  The playground exposes it as a "core syntax" mode.  Core lang is
useful because Kumir syntax is deliberately small and conservative: new
constructs can be introduced, inspected, and debugged at the AST level first,
before deciding whether they need `.kum` frontend syntax. See
[core-lang](core-lang.md) for the concrete syntax, supported AST forms, type
syntax, and printer conventions.

---

## 3. AST and type system

### 3.1 Node hierarchy

All AST nodes derive from `TExpr`.  Pattern matching uses `TMaybeNode<T>` —
a wrapper that holds a `shared_ptr<T>` when the dynamic type matches:

```cpp
if (auto call = TMaybeNode<TCallExpr>(expr)) { /* call.Cast() */ }
```

### 3.2 Type hierarchy

Types are `shared_ptr<TType>` subtypes, matched with `TMaybeType<T>`:

| C++ type           | Language concept                       |
|--------------------|----------------------------------------|
| `TIntegerType`     | `цел` — one 64-bit signed integer      |
| `TFloatType`       | `вещ` — IEEE-754 double                |
| `TBoolType`        | `лог` — boolean                        |
| `TStringType`      | `лит` — managed string handle          |
| `TSymbolType`      | `сим` — Unicode code point (i32)       |
| `TVoidType`        | procedure return / no value            |
| `TArrayType`       | `таб` — heap-allocated array           |
| `TNamedType`       | user-defined type alias                |
| `TFunctionType`    | `(param…) → ret`                       |
| `TPointerType`     | internal low-level pointer             |

---

## 4. Semantic passes

All four passes run in order via `NTransform::Pipeline()`.

### 4.1 Name resolution

Builds a symbol table and resolves every identifier to its declaration.
Module imports (`использовать`) are processed here; imported names are
registered before parsing the user's program body.

### 4.2 Type annotation

Infers and checks types bottom-up.  After this pass every expression node
carries a `TTypePtr`.

### 4.3 Definite assignment

Detects uses of potentially-uninitialised variables via dataflow analysis.

### 4.4 Transform

AST-level rewrites before IR lowering: array bound normalisation, syntactic
sugar elimination, `Loop` → `while` conversion.

---

## 5. IR (intermediate representation)

The IR is a custom SSA-like representation.

### 5.1 Structure

```
TModule
├── ExternalFunctions[]   imported runtime symbols
├── Functions[]
│   ├── Blocks[]
│   │   ├── Phis[]        φ-functions (SSA merge points)
│   │   └── Instrs[]      sequential instructions
│   └── LocalTypes[]      local variable type ids
├── GlobalTypes[]         module-level slot types
├── StringLiterals[]      constant pool
└── Types (TTypeTable)    type interning table
```

### 5.2 Instruction format

```cpp
struct TInstr {
    TOp      Op;             // opcode: "add", "call", "ret", "jmp", "cmp", …
    TTmp     Dest;           // destination temporary (-1 = no result)
    TOperand Operands[4];    // Tmp | Imm | Label | Local | Slot
};
```

Opcodes are string-literal hashes (`"add"_op`, `"call"_op`, …) so new
opcodes can be added without touching an enum.

### 5.3 IR passes

| Pass           | Purpose                                   |
|----------------|-------------------------------------------|
| `locals2ssa`   | stack locals → SSA temporaries            |
| `const_fold`   | constant folding                          |
| `de_ssa`       | insert copies at φ-joins before codegen   |
| renumber       | compact temporary indices                 |
| CFG analysis   | predecessor / successor computation       |

### 5.4 IR type table

`TTypeTable` interns IR-level types by kind: `I1`, `I8`, `I32`, `I64`,
`F64`, `Void`, `Ptr`, `Struct`, `Func`.  AST types map to IR types via
`FromAstType()`.

---

## 6. VM / interpreter

The VM is a register-based bytecode interpreter.  The IR is compiled to
`TVMInstr` bytecode by `TVMCompiler`, which assigns stack-frame byte offsets
to locals and temporaries.

### 6.1 Execution model

Each function call creates a stack frame (a heap-allocated byte buffer).
The interpreter loop dispatches on `EVMOp` enum values.  Frames are linked
(parent frame pointer stored at offset 0).

### 6.2 Calling convention for external (runtime) functions

External functions — robot actions, math helpers, string operations — are
registered by modules.  Each external function has **two representations**:

| Field    | Type                                          | Used by        |
|----------|-----------------------------------------------|----------------|
| `Ptr`    | `void*` — native function pointer             | LLVM backend   |
| `Packed` | `uint64_t(*)(const uint64_t* args, size_t n)` | VM interpreter |

The **`Packed` thunk** is the VM calling convention.  All arguments are
passed as a flat array of `uint64_t` values (integers, floats as bit-casts,
string handles, pointers as integers).  The return value is a single
`uint64_t`.

```cpp
// Typical registration
{
    .Name        = "вверх",
    .MangledName = "robot_up",
    .Ptr         = reinterpret_cast<void*>(static_cast<void(*)()>(robot_up)),
    .Packed      = [](const uint64_t*, size_t) -> uint64_t {
                       robot_up(); return 0;
                   },
}
```

The VM's `ECall` handler casts `addr` to `TPacked` and calls it directly,
collecting pre-pushed arguments from `Runtime.Args`.

Additional flags on external functions:

- **`RequireArgsMaterialization`** — string arguments are dereferenced from
  their handle before the call, so functions that expect `const char*` work
  without extra wrapping.
- **`Inline`** — an optional `TInlineFactory` that the type-annotation pass
  can use to replace the call with a different AST subtree.  Only the VM
  uses `Inline`; the LLVM/WASM backends continue to use `Ptr`.

### 6.3 LLVM JIT

The LLVM JIT runner compiles the IR module through the LLVM backend and runs
it in-process via LLVM ORC JIT.  External functions are resolved to their
`Ptr` addresses at link time.

---

## 7. LLVM backend

Translates `TModule` to LLVM IR and then to native code or WebAssembly.

### 7.1 Function lowering

IR basic blocks map directly to LLVM basic blocks.  IR temporaries become
LLVM values (`alloca`-free after the de-SSA pass).  Struct arguments and
returns are modelled as ordinary LLVM values.

### 7.2 Target

| Flag      | Triple                   | Notes                          |
|-----------|--------------------------|--------------------------------|
| *(none)*  | host triple              | native AOT or JIT              |
| `--wasm`  | `wasm32-unknown-unknown` | requires `wasm-ld` for linking |

### 7.3 Optimization

`cg.Emit(module, optLevel)` runs the standard LLVM pass pipeline at the
requested level (0–3).

---

## 8. Module system

User programs import modules with `использовать <Name>`.  Each module
implements `IModule` and registers external functions and types.

Built-in modules:

| Module             | Purpose                                   |
|--------------------|-------------------------------------------|
| System (default)   | I/O, file I/O, math, string operations    |
| Робот              | Grid robot executor                       |
| Черепаха           | Turtle graphics executor                  |
| Рисователь         | Canvas drawing                            |
| Чертежник          | Vector drawing                            |
| Комплексные числа  | Complex number support                    |
| Цвета              | Named color constants                     |

Runtime implementations exist in two forms: C++ (native / WASM) and
JavaScript (browser playground).

---

## 9. Runtime Data Representation

Arrays, structs, and strings have non-trivial ABI and lifetime conventions, so
the details live in separate notes: [arrays](arrays.md) describes `таб` as a
pointer to a flat heap buffer with compiler-maintained bounds/strides,
row-major multidimensional layout, and by-pointer function passing;
[structs](structs.md) describes struct layout, address-backed temporaries,
LLVM value ABI for arguments/returns, and reference-parameter copies; and
[strings](strings.md) describes `лит` as `TString::Data` / browser handles with
reference counting, ownership tracking, literal materialization, and
retain/release rules around calls, returns, assignments, and destruction.
