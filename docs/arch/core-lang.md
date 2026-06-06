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

Blocks and sequencing:

```core
(block stmt1 stmt2 ... stmtN)
(seq stmt1 stmt2 ... stmtN)
```

`block` introduces a nested lexical scope. `seq` evaluates items in order
without introducing a nested scope.

Pragmas may appear at the start of the outermost `block` (depth 1), before
any real statements. They are virtual nodes — not represented in the AST —
and are collected into `TParser::Pragmas` for the caller to apply after
`Parse()` returns.

```core
(block
  (pragma language overloads)
  stmt1
  stmt2 ...)
```

A pragma has the form `(pragma group value1 value2 ...)`. Placing a pragma
after a non-pragma statement, or inside a nested block, is an error.

Currently defined pragmas:

| Group | Value | Effect |
|-------|-------|--------|
| `language` | `overloads` | Enable user-defined function overloading (off by default) |

Conditionals:

```core
(cond condition then)
(cond condition then else)
(if condition then)
(if condition then else)
```

`cond` is the statement form (`TIfStmt`); `if` is the expression form
(`TIfExpr`). The else branch is optional.

Local bindings:

```core
(let
  ((name1 value1)
   (name2 value2))
  body)
```

Bindings are visible in `body`; bindings are not visible to each other.

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
(vars var1 var2 ... varN)
```

Array bounds are declaration metadata on `TVarStmt`, not part of the
`TArrayType` identity.

Functions:

```core
(fun name return_type (param1 ... paramN) (attr1 ... attrM) body)
```

Each parameter is a `(var ...)` form. The body must be a `block`. Supported
function attributes are:

```core
(expect_after expr)
(expect_before expr)
```

The parser currently stores `expect_after` on `TFunDecl::LastAssert`.
`expect_before` is parsed for forward compatibility.

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
  (fun pick i64 ((var x i64)) ()
    (block
      (var $$return i64)
      (= $$return (+ x (: 10 i64)))))

  (fun pick f64 ((var x f64)) ()
    (block
      (var $$return f64)
      (= $$return (+ x (: 0.5 f64)))))

  (fun pick string ((var x string)) ()
    (block
      (var $$return string)
      (= $$return x)))

  (fun <main> void () ()
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

Casts, indexing, and slicing:

```core
(cast operand type)
(index index collection)
(index [index1 ... indexN] collection)
(slice [start] collection)
(slice [start end] collection)
```

Single-index access creates `TIndexExpr`; vector index access creates
`TMultiIndexExpr`.

When a function parameter has type `<ref T>`, the argument must be an lvalue.
Identifiers, field accesses, and array element accesses can be passed by
reference. Indexed array arguments use the same syntax as normal reads:

```core
(fun bump void ((var x <ref i64>)) ()
  (block (= x (+ x (: 1 i64)))))

(var a <array i64 1> [0 2])
(call bump (index 1 a))
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
```

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
```

Unknown bare type identifiers are parsed as short named types:

```core
color
```

which is equivalent to a `TNamedType` without an underlying type attached.

## Type Attributes

Types carry `Readable` and `Mutable` flags. The default type is readable and
not mutable. Core syntax can spell attributes by wrapping a scalar or composite
type in angle brackets:

```core
<i64 (mutable)>
<ref string (mutable)>
<named buffer <array i64 1> (mutable)>
```

The parser also accepts `(readable)` in the attribute list, but the printer
omits default attributes. In canonical output, `readable` is not printed; only
the current non-default case, `mutable` without `readable`, is emitted.

## Printer Conventions

The core printer is the canonical form used by tests and AST goldens.

- Identifiers containing spaces are printed as bar identifiers.
- String and character literals are escaped with `\n`, `\t`, `\\`, `\"`, and
  `\'`.
- `nil` is printed for null expressions and null types.
- Function attributes currently print only `expect_after`.
- Type annotations are printed depending on `TPrintOptions::TypeMode`.
- Scalar types are printed bare unless they need non-default attributes.
- Named types may be printed short when listed in `ShortNamedTypes`; otherwise
  they print as `<named name underlying_type>`.

## Example

```core
(fun sum i64
  ((var n i64)
   (var a <array i64 1> [0 (- n 1)]))
  ()
  (block
    (vars
      (var i i64)
      (var s i64))
    (= s 0)
    (for i 0 (- n 1) 1
      (block
        (= s (+ s (index i a)))))
    s))
```
