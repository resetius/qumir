# Code Style

## Naming

### Prefixes

| Prefix | Used for | Example |
|--------|----------|---------|
| `N` | Namespaces | `NQumir`, `NIR`, `NAst`, `NSemantics`, `NRuntime`, `NRegistry`, `NTransform` |
| `T` | Classes and structs | `TLocation`, `TTypeTable`, `TFuture`, `TModuleSet`, `TNameResolver` |
| `I` | Abstract interfaces | `IModule`, `IModuleManager` |
| `E` | Enumerations (`enum class`) | `EKind`, `EErrorId`, `EExecBackend` |

### Fields and methods

- **Public struct fields**: PascalCase, no prefix — `Line`, `Byte`, `Kind`, `Aux`, `FieldTypes`
- **Private class fields**: PascalCase with trailing underscore — `ExternalFunctions_`, `ExternalTypes_`
- **Methods**: PascalCase — `RegisterModule`, `SizeInBytes`, `GetKind`, `IsSigned`
- **Local variables**: camelCase — `integerType`, `colorType`, `lowIntTypeId`
- **Lambdas**: camelCase — `colorConst`, `intLiteral`, `binary`, `cast`, `binOp`
- **Function parameters**: camelCase
- **Constants**: a local `constexpr`/`const` follows local-variable casing —
  camelCase (`acquireSpin`, `maxRetries`); a namespace-scope or class
  `static constexpr` constant follows field/method casing — PascalCase
  (`AcquireSpin`, `DefaultCapacity`).

### Special cases

- Nested namespaces on separate lines, not `namespace A::B`
- Closing namespace brace annotated: `} // namespace NIR`

## File structure

- Always `#pragma once` in headers, never include guards
- Include order: project headers first (`<qumir/...>`), then standard library
- One class per header file as a rule; implementation in `.cpp`

## Language features

- C++23: `std::expected`, `std::format`, designated initializers, `auto` lambdas
- Coroutines: `co_await` / `co_return` via custom `TFuture<T>`
- No `using namespace std` anywhere
- Aggregate initialization with designated initializers preferred:
  ```cpp
  ExternalTypes_ = {
      { .Name = "цвет", .Type = colorStorageType },
  };
  ```

## Opcodes

IR opcodes are string literals with `_op` UDL suffix, used in switch cases:

```cpp
case "mov"_op:
case "stre"_op:
case "<<"_op:
```

## Comments

- Comments in English only
- Comment only the **why**, not the what
- `// TODO:` for known gaps; include a short description of what is missing
- No multi-line block comments for explanations; use short single-line comments

## Error handling

- `std::expected<T, TError>` for fallible operations throughout the compiler pipeline
- No exceptions in the hot path; exceptions reserved for truly unexpected internal errors (`throw std::runtime_error(...)`)

## Braces and formatting

Braces are **always** written — no exceptions, including trivial single-statement bodies.

Opening brace on the same line as the statement. Body on a new line, indented. Closing brace on its own line:

```cpp
if (condition) {
    doSomething();
}

if (!value) {
    return;
}

for (int i = 0; i < n; ++i) {
    process(i);
}
```

`else` and `else if` follow the closing brace on the same line:

```cpp
if (hasValue) {
    processValue();
} else if (hasDefault) {
    processDefault();
} else {
    reportError();
}
```

### Inheritance lists

Same rule as initializer lists — `:` and `,` lead each line when the list doesn't fit on one line:

```cpp
// fits on one line — keep it
class TFoo : public TBar {};

// doesn't fit — break it
class TLoweredFunction
    : public TBaseFunction
    , public ILowerable
    , public IDisposable
{
    ...
};
```

### Constructor initializer lists

The initializer list is broken across lines. `:` and each `,` lead the line, indented one level. The opening brace `{}` is on its own line:

```cpp
TTypeTable()
    : Types_(defaultTypes)
    , Structs_()
    , FuncSigs_()
{}

TLocation(int line, int byte)
    : Line(line)
    , Byte(byte)
{}
```

### Ternary operator

A standalone ternary (assignment, return, variable init) is always broken across lines — `?` and `:` on their own lines, indented one level:

```cpp
int size = (typeId < 0)
    ? 8
    : SizeInBytes(typeId);

return (sourceSigned)
    ? irb->CreateSIToFP(val, outputType, "cast")
    : irb->CreateUIToFP(val, outputType, "cast");
```

Inside a function argument list, a short ternary may stay on one line:

```cpp
emitCast(sourceSigned ? ESign::Signed : ESign::Unsigned);
```

### No alignment with spaces

Never align code with extra spaces. This applies everywhere: assignments, initializers, struct fields, comments. The code must look correct in any editor without special configuration.

```cpp
// wrong
int a  = init1;
int bb = init2;

TExternalFunction f1  = { .Name = "RGB" };
TExternalFunction foo = { .Name = "белый" };

// correct
int a = init1;
int bb = init2;

TExternalFunction f1 = { .Name = "RGB" };
TExternalFunction foo = { .Name = "белый" };
```

### Line wrapping in declarations and calls

When a function signature or call doesn't fit on one line, open the parenthesis, put each argument on its own line indented one level, and close on the last argument's line. **Never align with spaces to the opening parenthesis.** When the signature is wrapped, the opening brace `{` moves to its own line:

```cpp
// single line — brace on same line as usual
void LowerInstr(const TInstr& instr, TModule& module) {
    ...
}

// wrapped — brace on new line
void LowerInstr(
    const TInstr& instr,
    TModule& module,
    TContext& ctx)
{
    ...
}

// call site — same rule
auto result = EmitCast(
    sourceValue,
    targetTypeId,
    sourceSigned);
```

The same applies to `if`, `for`, template parameter lists, and any other construct where the head wraps:

```cpp
if (
    actualTy->isIntegerTy() &&
    expectedType->isIntegerTy())
{
    return irb->CreateIntCast(val, expectedType, sourceSigned, "cast");
}
```

### Initializer lists

The "body on new line" rule does not apply to initializer lists. Instead, each element goes on its own line, indented one level, with the closing brace on its own line:

```cpp
std::map<std::string, int> opcodeMap = {
    {"mov", 1},
    {"add", 2},
};

ExternalTypes_ = {
    { .Name = "цвет", .Type = colorStorageType },
};

ExternalFunctions_ = {
    {
        .Name = "RGB",
        .ArgTypes = { integerType, integerType, integerType },
        .ReturnType = colorType,
    },
    {
        .Name = "белый",
        .ReturnType = colorType,
    },
};
```

## Misc

- Prefer `const` references and `std::move` over copies
- Lambdas capture by reference (`[&]`) inside local scope; capture explicitly (`[x, y]`) for stored lambdas
- `static` local variables only when the value is truly immutable and has no per-invocation state; never for objects with mutable counters or accumulated state
