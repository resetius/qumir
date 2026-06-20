# AST transformation pipeline

This document describes the compiler stages between parsing and IR lowering.
The pipeline entry point is `NTransform::Pipeline()` in
`qumir/semantics/transform/transform.cpp`.

## Contract

The parser produces a source AST. IR lowering does not consume that tree
directly: semantic passes resolve names, establish types and control-flow
properties, remove source-level sugar, and make ownership explicit. The result
is a finalized AST with these properties:

- every identifier, call, and synthetic variable resolves in the final symbol
  table;
- every expression required by lowering has a type;
- every function has explicit return control flow;
- every managed-value ownership transition and cleanup is represented by a
  lifetime AST node;
- invalid ownership combinations have been rejected before lowering.

The complete order is:

```text
parsed source AST
  │
  ├─ source transform fixpoint
  │    ├─ pre-resolution rewrites and pending core imports
  │    ├─ name resolution
  │    ├─ post-resolution rewrites
  │    ├─ type annotation
  │    ├─ post-annotation rewrites
  │    └─ optional coroutine annotation
  │         repeat while the AST changes
  │
  ├─ definite-assignment analysis
  ├─ return normalization
  ├─ lifetime rewrite
  ├─ final name resolution
  ├─ final type annotation
  └─ lifetime validation
       │
       ▼
finalized AST → IR lowering
```

## Source transform fixpoint

`RunSourceTransformFixpoint()` first performs source name resolution and then
repeats annotation and rewriting. A pass returns whether it changed the tree;
the loop stops only when no pass reports a change. The iteration limit is ten,
and exceeding it is a compiler error rather than silently lowering an unstable
tree.

### Pre-resolution phase

`PreNameResolutionTransform()` performs rewrites that do not require symbol or
type information. Pending core-language imports are loaded before resolution.
The resolver then rebuilds the symbol relationships for the current tree.

### Post-resolution phase

`PostNameResolutionTransform()` can use resolved declarations and scope IDs.
Any nodes introduced here participate in the next resolution/annotation cycle.

### Type annotation and post-annotation phase

`TTypeAnnotator::Annotate()` checks and annotates the current AST bottom-up.
`PostTypeAnnotationTransform()` then applies rewrites that require concrete
types, including typed operators, array/string transformations, and other
source normalization.

When `TPipelineOptions::EnableCoroutineAnalysis` is enabled,
`CoroutineAnnotationTransform()` propagates coroutine function types and
inserts `await` expressions. Because this changes function and call types, name
resolution runs again before the fixpoint decision. Coroutine lowering is
described in [coroutine.md](coroutine.md).

## Definite-assignment analysis

`TDefiniteAssignmentChecker` runs after source rewrites stabilize and before
internal lifetime nodes are introduced. It follows source control flow and
rejects reads that are not assigned on every path. Keeping this stage here
means it reasons about source variables and source exits instead of synthetic
lifetime storage.

## Return normalization

`ReturnNormalizationPass()` removes implicit function fallthrough:

- external functions are unchanged;
- a function already proven to return on every path is unchanged;
- a void function that can fall through receives a trailing `(return)`;
- the final value expression of a non-void function becomes
  `(return <expression>)`.

Return analysis is separate from expression typing. A `return` expression is a
void-typed terminal operation; it never acquires the type of its operand and
therefore cannot be used in assignments or larger value expressions. Nested
blocks and conditionals are handled by control-flow analysis, not by pretending
that terminal expressions produce values.

Return normalization must precede lifetime rewriting because the lifetime pass
needs every function exit to be explicit when it computes cleanup.

## Lifetime rewrite

`LifetimePass()` converts implicit ownership into explicit internal AST nodes.
It introduces synthetic variables, so the symbol table and type annotations
from the source fixpoint are no longer final. The complete model and rewrite
rules are documented in [lifetime.md](lifetime.md).

## Final resolution, annotation, and validation

`RunFinalSemanticPipeline()` resolves and annotates the rewritten tree again.
This is required for synthetic locals, wrapper blocks, and lifetime operands;
reusing source-pass symbol IDs or types here would leave stale semantic data.

`TLifetimeValidator` is the final AST gate. It checks that ownership-producing
nodes have consumers, borrowed values are not destroyed, unique values are not
copied, cleanup nodes contain valid operations, and synthetic names resolve to
the declarations created by the lifetime pass.

Only after validation may `TAstLowerer` build IR. Lowering is intentionally not
a semantic fallback: it translates explicit operations and reports malformed
final AST instead of reconstructing ownership or missing cleanup.

## Idempotence and serialized core AST

A finalized AST may be serialized and re-enter the pipeline. The presence of a
lifetime node marks the tree as already rewritten; `LifetimePass()` does not
apply ownership rewriting a second time. The validator still runs. At most one
root `cleanup-global` node is allowed.

