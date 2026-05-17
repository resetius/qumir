# Coroutine annotation

This note describes the front-end decision for coroutine typing, propagation, and await insertion. Coroutine lowering and VM execution are separate steps and are not covered here yet.

## Motivation

Robot and Turtle commands may suspend so that the browser runtime can render movement step by step. In source code this should behave like a normal call, but after semantic analysis the AST must make suspension explicit:

```text
value := function()
```

where `function` is coroutine-typed becomes:

```text
value := await function()
```

Physically, if a function calls a coroutine, the semantics are the same as `value = co_await function()`, and the caller also becomes a coroutine.

## Type model

`Future<T>` is an AST-level type. It marks a function whose execution may suspend before producing `T`.

Runtime functions that can suspend are marked on their function declarations with `MaySuspend`. The coroutine annotation pass treats those functions as returning `Future<RetType>` for front-end typing purposes.

User functions are not annotated as coroutine functions during normal type annotation. First, the ordinary type annotator assigns types without doing coroutine propagation. Then a separate transform pass rewrites coroutine types and call sites.

## Pipeline

Coroutine annotation is part of the transform pipeline:

1. Run name resolution.
2. Run type annotation.
3. Run `PostTypeAnnotationTransform`.
4. Run `CoroutineAnnotationTransform`.
5. If either post-type transform changed the AST or function types, run name resolution, type annotation, and post-type transforms again.

This keeps the type annotator local: it does not discover coroutine functions by itself. It only understands the `Await` node once the transform has inserted it.

## Propagation

`CoroutineAnnotationTransform` builds a call graph for user functions and finds direct coroutine callers:

- a function directly calls a `MaySuspend` runtime function;
- a function directly calls another function whose return type is already `Future<T>`.

The pass then propagates this mark backwards through the call graph. If `a` calls `b` and `b` is a coroutine, then `a` becomes a coroutine too. For every user function marked this way, the pass changes its return type from `T` to `Future<T>`.

The pass is idempotent. If a return type is already `Future<T>`, it is not wrapped again.

## Await insertion

Await is represented by a distinct AST node:

```cpp
TAwaitExpr {
    TExprPtr Operand;
}
```

The transform rewrites:

```text
(call f ...)
```

to:

```text
(await (call f ...))
```

when `f` is `MaySuspend` or has return type `Future<T>`.

During the next type annotation iteration, `AnnotateAwait` checks that the operand has type `Future<T>` and sets the await expression type to `T`. This is what allows assignments such as:

```text
цел value
value := wrap()
```

when `wrap` was rewritten to return `Future<Integer>` by propagation.
