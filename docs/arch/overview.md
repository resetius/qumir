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

A lower-level, ASCII-only representation of the same AST used for tests and
debugging.  The playground exposes it as a "core syntax" mode.  It allows
writing test fixtures and viewing the de-sugared program structure without
going through the full Cyrillic front-end.

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

## 9. Array Representation

Kumir arrays are written with `таб`:

```kumir
цел таб v[1:n]
вещ таб a[0:n-1, 0:m-1]
```

The AST type is `TArrayType(elementType, arity)`.  The bounds are not part of
the type identity itself; they live on the declaration (`TVarStmt` /
function parameter) as one `[left:right]` pair per dimension.  In core/AST
goldens this shows up as a type plus a separate bounds list:

```text
(var a <array f64 2> [0 (- n 1)] [0 (- m 1)])
```

### 9.1 Runtime Value

At IR/LLVM level an array value is just a pointer to the first element:

```cpp
FromAstType(TArrayType(T, n)) == Ptr(FromAstType(T))
```

There is no array header in the runtime allocation: no length, no bounds, no
rank, and no stride table are stored next to the elements.  Allocation is done
through the System runtime:

```cpp
void* array_create(size_t sizeInBytes);
void  array_destroy(void* ptr);
void  array_str_destroy(void* ptr, size_t arraySize);
```

`array_create` allocates an 8-byte-aligned zero-filled byte buffer.  Ordinary
arrays are destroyed with `array_destroy`.  Arrays of `лит` are destroyed with
`array_str_destroy`, which releases every stored string pointer before freeing
the buffer.

### 9.2 Bounds and Layout Metadata

Because the runtime pointer has no header, the compiler creates separate
hidden storage for array layout metadata.  During IR lowering,
`LowerArrayLayout` evaluates each declared bound and stores, per dimension:

| Field      | Meaning                                      |
|------------|----------------------------------------------|
| lower bound | declared left bound, e.g. `1` in `[1:n]`    |
| dim size   | `right - left + 1`                           |
| stride     | cumulative element count from this dimension |

The same layout lowering runs for local/global array declarations and for
array parameters inside the callee.  For function parameters, the declared
parameter bounds are expressions in the callee scope, commonly using scalar
size parameters:

```kumir
алг matvec(цел n, вещ таб x[0:n-1], рез вещ таб y[0:n-1])
```

Here `n` is passed separately, and the callee reconstructs the layout metadata
for `x` and `y` from its parameter declarations.

### 9.3 Element Addressing

Arrays are flattened into one row-major allocation: the last dimension is
contiguous.  For an access `a[i0, i1, ..., ik]`, lowering computes a byte
offset equivalent to:

```text
linear =
    (i0 - lb0) * size1 * size2 * ... * sizek
  + (i1 - lb1) * size2 * ... * sizek
  + ...
  + (ik - lbk)

byteOffset = linear * sizeof(element)
address    = basePointer + byteOffset
```

For example, `вещ таб a[0:n-1, 0:m-1]` stores `a[i,j]` at:

```text
base + ((i * m) + j) * 8
```

The implementation emits normal pointer arithmetic in IR:

```text
offset = LowerIndices(symbol, indices, elemByteSize)
addr   = arrayPtr + offset
```

Loads use `lde`; scalar stores use `ste`; struct elements are copied with
`copy` because the element value may be address-backed.

### 9.4 Passing Arrays to Functions

Array arguments are passed by pointer.  The callee receives the same backing
allocation that the caller owns; no element copy is made at the call boundary.
This is why procedures can fill output arrays efficiently:

```kumir
алг fill(цел n, рез цел таб a[0:n-1])
нач
  цел i
  нц для i от 0 до n-1
    a[i] := i
  кц
кон
```

Parameter modes are enforced by semantic type flags:

| Mode       | Effect for arrays                                      |
|------------|---------------------------------------------------------|
| `арг`      | input array; elements may be read according to type flags |
| `рез`      | output array; reading elements is rejected              |
| `арг рез`  | in/out array; elements may be read and written          |

The ABI is still pointer-based in all three cases.  The difference is
semantic checking and generated read/write permissions, not a different
runtime representation.

---

## 10. Struct Representation and ABI

Structs are currently used mostly through named module types, most visibly the
`компл` type from the complex-number module.  At AST level a struct is
`TStructType`, optionally wrapped in `TNamedType` so user-facing diagnostics and
printers can keep the domain name:

```text
<named компл <struct (re f64) (im f64)>>
```

### 10.1 IR Type

`FromAstType(TStructType)` maps to an IR `Struct` type whose fields are the
lowered field types in declaration order.  `TTypeTable` interns struct layouts
by the vector of field type ids:

```text
struct { f64; f64 }          ; complex number payload
```

Struct size is the sum of field sizes in the IR type table.  Field offsets are
computed by summing sizes of all previous fields; there is no separate runtime
header or field-name table.

### 10.2 In-Memory Layout

When lowering needs an addressable struct value, it emits `salloc(size)` and
writes fields into that temporary buffer.  Field access computes:

```text
fieldPtr = objectAddress + fieldByteOffset
```

For scalar fields, reads use `lde` and writes use `ste`.  For nested struct
fields, field access returns the field pointer and assignments use `copy`
because the value may be address-backed rather than an SSA scalar.

Struct values inside arrays are stored inline in the flat array buffer.  An
array access to a struct element returns a pointer to the element so callers can
copy from or write into that storage.

### 10.3 LLVM ABI

The LLVM backend models struct arguments and return values as ordinary LLVM
struct values, not hidden out-parameters:

```llvm
define { double, double } @make_complex(...)
define void @use_complex({ double, double } %z)
```

This is the external ABI seen by LLVM functions.  The IR lowering may still
represent an expression as a pointer to a temporary struct buffer.  Codegen
bridges this mismatch at call and return boundaries:

| Boundary             | Codegen behavior                                      |
|----------------------|-------------------------------------------------------|
| struct argument      | if the IR operand is a pointer, load the LLVM struct value before `call` |
| struct return        | if `ret` receives a pointer but LLVM expects a struct, load before `ret` |
| store into struct local | if destination is a struct alloca and source is a pointer, emit memcpy |
| `copy` into pointer  | if source is already a struct LLVM value, store it; otherwise memcpy |

So the high-level convention is value ABI for LLVM, with address-backed
temporaries as an implementation detail inside lowering.

### 10.4 Function Parameters and Reference Parameters

Plain struct parameters are passed by value in LLVM.  Mutating such a parameter
inside the callee mutates the callee's local copy, not the caller's object.

Reference parameters (`рез` / `арг рез`) are lowered as pointer/reference
types.  For a struct reference assignment, lowering copies the full struct into
the referenced destination:

```text
addr = load reference parameter
copy addr, rhsStruct, sizeof(struct)
```

This is the mechanism used for `рез компл`-style APIs: the ABI carries an
address, and assignment writes the whole struct payload into the caller-owned
storage.

### 10.5 VM Notes

The VM has extra support for materializing struct temporaries and copying
struct payloads.  `StructStore` copies a struct value into a local frame slot,
and call/return handling materializes struct temporaries when a struct value is
represented by an address in IR.  This keeps VM behavior aligned with the LLVM
value ABI at the language level.

---

## 11. String representation

### 11.1 Native `TString` (C++ / WASM heap)

Every mutable string is a heap-allocated `TString` object:

```cpp
struct TString {
    int*    Utf8Indices;  // lazy: symbol index → byte offset in Data
    int64_t Symbols;      // Unicode codepoint count (filled by Utf8Indices build)
    int64_t Rc;           // reference count
    int64_t Length;       // byte length of the UTF-8 payload
    char    Data[0];      // flexible array — the UTF-8 bytes, NUL-terminated
};
```

The value that is **passed around at runtime is `char* TString::Data`** —
a pointer into the *middle* of the struct, just past the header.  To reach
the header from a data pointer: `(TString*)(ptr - offsetof(TString, Data))`.

This means a string value looks like a plain C string to any code that just
reads bytes, while the reference count and metadata live at negative offsets
before it.

**Reference counting** — `str_retain(char*)` increments `Rc`;
`str_release(char*)` decrements it and frees the whole allocation (header +
data) when it reaches zero.

The lowering code tracks string ownership with an internal `EOwnership` flag:

| Ownership   | Meaning                                                    |
|-------------|------------------------------------------------------------|
| `Borrowed`  | Pointer aliases storage owned somewhere else               |
| `Owned`     | Current expression owns one RC reference and must transfer or release it |
| `Unknown`   | Non-string or not relevant for string lifetime handling    |

Important sources of ownership:

| Expression/source                 | Ownership result                                      |
|-----------------------------------|-------------------------------------------------------|
| string literal                    | raw read-only pointer, no `TString` header and no RC  |
| string variable / parameter read  | `Borrowed`                                           |
| string array element read         | `Borrowed`                                           |
| runtime string constructor result | `Owned`, usually RC=1                                |
| string-returning user call        | `Owned`                                              |

**Unicode indexing** — Kumir strings are 1-indexed sequences of Unicode
codepoints.  `Utf8Indices` is built lazily on the first symbol-position
access (`str_symbol_at`, `str_slice`, …).  Once built, it maps
`Utf8Indices[i]` → byte offset of the *i*-th codepoint start inside `Data`.

### 11.2 String literals

Compile-time string literals are emitted as read-only LLVM globals containing
raw UTF-8 bytes.  Their pointers are used directly — **no `TString` header,
no reference count**.  All string runtime functions that accept a `const char*`
argument check for this case: a read-only literal is never passed to
`str_release`.

When a literal must be stored into a mutable `лит` variable/array slot, or
passed to a function that needs a real managed string object, lowering first
calls `str_from_lit(literal)`.  The result is a `TString::Data` pointer with
RC=1.

### 11.3 Function Call Convention for Strings

At the ABI level a `лит` value is passed and returned as `char*` / `ptr i8`.
The ownership convention is the part that matters:

#### String Arguments

Arguments are normally passed as borrowed pointers.  A call does not retain
ordinary string arguments just because they are passed to a function.

If the argument expression is an owned temporary created only for this call,
lowering releases it immediately after the call returns.  Example cases:

- a string literal materialized through `str_from_lit` for a non-external
  user call;
- a literal passed to an external function marked
  `RequireArgsMaterialization`;
- a temporary string expression such as concat/slice/cast result used only as
  an argument.

In IR shape, this is:

```text
tmp = call str_from_lit(literal)   ; RC=1, owned temporary
arg tmp
call f
arg tmp
call str_release                  ; drop caller's temporary reference
```

External runtime functions may opt into argument materialization with
`RequireArgsMaterialization`.  This is used by string functions that inspect
`TString` metadata (`Length`, `Utf8Indices`, etc.) and therefore cannot safely
operate on a raw string literal pointer.

#### String Return Values

A function returning `лит` returns an owned pointer.  The callee transfers one
RC reference to the caller.  Runtime constructors such as `str_concat`,
`str_slice`, `str_from_unicode`, `str_input`, and `str_from_lit` return new
managed strings with RC=1.

The caller then either stores/transfers that owned value or releases it when
it is only a temporary expression.  Block/sequence lowering releases owned
string expression results that are not consumed by an assignment, return, or
other ownership-taking operation.

The LLVM runner has a special `ReturnTypeIsString` marker for top-level JIT
execution: after converting a returned `char*` to text for display, it calls
`str_release` to drop the result owned by the caller.

#### Assignment and RC Updates

Assigning to a string variable or string array element follows a replace
protocol:

1. If RHS is a raw literal, materialize it with `str_from_lit` and treat it as
   owned.
2. If RHS is borrowed, call `str_retain(rhs)` because the destination will now
   own a reference.
3. Load the old destination value and call `str_release(old)`.
4. Store the RHS pointer into the destination.

Owned RHS values do not need an extra retain: the destination takes over the
reference already held by the expression.  Borrowed RHS values must be retained
before the old value is released; this also handles self-assignment safely.

Reference parameters (`рез лит` / `арг рез лит`) use the same replace protocol
through the referenced address: retain the new borrowed value when needed,
release the previous value behind the reference, then store the new pointer.

#### Local and Array Destruction

Local `лит` variables get a pending destructor that calls `str_release` on
scope exit.  The synthetic `$$return` variable is excluded because its value is
owned by the caller after function return.

Arrays of strings use `array_str_destroy(ptr, sizeInBytes)`.  It walks the flat
array of `char*` elements, calls `str_release` for each stored pointer, and
then frees the array buffer.

### 11.4 Browser / WASM dual-handle scheme

The JavaScript `string.js` runtime splits string values into two namespaces
by sign of the `int32` handle:

| Handle value | Meaning                                                    |
|--------------|------------------------------------------------------------|
| ≥ 0          | Pointer into WASM linear memory — a `TString::Data` address or a literal `const char*` |
| < 0          | Index into a JavaScript-managed `STRING_POOL` Map          |

The JS pool stores entries of the form
`{ value: string, refs: number, positions: Array|null }`.  `positions` is the
JS-side equivalent of `Utf8Indices`: a lazy array of UTF-16 index offsets for
Unicode-safe character operations.

Negative handles arise when JS-side string operations (e.g. reading from an
`<input>`) need to produce a string that the WASM code can use as a `лит`
value.  Reference counting for negative handles is managed entirely in JS.

`result.js` interprets handles with the same rules as `string.js` so that
both the interpreter and WASM paths display string return values consistently
in the playground.
