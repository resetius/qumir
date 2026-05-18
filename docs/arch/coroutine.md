# Coroutines

This note describes the current coroutine pipeline: front-end typing,
propagation, AST `await` insertion, IR lowering, LLVM coroutine lowering, and
browser/WebAssembly execution.

Coroutines are currently an LLVM/WASM execution feature. The classic VM runner
does not execute coroutine IR yet.

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

## IR Lowering

`Future<T>` does not become a low-level `Future` object in IR. It is an AST type
only. During AST-to-IR lowering a coroutine function is represented as a normal
IR function with a physical pointer return type:

- source/AST return type: `Future<T>`
- IR function return type: `ptr<void>` coroutine handle
- IR metadata on the function:
  - `IsCoroutine = true`
  - `CoroutineResultTypeId = IR type id of T`

For `Future<void>`, `CoroutineResultTypeId` is `void` and the coroutine has no
promise result payload. For `Future<Int>`, the function still physically returns
the coroutine handle, while the `Int` result is stored in the coroutine promise
and loaded by the awaiting parent after the child is complete.

The AST lowerer recognizes `TAwaitExpr` before lowering the operand. It requires
the operand to be a call, lowers the call arguments in the usual way, and emits
IR opcode `await` instead of `call`.

Regular call:

```text
arg ...
%tmp = call f
```

Awaited call:

```text
arg ...
%tmp = await f
```

The `await` opcode is illegal in regular LLVM lowering. It must appear only in a
function marked `IsCoroutine`, and it is consumed by `LowerCoroutineFunction`.

### IR Example

Source:

```kumir
использовать Робот

алг квадрат
нач
    закрасить
    вправо
    закрасить
кон
```

With `--async-code`, robot actions are `MaySuspend`, so `квадрат` becomes a
coroutine. The printed IR looks like this, shortened:

```text
function квадрат () { ; ptr to void coroutine result void
  block {
    label: label(0)
    await закрасить
    await вправо
    await закрасить
    jmp label(1)
  }
  block {
    label: label(1)
    ret
  }
}
```

The comment means:

- physical function return: `ptr to void`
- coroutine promise/result type: `void`

If the function returned `Future<Int>`, the physical return would still be
`ptr<void>`, but the comment/result metadata would identify the coroutine result
as `Int`.

## LLVM Lowering

`TLLVMCodeGen::LowerFunction` dispatches coroutine functions to
`LowerCoroutineFunction`. Coroutine functions are emitted with
`presplitcoroutine` and use LLVM coroutine intrinsics directly:

- `llvm.coro.id`
- `llvm.coro.size.i32`
- `llvm.coro.begin`
- `llvm.coro.suspend`
- `llvm.coro.end`
- `llvm.coro.free`
- `llvm.coro.promise`
- `llvm.coro.done`
- `llvm.coro.resume`
- `llvm.coro.destroy`

LLVM supports several coroutine lowering conventions / ABIs. Qumir uses LLVM's
**standard switched-resume lowering**: it is selected by emitting
`llvm.coro.id`, the same family of intrinsics used for the C++ coroutine-style
handle/resume/destroy model. In this convention the coroutine invocation is
represented by a coroutine object / frame handle, and LLVM creates shared
resume and destroy functions that switch on the stored suspend index.

The other LLVM coroutine lowerings are not used here:

- returned-continuation lowering, selected by `llvm.coro.id.retcon` or
  `llvm.coro.id.retcon.once`;
- async lowering, selected by `llvm.coro.id.async`.

Frame memory is allocated through the existing runtime allocation API:

```llvm
%size = call i32 @llvm.coro.size.i32()
%alloc.size = zext i32 %size to i64
%alloc = call ptr @array_create(i64 %alloc.size)
%handle = call ptr @llvm.coro.begin(token %id, ptr %alloc)
```

Cleanup uses `llvm.coro.free` and `array_destroy`:

```llvm
%mem = call ptr @llvm.coro.free(token %id, ptr %handle)
call void @array_destroy(ptr %mem)
```

This keeps coroutine frames on the same allocation path as arrays, so the JS
runtime does not need a separate `malloc/free` import for coroutine frames.

### Awaiting External Suspend Actions

External executor functions marked `MaySuspend` are still imported as ordinary
void host calls at the low level. The suspension is inserted around the call by
the compiler.

For an awaited robot action, lowering emits:

```llvm
call void @robot_paint()
%s = call i8 @llvm.coro.suspend(token none, i1 false)
switch i8 %s, label %suspend [
  i8 0, label %after.await.N
  i8 1, label %cleanup
]
```

So the command is executed first, then the coroutine yields back to JavaScript.
This is why the browser can render the already-applied robot/turtle/painter
state after each suspension.

### Awaiting Child Coroutines

When `await` targets another user coroutine, lowering calls the child coroutine
to get its handle, then repeatedly resumes it until `llvm.coro.done(child)` is
true. After each child resume, the parent suspends too:

```llvm
%child = call ptr @child(...)
br label %await.child.cond

await.child.cond:
  %done = call i1 @llvm.coro.done(ptr %child)
  br i1 %done, label %after.await, label %await.child.resume

await.child.resume:
  call void @llvm.coro.resume(ptr %child)
  %s = call i8 @llvm.coro.suspend(token none, i1 false)
  switch i8 %s, label %suspend [
    i8 0, label %await.child.cond
    i8 1, label %cleanup
  ]
```

If the child has a non-void result, the parent reads it from the child promise:

```llvm
%promise = call ptr @llvm.coro.promise(ptr %child, i32 0, i1 false)
%result = load <T>, ptr %promise
```

Then the child is destroyed:

```llvm
call void @llvm.coro.destroy(ptr %child)
```

### Returning Values

Coroutine return values are not returned through the physical LLVM function
return. The physical return is always the coroutine handle. On `ret value`, the
lowerer stores `value` into the promise slot and branches to final suspend.

Final suspend is emitted with `isFinal = true`:

```llvm
%sf = call i8 @llvm.coro.suspend(token none, i1 true)
switch i8 %sf, label %suspend [
  i8 0, label %suspend
  i8 1, label %cleanup
]
```

After final suspend, `llvm.coro.done(handle)` becomes true.

### Coroutine Passes

LLVM coroutine intrinsics must be split before object/WASM code emission. In the
compiler driver path, coroutine modules run this LLVM pass pipeline:

```text
coro-early,coro-split,coro-elide,coro-cleanup
```

`coro-split` requires optimization infrastructure. The compiler driver
automatically bumps the effective optimization level from `O0` to `O1` when the
IR module contains coroutine functions.

## WebAssembly Output

LLVM's coroutine passes lower the coroutine into a frame and generated resume /
destroy functions. In the optimized output the public Qumir coroutine function
returns a pointer to the frame:

```llvm
define ptr @main() {
entry:
  %frame = call ptr @array_create(i64 <frame-size>)
  store ptr @main.resume, ptr %frame
  store ptr @main.destroy, ptr <destroy-slot>
  call void @robot_paint()
  store <state> ..., ptr <state-slot>
  ret ptr %frame
}
```

The exact frame layout is LLVM-owned. Conceptually it contains:

- resume function pointer
- destroy function pointer
- suspend-state index
- spilled locals / temporaries
- optional promise storage

On `wasm32`, function pointers are represented through the WebAssembly function
table. The compiler also emits stable helper exports so JavaScript does not need
to know the frame layout:

```text
__qumir_is_coroutine  ; exported sentinel global
__qumir_coro_done(ptr) -> i32
__qumir_coro_resume(ptr) -> void
__qumir_coro_destroy(ptr) -> void
```

The sentinel is emitted if the module contains at least one coroutine function.
The JS runtime uses it to decide whether the entry point must be driven as a
coroutine.

## Browser Runtime

The browser compiles with `--async-code` and instantiates the WASM module as
usual. After choosing the exported entry function, `runWasm` calls
`shouldRunWasmCoroutine`.

Coroutine execution is:

```javascript
const handle = entryFn(...args);

while (!__qumir_coro_done(handle) && !stopRequested) {
  __qumir_coro_resume(handle);
  renderStep();
  await sleep(animationDelay);
}

__qumir_coro_destroy(handle);
```

The actual implementation uses exported helpers:

- `__qumir_coro_done(handle)` checks final suspend;
- `__qumir_coro_resume(handle)` resumes execution until the next suspend;
- `__qumir_coro_destroy(handle)` releases the coroutine frame.

For Robot, the runtime also enables coroutine mode in `robot.js`. In coroutine
mode robot history replay is disabled because the browser is rendering the real
execution state after each suspend, not replaying a recorded history.

For animated executors:

- Robot and Turtle return their configured animation delay;
- Painter currently uses frame boundaries such as `новый лист` as suspend
  points and returns a frame delay;
- if delay is zero, the runner batches resumes and yields to the browser
  periodically with `setTimeout(..., 0)` so long runs do not freeze the tab.

Stopping execution sets the stop flag. The loop exits at the next suspension,
destroys the coroutine handle, and renders the final visible state.

## Current Limitations

- VM execution for coroutine IR is not implemented.
- Low-level `await` is currently LLVM-codegen-only.
- Awaited external suspend actions are expected to be void-returning host
  calls. Non-void coroutine values are supported for user coroutine calls via
  the promise path.
- JS intentionally uses compiler-exported helpers instead of reading coroutine
  frame fields directly. This keeps the browser side independent from LLVM's
  exact frame layout.
