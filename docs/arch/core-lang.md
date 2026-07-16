# Core Language

Core lang is the internal surface syntax for the Qumir AST. It is used by
tests, golden files, debugging output, and the playground "core syntax" mode.
The syntax is intentionally close to S-expressions: most AST nodes are written
as parenthesized forms, while composite types use angle brackets.

The motivation is practical: the user-facing Kumir syntax is small and
conservative, which makes it awkward to introduce and debug new language
constructs directly in `.kum` first. Core lang lets us start from the AST
shape: add or inspect a node, write it explicitly, run it through semantic
passes and lowering, and only later decide whether and how it should be exposed
in the Kumir frontend syntax.

The implementation lives in `qumir/parser/core/lexer.cpp`,
`qumir/parser/core/parser.cpp`, and `qumir/parser/core/printer.cpp`.

## Lexical Syntax

Whitespace separates tokens and is otherwise ignored.

Delimiters and structural operators:

| Token | Meaning |
|-------|---------|
| `(` `)` | expression forms and nested lists |
| `<` `>` | composite type forms |
| `[` `]` | array bounds, index vectors, and slices |
| `:` | type annotation form head |

Core source is UTF-8 text. Simple identifiers start with a letter, `_`, `$`, or
a non-ASCII byte. They may continue with letters, digits, `_`, `$`, `:`, or
non-ASCII bytes:

```core
foo
my_var
$tmp
module:name
имя
```

Symbolic identifiers are made from operator characters:

```core
+ - * / = ! & ^
<= >= << >> || |
```

`<`, `>`, `[`, `]`, `(`, `)`, and `:` are delimiter tokens by default. The
lexer has one special case for operator heads: after an opening `(`, two-byte
`<<`, `<=`, `>>`, and `>=` are read as identifiers so forms like `(>> x 1)` are
valid operator calls.

Bar identifiers allow spaces:

```core
|foo bar|
```

String and character literals support the common escapes `\n`, `\t`, `\\`,
`\"`, and `\'`:

```core
"hello\nworld"
'\n'
```

Numeric and boolean literals:

```core
123
-42
1.25
.5
3e-2
#t
#f
```

`nil` is a reserved identifier used as the empty expression/type value in
places where the AST allows a missing child.

## Expressions

An atom expression is one of:

| Syntax | AST node |
|--------|----------|
| `name` | `TIdentExpr`, except reserved `break`/`continue` |
| integer, float, boolean | `TNumberExpr` |
| character | `TNumberExpr` with `char` type |
| string | `TStringLiteralExpr` |
| `break` | `TBreakStmt` |
| `continue` | `TContinueStmt` |
| `nil` | null `TExprPtr` |

Parenthesized forms use the first item as a form name. Unknown two-argument
forms are parsed as binary operator expressions, and unknown one-argument forms
are parsed as unary operator expressions.

## Core Forms

Assignment:

```core
(= name value)
(= name [index1 ... indexN] value)
```

The first form creates `TAssignExpr`; the indexed form creates
`TArrayAssignExpr`.

Unary and binary operators:

```core
(op operand)
(op left right)
```

Blocks:

```core
(block stmt1 stmt2 ... stmtN)
```

`block` introduces a nested lexical scope. Its value is the value of its last
statement (or void if the block is empty or the last statement does not
produce a value) — this is how function bodies return a value implicitly, see
"Functions" below.

The outermost `block` (depth 1) enforces a strict statement order:

```
(pragma ...)* → (use ...)* → (type ...)* → other statements
```

**Pragmas** are virtual nodes — not represented in the AST — collected into
`TParser::Pragmas` for the caller to apply after `Parse()` returns.

```core
(block
  (pragma language overloads)
  stmt1
  stmt2 ...)
```

A pragma has the form `(pragma group value1 value2 ...)`. Pragmas must appear
before `use` and `type` declarations.

**`use`** imports a module, making its functions and types available in scope.
`use` must appear after pragmas and before type declarations.

```core
(block
  (use "Цвета")
  ...)
```

Core-lang does not define an I/O runtime and does not import anything
implicitly: a pure core program imports only what it spells with `use`. The
runtime modules (such as `System`) are provided by the host. The `qumiri` and
`qumirc` core modes import `System` as a host prelude so that forms like
`output` resolve; module aliases like the legacy Kumir `Файлы` exist only in
the Kumir frontend.

**`type`** declares a named type alias. It must appear after `use` statements.
Declaring a type whose name is already imported from a module is an error.

Currently defined pragmas:

| Group | Value | Effect |
|-------|-------|--------|
| `language` | `overloads` | Enable user-defined function overloading (off by default) |

Conditionals:

```core
(if condition then)
(if condition then else)
```

`if` creates `TIfExpr`, used both as a statement and as a value-producing
expression — its value is the value of the taken branch. The else branch is
optional. The older `(cond ...)` spelling is rejected with an error pointing
at `(if ...)`.

Loops:

```core
(while condition body)
(repeat body condition)
(for name from to step body)
(times count body)
break
continue
```

`for` always has a step expression in core syntax. Use `nil` if a missing step
must be represented.

Variables and variable blocks:

```core
(var name type)
(var name type [from1 to1] ... [fromN toN])
(var name = value)
(vars var1 var2 ... varN)
```

Array bounds are declaration metadata on `TVarStmt`, not part of the
`TArrayType` identity. The `(var name = value)` form omits the type entirely —
it is inferred from `value`'s type during type annotation.

Functions:

```core
(fun name (param1 ... paramN) body)
(fun name (param1 ... paramN) -> return_type body)
(fun name (param1 ... paramN) -> return_type (attrs (expect_after expr) (expect_before expr) ...) body)
```

Each parameter is a `(var ...)` form; `(param1 ... paramN)` is required even
when empty (`()`). The body must be a `block`. The return type defaults to
`void` when `-> return_type` is omitted.

`(attrs ...)` is an optional attribute list, placed after the return type (if
any) and before the body. Recognized attributes:

```core
(expect_after expr)
(expect_before expr)
(operator "OP")
print
```

The parser stores `expect_after` on `TFunDecl::LastAssert`. `expect_before` is
parsed for forward compatibility. Bare-identifier attributes other than `print`
(e.g. `inline`) are accepted and silently ignored for forward compatibility.

### Operator, cast and print attributes

A function may implement an operator, a cast or a type's printer instead of
being called by name. This lets a source module (or program) provide the same
overloaded-by-type behavior that built-in runtime modules do — without enabling
`language overloads`.

```core
; binary operator: a + b for tuple dispatches here
(fun add ((var a tuple) (var b tuple)) -> tuple (attrs (operator "+"))
  (block ...))

; unary operator (e.g. "neg" for unary minus)
(fun negate ((var a tuple)) -> tuple (attrs (operator "neg"))
  (block ...))

; cast: implicit coercion from i64 to tuple (the magic name is "cast")
(fun from_int ((var n i64)) -> tuple (attrs (operator "cast"))
  (block ...))

; printer: outputting a tuple value via (output ...) dispatches here
(fun show ((var p tuple)) (attrs print)
  (block (output "(" (field p a) ";" (field p b) ")")))
```

- `(operator "OP")` — the function implements operator `OP`. With two
  parameters it is a binary operator (`"+"`, `"=="`, ...); with one parameter, a
  unary operator. The reserved value `"cast"` (one parameter) registers an
  implicit cast from the parameter type to the return type.
- `print` (a bare attribute) — the function is its single parameter type's
  printer; `(output value ...)` of that type dispatches to it. Equivalent to a
  unary operator under the internal key `"print"`.
- `used` (a bare attribute) - keep this function even when no explicit reference makes
  it reachable.

These functions are resolved **by operand/result types**, in the resolver's
dispatch tables (`ImportedBinaryOps` / `ImportedUnaryOps` / `ImportedCasts`) —
not by their declared name. The declared name stays an ordinary symbol: e.g.
naming the printer `print` does not clash with the operator key `"print"`,
since the latter lives in a separate table and is invoked through a synthetic
name. Explicit casts `(cast x T)` are *not* routed through `(operator "cast")`;
only implicit coercions (assignment, arguments) are.

`<main>` is the program's entry point: `(fun <main> () body)`. It is always
void.

## Return

A non-void function returns its result either via an explicit `(return expr)`
or implicitly: if execution falls off the end of the body without hitting a
`return`, the value of the body block's last statement becomes the return
value. Falling off the end without producing a value is a compile error
("Тело функции должно заканчиваться оператором `return` или выражением,
возвращающим значение.").

```core
(return)
(return expr)
```

`(return)` is only valid in `void` functions, `(return expr)` only in
non-void ones. Both work for every return type, including structs and named
types.

```core
(fun square ((var x i64)) -> i64
  (block
    (* x x)))            ; implicit return: value of the last statement

(fun cube ((var x i64)) -> i64
  (block
    (var sq i64)
    (= sq (* x x))
    (return (* sq x))))  ; explicit return
```

`break`/`continue`/`return` release any in-scope locals with pending
destructors (string/array) before transferring control: `break`/`continue`
release locals declared since the start of the nearest enclosing loop body;
`return` releases locals declared since the start of the function body.

Type declarations:

```core
(type name underlying_type)
```

Declares a named type alias. `name` becomes available as a bare type identifier
in the surrounding block scope. Named types are transparent to the IR — the IR
type is the same as the underlying type. In type position the long form is
`<named name>` or `<named name underlying_type>`:

```core
(block
  (type имя <struct (val i64)>)
  (fun make_name ((var v i64)) -> <named имя>
    (block
      (return (: (struct ((val v))) <named имя>))))
  (fun <main> ()
    (block
      (var n <named имя>)
      (= n (call make_name 42)))))
```

Calls and I/O:

```core
(call callee arg1 ... argN)
(input arg1 ... argN)
(output arg1 ... argN)
(output (fmt expr width) (fmt expr width precision))
```

`fmt` is the output argument wrapper for width and optional precision.

Overloaded functions:

```core
(block
  (fun pick ((var x i64)) -> i64
    (block
      (+ x (: 10 i64))))

  (fun pick ((var x f64)) -> f64
    (block
      (+ x (: 0.5 f64))))

  (fun pick ((var x string)) -> string
    (block
      x))

  (fun <main> ()
    (block
      (output (call pick (: 7 i32)) "\n")
      (output (call pick (: 2.5 f64)) "\n")
      (output (call pick "text") "\n"))))
```

Multiple `fun` forms with the same source name form an overload set when their
parameter type lists differ. Return type alone is not enough to distinguish
overloads.

Overload resolution runs after argument type annotation. Exact matches are
preferred over implicit conversions. Integer widening is preferred over
integer-to-float conversion, so `(call pick (: 7 i32))` selects the `i64`
overload in the example above. If two viable overloads have the same best cost,
the call is rejected as ambiguous.

Generic functions:

```core
(fun identity ((var x <named K (template)>)) -> <named K (template)>
  (block
    (return x)))
```

A `TNamedType` marked `template` is a type placeholder, not a real declared
type — it makes the enclosing `fun` generic. At each call site the concrete
types bound to placeholders are inferred from argument types by unification,
and a monomorphized clone of the function is generated for that binding,
registered under a synthetic name `__generic_<name>$<TypeKey1>$<TypeKey2>...`
(`__generic_identity$Int`, `__generic_identity$String`, ...) and substituted
for the call. Repeat calls with the same concrete types reuse the cached
clone — exactly one definition exists per (function, type-binding) pair,
regardless of how many call sites use it.

Placeholders may appear nested inside composite types (`<array <named K
(template)> 1>`, `<ref <named K (template)>>`, ...) and a function may have
several independent placeholder names; each gets its own binding inferred
independently. A call is a typing error when a placeholder's binding can't be
inferred from the arguments, or when the same placeholder name would have to
bind to two structurally different concrete types.

When an overload set mixes concrete and generic `fun` forms for the same name,
overload resolution prefers a concrete match over instantiating the generic
fallback — see `(fun f ((var x i64)) -> i64 ...)` vs. `(fun f ((var x <named K
(template)>)) -> i64 ...)`.

Casts, indexing, and slicing:

```core
(cast operand type)
(bitcast operand type)
(index collection index)
(index collection [index1 ... indexN])
(slice collection [start])
(slice collection [start end])
```

`bitcast` preserves the operand's representation and requires integer, float,
symbol, or pointer source and target types with the same byte size. Numeric
literal bitcasts are folded during the transformation pipeline.

Single-index access creates `TIndexExpr`; vector index access creates
`TMultiIndexExpr`. `(slice collection [start])` with a single bound is a
single-element slice, equivalent to `(slice collection [start start])`.

When a function parameter has type `<ref T>`, the argument must be an lvalue.
Identifiers, field accesses, and array element accesses can be passed by
reference. Indexed array arguments use the same syntax as normal reads:

```core
(fun bump ((var x <ref i64>))
  (block (= x (+ x (: 1 i64)))))

(var a <array i64 1> [0 2])
(call bump (index a 1))
```

During IR lowering the indexed expression is converted to the element address,
not loaded as a value.

Modules and assertions:

```core
(use module_name)
(use "module name")
(assert expr)
```

`use` accepts either an identifier-like name or a string token.

### Source modules (`.oz`)

A `use` name that does not name a registered runtime module but resolves to a
`<name>.oz` file is loaded as a **source module**: it is parsed, validated and
inlined into the compilation unit, so its top-level functions, types and globals
become part of the program (one combined AST, one set of constructors). Module
name = file stem.

Resolution order for `<name>.oz`: explicitly registered modules (`--module`),
then the directory of the main source file, then `--module-path` directories in
registration order; the first match wins. (Programmatically the runners take
`ModuleSearchPaths` / `ModuleFiles`.)

A source module's `use` declarations are its transitive dependencies; the loader
detects cycles and reports the chain. All top-level declarations are exported
into the shared namespace (no explicit `export`/`private` yet). A source module
may **not** declare `<main>` or executable top-level statements; it may declare
functions, types and globals. Globals are initialized in dependency order
(dependencies first, the main program last) and destroyed in reverse.

Operator/cast/print functions (see the attributes section) carried by a source
module are registered into the operator/cast dispatch tables on import, so
`a + b`, implicit casts and `(output value)` work for the module's types.

The same mechanism is available to the Kumir frontend: `использовать <name>`
loads `<name>.oz`; its exported type names are registered so the Kumir
lexer/parser recognizes them at parse time.

Struct operations:

```core
(field field_name object)
(struct ((name1 value1) (name2 value2) ... (nameN valueN)))
(field_assign object field_name value)
```

The `struct` expression creates a `TStructConstructExpr`. Field types are
usually supplied later through type annotation.

Type annotations:

```core
(: expr type)
```

This sets `TExpr::Type` on any AST node. The printer emits annotations in
`All` mode, and in required places such as named types and struct constructors
in the default mode.

## Types

Primitive types:

| Core | AST type |
|------|----------|
| `i64` | `TIntegerType` |
| `f64` | `TFloatType` |
| `bool` | `TBoolType` |
| `string` | `TStringType` |
| `char` | `TSymbolType` |
| `file` | `TFileType` |
| `void` | `TVoidType` |

Composite types:

```core
<fun return_type (param_type1 ... param_typeN)>
<array element_type arity>
<ptr pointee_type>
<ref referenced_type>
<named name underlying_type>
<struct (field_name1 field_type1) ... (field_nameN field_typeN)>
<future result_type>
```

`<future result_type>` is `TFutureType`, the type of an awaitable produced by
a coroutine; see `arch/coroutine.md`.

Examples:

```core
i64
<array i64 1>
<array <ref f64> 2>
<ptr <struct (x f64) (y f64)>>
<ref string>
<named color i64>
<struct (x i64) (values <array f64 1>)>
<fun bool (i64 f64)>
<future i64>
```

Unknown bare type identifiers are parsed as short named types:

```core
color
```

which is equivalent to a `TNamedType` without an underlying type attached.

## Type Attributes

Types carry `Readable` and `Mutable` access flags. By default a type is both
readable and mutable. Core syntax can spell non-default access modes by wrapping
a scalar or composite type in angle brackets:

```core
<i64 (readonly)>
<ref string (writeonly)>
<named buffer <array i64 1> (readonly)>
```

The access modifiers `mutable`, `readonly`, and `writeonly` are mutually
exclusive. `mutable` explicitly selects the default readable and mutable state,
which the printer omits in canonical output. The fully immutable, unreadable
state cannot be spelled — there is no attribute for it.

`template` is an additional attribute accepted only on named types
(`<named K (template)>`). It does not change access flags and marks the type as
a generic placeholder rather than a real declared type — see
"Generic functions" above.

## Printer Conventions

The core printer is the canonical form used by tests and AST goldens.

- Identifiers containing spaces are printed as bar identifiers.
- String and character literals are escaped with `\n`, `\t`, `\\`, `\"`, and
  `\'`.
- `nil` is printed for null expressions and null types.
- Function attributes currently print only `expect_after`; `operator`/`print`
  attributes are consumed at resolution and are not yet re-emitted by the
  printer.
- Type annotations are printed depending on `TPrintOptions::TypeMode`.
- Scalar types are printed bare unless they need non-default attributes.
- Named types may be printed short when listed in `ShortNamedTypes`; otherwise
  they print as `<named name underlying_type>`.

## Example

```core
(fun sum
  ((var n i64)
   (var a <array i64 1> [0 (- n 1)]))
  -> i64
  (block
    (vars
      (var i i64)
      (var s i64))
    (= s 0)
    (for i 0 (- n 1) 1
      (block
        (= s (+ s (index a i)))))
    s))
```
