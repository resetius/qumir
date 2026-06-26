# FFI: the template/descriptor approach (reference)

This is an **archived write-up** of the first VM FFI implementation
(`qumir/ir/ffi.{h,cpp}`), later replaced by a hand-written assembly trampoline
(`QumirFfiInvoke`). The code below is no longer built. It is kept as a worked
example of "marshalling to the C ABI at compile time, without libffi", and as a
record of the limitations that motivated the rewrite.

## The problem

The IR interpreter must call native C functions — both the C++ runtime modules
and oz-lang `extern` functions. Given a function's signature it builds a
type-erased `IFunction` whose `operator()(const uint64_t* args, size_t)` takes the
VM's packed argument slots, places them where the C ABI expects (integer
registers, SSE/FP registers, or the stack), calls the symbol, and returns the
result.

The hard part is the platform ABI: which bytes of which argument go into which
register class. A general solution is libffi (a runtime trampoline). This
approach instead asks the **C++ compiler** to do it.

## The core idea

If a thunk is *instantiated with the concrete C++ types* of the arguments and
return value, then a plain call `Symbol(arg0, arg1, ...)` makes the compiler
emit the correct ABI marshalling for free. So the job reduces to: map the
runtime type descriptors (`EKind` / `EStructKind`) to C++ types and instantiate
`TFunction<Ret, Args...>`. The mapping is done with `switch`es that the compiler
expands into every reachable combination of types.

The catch is that "every combination" is a Cartesian product: the number of
template instantiations is `(types-per-position) ^ (argument-count)`. Keeping
that product small is the whole story of this design's evolution.

## Evolution

**v1 — a concrete C++ type for everything.** Each `EKind` mapped to its exact
type (`I8`→`int8_t`, `F64`→`double`, …) and each `EStructKind` to a *fake struct*
with the same register classification (`{int64,int64}`, `{int64,double}`, …).
Structs were passed/returned **by value**, so the compiler produced the right
ABI on any target — portable, but with ~13 scalar types + 7 struct types per
argument position the instantiation count exploded.

**v2 — collapse scalars.** Every integer and pointer kind was folded into a
single `int64_t`, and `double` for `F64` (`DispatchScalar`). At the ABI level a
narrow integer and a pointer travel in the same GP register as an `int64_t`, so
this is sound and cuts per-position scalar types from ~13 to 2. The separate
struct-return wrapper was merged back into `TFunction` via a `ReturnSize` field.

**v3 — decompose single-eightbyte structs.** A struct that fits one eightbyte is
ABI-identical to a single `int64_t` (INTEGER) or `double` (SSE). Instead of a
fake type, the argument's pointer is dereferenced into that scalar, so `Int`/
`Sse` structs stop adding types of their own.

**v4 — decompose two-eightbyte structs (the fragile step).** A small struct
passed by value occupies the same registers as its eightbytes passed as separate
scalars (GP registers fill in order, SSE registers fill in order, regardless of
grouping). So a two-eightbyte struct can be expanded into two `int64`/`double`
"flat" arguments, removing the last fake types — only `int64_t`/`double` remain,
turning `N^k` into `2^k`. A runtime descriptor `TFlatSrc` records, per flat slot,
which VM argument and field offset to read (and whether to dereference).

This step was initially written for x86-64 only, classifying each eightbyte as
INTEGER→`int64` or SSE→`double`. That is correct on SysV but **wrong on
AArch64**: there a struct that is not a homogeneous float aggregate (HFA) is
passed entirely in GP registers, so a `{int64,double}` struct must be expanded as
two `int64`s (the `double` bits ride in a GP register), not as `(int64, double)`.
The mixed-struct tests failed on ARM until this was fixed.

**v5 — `#ifdef` the mixed cases.** `DispatchArgExpand` keeps `(int64, double)` /
`(double, int64)` on x86-64 and switches to `(int64, int64)` on AArch64 for
`IntSse` / `SseInt`. Uniform (`IntInt`, `SseSse`) and single-eightbyte structs
are the same on both targets. Struct *returns* keep the fake types
(`DispatchStructRet`): a returned struct is one value that must be captured as a
single object, and the compiler classifies the fake per the target ABI anyway.

## Calling convention

- A struct argument arrives from the VM as a **pointer** in its slot; the thunk
  dereferences and passes by value (register classes), or forwards the pointer
  (Memory class).
- A struct return is materialised through `args[0]`, a hidden result pointer: the
  value is captured by a fake struct and copied into `args[0]`.

## Pros

- **No assembly.** Pure C++; the compiler is the source of ABI truth for the
  by-value path, so register/stack placement is correct by construction.
- **Fast marshalling.** Argument shuffling is generated and inlined at compile
  time; there is no per-call interpretation of a move list.

## Cons

- **Binary growth.** The Cartesian product of per-position types instantiates a
  `TFunction` for every reachable signature shape.
- **Hard argument cap.** `DispatchArgs` only unrolls 0..4 flat arguments; beyond
  that `BuildFFI` returns `nullptr`. Raising the cap multiplies instantiations.
- **No Memory (>16-byte) struct returns** (`BuildFFI` returns `nullptr`).
- **Per-arch `#ifdef`** for mixed structs — easy to get wrong (it caused a real
  ARM64 bug) and does not generalise to other ABIs.
- **No `F32`** (only `F64`).

## What replaced it

A hand-written assembly trampoline `QumirFfiInvoke` (one block per architecture,
x86-64 and AArch64) — essentially a minimal libffi. `BuildFFI` precomputes a list
of moves (which bytes go to which GP/FP register or stack slot), and at call time
the thunk fills a register frame and the trampoline loads the registers, calls
the symbol, and captures `rax/rdx/xmm0/xmm1`. That removes every limitation above
(arbitrary argument counts, Memory returns, no template combinatorics) at the
cost of maintaining the assembly per target.

## ffi.h (the essentials)

```cpp
enum class EStructKind : uint8_t {
    None, Memory, Int, Sse, IntInt, IntSse, SseInt, SseSse,
};

template<class T>
T LoadArg(uint64_t x) {
    if constexpr (std::is_class_v<T>) {
        // A by-value struct argument arrives as a pointer to its storage.
        return *reinterpret_cast<const T*>(static_cast<uintptr_t>(x));
    } else {
        T ret;
        memcpy(&ret, &x, std::min(sizeof(T), sizeof(uint64_t)));
        return ret;
    }
}

template<typename T>
uint64_t StoreRet(T v, void* output = nullptr, size_t size = 0) {
    if (output != nullptr) { memcpy(output, &v, size); return 0; }
    uint64_t ret = 0;
    memcpy(&ret, &v, std::min(sizeof(T), sizeof(uint64_t)));
    return ret;
}

struct IFunction {
    virtual ~IFunction() = default;
    virtual uint64_t operator() (const uint64_t* args, size_t argCount) = 0;
};

IFunction* BuildFFI(
    void* symbol, EKind retKind, EStructKind retStruct, size_t retSize,
    const std::vector<EKind>& argKinds, const std::vector<EStructKind>& argStructs) noexcept;
```

## ffi.cpp

```cpp
#include "ffi.h"

#include <array>
#include <cassert>
#include <cstring>
#include <new>
#include <utility>

#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__) && !defined(_M_ARM64)
#error "NFFI struct argument decomposition supports only x86-64 and AArch64"
#endif

namespace NQumir {
namespace NIR {
namespace NFFI {

namespace {

static_assert(sizeof(void*) == sizeof(int64_t));

// Captures a 2-eightbyte struct return value (a single object held in two
// registers); only return values still need a concrete struct type.
struct TSIntInt { int64_t a; int64_t b; };
struct TSIntSse { int64_t a; double b; };
struct TSSseInt { double a; int64_t b; };
struct TSSseSse { double a; double b; };

// Where one native eightbyte comes from: a VM argument slot, optionally
// dereferenced (struct passed by pointer) at a field offset.
struct TFlatSrc {
    uint8_t VmIndex;
    uint8_t Offset;
    bool Deref;
};

constexpr size_t MaxFlatArgs = 8;

std::array<TFlatSrc, MaxFlatArgs> BuildFlatSrc(
    const std::vector<EKind>& argKinds,
    const std::vector<EStructKind>& argStructs,
    size_t& flatCount)
{
    std::array<TFlatSrc, MaxFlatArgs> srcs{};
    flatCount = 0;
    auto push = [&](uint8_t vm, uint8_t offset, bool deref) {
        if (flatCount < MaxFlatArgs) {
            srcs[flatCount] = {vm, offset, deref};
        }
        ++flatCount;
    };
    for (size_t i = 0; i < argKinds.size() && i < argStructs.size(); ++i) {
        auto vm = static_cast<uint8_t>(i);
        if (argKinds[i] != EKind::Struct) {
            push(vm, 0, false);
            continue;
        }
        switch (argStructs[i]) {
            case EStructKind::Memory:
                push(vm, 0, false);
                break;
            case EStructKind::Int:
            case EStructKind::Sse:
                push(vm, 0, true);
                break;
            case EStructKind::IntInt:
            case EStructKind::IntSse:
            case EStructKind::SseInt:
            case EStructKind::SseSse:
                push(vm, 0, true);
                push(vm, 8, true);
                break;
            default:
                break;
        }
    }
    return srcs;
}

template<typename ReturnType, typename... Types>
struct TFunction : public IFunction {
    using TFunctionType = ReturnType(*)(Types...);

    TFunction(void* addr, size_t returnSize, const std::array<TFlatSrc, MaxFlatArgs>& srcs)
        : Symbol(reinterpret_cast<TFunctionType>(addr))
        , ReturnSize(returnSize)
        , Srcs(srcs)
    { }

    uint64_t operator() (const uint64_t* args, size_t) override {
        void* ret = nullptr;
        const uint64_t* vmArgs = args;
        if (ReturnSize != 0) {
            ret = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0]));
            vmArgs = args + 1;
        }

        std::array<uint64_t, sizeof...(Types)> flat{};
        for (size_t j = 0; j < sizeof...(Types); ++j) {
            uint64_t base = vmArgs[Srcs[j].VmIndex];
            if (Srcs[j].Deref) {
                memcpy(&flat[j], reinterpret_cast<const char*>(static_cast<uintptr_t>(base)) + Srcs[j].Offset, sizeof(uint64_t));
            } else {
                flat[j] = base;
            }
        }
        return Call(flat.data(), ret, std::index_sequence_for<Types...>{});
    }

    template<size_t... I>
    uint64_t Call(const uint64_t* flat, void* ret, std::index_sequence<I...>) {
        if constexpr(std::is_same_v<ReturnType, void>) {
            Symbol(LoadArg<Types>(flat[I])...);
            return 0;
        } else {
            ReturnType result = Symbol(LoadArg<Types>(flat[I])...);
            return StoreRet(result, ret, ReturnSize);
        }
    }

    TFunctionType Symbol = nullptr;
    size_t ReturnSize = 0;
    std::array<TFlatSrc, MaxFlatArgs> Srcs{};
};

template<typename F>
bool DispatchScalar(EKind k, F&& f) {
    switch (k) {
        case EKind::I1:
        case EKind::I8:
        case EKind::U8:
        case EKind::I16:
        case EKind::U16:
        case EKind::I32:
        case EKind::U32:
        case EKind::I64:
        case EKind::U64:
        case EKind::Ptr:
            f.template operator()<int64_t>();
            return true;
        case EKind::F64:
            f.template operator()<double>();
            return true;
        default:
            return false;
    }
}

// A struct return is captured as one value, so it still needs a concrete type;
// single-eightbyte and pointer returns reuse int64_t / double.
template<typename F>
bool DispatchStructRet(EStructKind sk, F&& f) {
    switch (sk) {
        case EStructKind::Int:
            f.template operator()<int64_t>();
            return true;
        case EStructKind::Sse:
            f.template operator()<double>();
            return true;
        case EStructKind::IntInt:
            f.template operator()<TSIntInt>();
            return true;
        case EStructKind::IntSse:
            f.template operator()<TSIntSse>();
            return true;
        case EStructKind::SseInt:
            f.template operator()<TSSseInt>();
            return true;
        case EStructKind::SseSse:
            f.template operator()<TSSseSse>();
            return true;
        default:
            return false;
    }
}

// A struct argument decomposes into the int64_t / double of each eightbyte; the
// matching value sources are produced by BuildFlatSrc.
template<typename F>
bool DispatchArgExpand(EKind k, EStructKind sk, F&& f) {
    if (k != EKind::Struct) {
        return DispatchScalar(k, std::forward<F>(f));
    }
    switch (sk) {
        case EStructKind::Memory:
        case EStructKind::Int:
            f.template operator()<int64_t>();
            return true;
        case EStructKind::Sse:
            f.template operator()<double>();
            return true;
        case EStructKind::IntInt:
            f.template operator()<int64_t, int64_t>();
            return true;
        case EStructKind::SseSse:
            f.template operator()<double, double>();
            return true;
        // Mixed structs differ by ABI: x86-64 routes each eightbyte to its own
        // register file (GP + SSE), AArch64 keeps a non-HFA struct entirely in
        // GP registers, so the SSE eightbyte travels as raw int64 bits.
        case EStructKind::IntSse:
#if defined(__x86_64__) || defined(_M_X64)
            f.template operator()<int64_t, double>();
#else
            f.template operator()<int64_t, int64_t>();
#endif
            return true;
        case EStructKind::SseInt:
#if defined(__x86_64__) || defined(_M_X64)
            f.template operator()<double, int64_t>();
#else
            f.template operator()<int64_t, int64_t>();
#endif
            return true;
        default:
            return false;
    }
}

template<typename F>
bool DispatchRet(EKind k, F&& f) {
    if (k == EKind::Void) {
        f.template operator()<void>();
        return true;
    }
    return DispatchScalar(k, std::forward<F>(f));
}

template<typename Finish>
IFunction* DispatchArgs(
    const std::vector<EKind>& ak,
    const std::vector<EStructKind>& as,
    Finish&& finish)
{
    if (ak.size() != as.size()) {
        return nullptr;
    }

    IFunction* r = nullptr;
    switch (ak.size()) {
    case 0:
        r = finish.template operator()<>();
        break;
    case 1:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            r = finish.template operator()<A0...>();
        })) {
            return nullptr;
        }
        break;
    case 2:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                r = finish.template operator()<A0..., A1...>();
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    case 3:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                if (!DispatchArgExpand(ak[2], as[2], [&]<class... A2>() {
                    r = finish.template operator()<A0..., A1..., A2...>();
                })) {
                    r = nullptr;
                }
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    case 4:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                if (!DispatchArgExpand(ak[2], as[2], [&]<class... A2>() {
                    if (!DispatchArgExpand(ak[3], as[3], [&]<class... A3>() {
                        r = finish.template operator()<A0..., A1..., A2..., A3...>();
                    })) {
                        r = nullptr;
                    }
                })) {
                    r = nullptr;
                }
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    default:
        return nullptr;
    }
    return r;
}

} // namespace

IFunction* BuildFFI(
    void* symbol,
    EKind retKind,
    EStructKind retStruct,
    size_t,
    const std::vector<EKind>& argKinds,
    const std::vector<EStructKind>& argStructs) noexcept
{
    if (retKind == EKind::Struct && retStruct == EStructKind::Memory) {
        return nullptr;
    }

    size_t flatCount = 0;
    std::array<TFlatSrc, MaxFlatArgs> srcs = BuildFlatSrc(argKinds, argStructs, flatCount);
    if (flatCount > MaxFlatArgs) {
        return nullptr;
    }

    if (retKind == EKind::Struct) {
        IFunction* r = nullptr;
        if (!DispatchStructRet(retStruct, [&]<class Ret>() {
            r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
                return new (std::nothrow) TFunction<Ret, Args...>(symbol, sizeof(Ret), srcs);
            });
        })) {
            return nullptr;
        }
        return r;
    }

    IFunction* r = nullptr;
    if (!DispatchRet(retKind, [&]<class Ret>() {
        r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
            return new (std::nothrow) TFunction<Ret, Args...>(symbol, 0, srcs);
        });
    })) {
        return nullptr;
    }
    return r;
}

} // namespace NFFI
} // namespace NIR
} // namespace NQumir
```
