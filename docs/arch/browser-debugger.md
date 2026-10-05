# Browser debugger and JSPI

The browser debugger runs on the main thread using JavaScript Promise
Integration. The compiler emits ordinary external calls to
`__qumir_debug_point(pointId, address)` and a `qumir.debug` WASM custom section.
No browser protocol or new IR opcode is involved.

## Compiler options

- `-g` collects instruction, function and local metadata for VM/LLVM.
- `--debug-points` emits browser instrumentation and its descriptors at O0.
- These options are independent; shared metadata is collected when either is on.
- `--async-mode=jspi` is the default for WASM. Async external module signatures
  become `T` instead of `Future<T>` through `TJspiModule`; coroutine annotation
  and splitting are absent from this path.
- `--async-mode=coroutine` preserves the existing future ABI and runtime.
  Native and VM execution keep this mode.

Points currently support wasm32, one source file and ordinary user functions.
Explicit core-language Future/await constructs require coroutine mode.
Arrays and structures have opaque value displays; formatters can be added
without changing the checkpoint import.

## Boundaries

`TDebugPointEmitter` owns instrumentation. Instruction points use the existing
instruction wrapper; declarations use a local wrapper. Synthetic calls use
the builder directly. Calls are instrumented before the entire argument packet
and after the call; no point is inserted between `arg` and `call`, or before a
`phi`. Statement points include loop conditions on every visit.

The custom section holds functions, lexical scope parents, AST local types,
source identifier bindings and point descriptors. A variable point passes the
address of its live stack storage, including argument storage. Source bindings
identify locals by id so shadowed names remain distinguishable.

`ExecutionSession` wraps imports with `WebAssembly.Suspending` and exports with
`WebAssembly.promising`. It owns cancellation, animation pacing and event-loop
yields. Most checkpoints return synchronously; pauses and periodic yields
return a Promise. Stop cancels the session and discards the instance.

`Debugger` owns recursive frames, breakpoints and stepping. `ValuePrinter`
reads live WASM memory; `DebuggerUI` binds CodeMirror, controls, stack, locals
and hover. Hover is enabled only while paused, with codepoint positions
converted to CodeMirror UTF16 offsets. Editing is locked during debugging.

Without JSPI the UI disables debugging and requests coroutine compilation for
ordinary runs. JS runtime functions retain their existing future variants;
separate `_jspi` variants return values or Promises.

## Checks

Build `qumirc`, `server`, `test_llvm_debug_info` and `test_cfg` with Ninja.
The LLVM test checks all four flag combinations and argument packet integrity.
`test/test_exec.js` supports `--async-mode=jspi|coroutine` and compares execution
with existing goldens. For the browser test, install the root npm dependencies
and run `node test/test_browser_debugger.mjs` from the repository root. It starts
the C++ server and headless Chrome; `QUMIR_TEST_URL` can select a running server.
A paused screenshot is written to `build/browser_debugger.png`.
