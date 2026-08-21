#pragma once

#include <qumir/ir/builder.h>
#include <cstdint>

namespace NQumir {
namespace NIR {

enum class EVMOp : uint8_t {
    // TODO: add variants imm op const for simpler decoding
    // integer ALU ops
    INeg, // unary -
    INot, // unary !
    IBitNot, // unary ~
    IAdd, // +
    ISub, // -
    IMulS, // * signed
    IMulU, // * unsigned
    IDivS, // / signed
    IDivU, // / unsigned
    IRemS, // % signed
    IRemU, // % unsigned
    IAnd, // &
    IOr,  // |
    IXor, // xor
    IShl, // <<
    IShrS, // >> signed
    IShrU, // >> unsigned
    ICmpLTS, // < signed
    ICmpLTU, // < unsigned
    ICmpGTS, // > signed
    ICmpGTU, // > unsigned
    ICmpLES, // <= signed
    ICmpLEU, // <= unsigned
    ICmpGES, // >= signed
    ICmpGEU, // >= unsigned
    ICmpEQ, // ==
    ICmpNE, // !=

    // float ALU ops
    FNeg, // unary -
    FAdd, // +
    FSub, // -
    FMul, // *
    FDiv, // /
    FCmpLT, // <
    FCmpGT, // >
    FCmpLE, // <=
    FCmpGE, // >=
    FCmpEQ, // ==
    FCmpNE, // !=

    // load/store
    Load8,
    Load16,
    Load32,
    Load64,
    Store8,
    Store16,
    Store32,
    Store64,

    // tmp assignment
    Mov,
    Cmov, // convert imm to tmp
    I2F, // int to float
    F2I, // float to int
    Bitcast,

    // control flow
    Jmp,
    Cmp,
    ArgTmp, // temporary to argument
    ArgConst, // constant to argument
    Call,
    ECall, // external call
    Await,
    AwaitVoid,
    Ret,
    RetVoid,

    // pointer arithmetic
    Ste, // store by address (*a = i)
    Lde, // load by address (a = *i)
    Lea, // load effective address (a = &i)
    Copy,        // copy(dst_ptr, src, size_bytes_imm); src may be a pointer or packed value
    StructStore, // struct_store(dst_local, src_tmp, size_imm) — memcpy from Tmp into Local frame slot
    SAlloc,      // salloc(dst_tmp, frame_offset_imm, size_imm) — zero frame storage and return its address

    // 128-bit ops address the Regs128 file by the same register index as Regs.
    INeg128,
    INot128,
    IBitNot128,
    IAdd128,
    ISub128,
    IMul128,
    IDivS128,
    IDivU128,
    IRemS128,
    IRemU128,
    IAnd128,
    IOr128,
    IXor128,
    IShl128,
    IShrS128,
    IShrU128,
    ICmpLTS128,
    ICmpLTU128,
    ICmpGTS128,
    ICmpGTU128,
    ICmpLES128,
    ICmpLEU128,
    ICmpGES128,
    ICmpGEU128,
    ICmpEQ128,
    ICmpNE128,

    Load128,
    Store128,
    Mov128,   // 128-bit register copy
    CmovS128, // sign-extend a 64-bit immediate into a 128-bit register
    CmovU128, // zero-extend a 64-bit immediate into a 128-bit register
    SExt128,  // sign-extend a 64-bit register into a 128-bit one
    ZExt128,  // zero-extend a 64-bit register into a 128-bit one
    Trunc128, // low 64 bits of a 128-bit register into a 64-bit one
    I2B128,
    I2F128S,
    I2F128U,
    F2I128,
    Lde128,
    Ste128,
    ArgTmp128,
    Ret128,
};

std::ostream& operator<<(std::ostream& os, EVMOp op);

struct TUntypedImm {
    int64_t Value;
};

struct TVMOperand {
    union {
        TTmp  Tmp;
        TSlot Slot; // TODO: replace Slot/Local with Address
        TLocal Local;
        TUntypedImm  Imm;
    };

    enum class EType : uint8_t {
        Tmp,
        Slot,
        Local,
        Imm,
    } Type;

    TVMOperand() : Type(EType::Tmp), Tmp({-1}) {}
    TVMOperand(const TTmp& t) : Type(EType::Tmp), Tmp(t) {}
    TVMOperand(const TSlot& s) : Type(EType::Slot), Slot(s) {}
    TVMOperand(const TLocal& l) : Type(EType::Local), Local(l) {}
    TVMOperand(const TImm& i) : Type(EType::Imm), Imm(i.Value) {}
    TVMOperand(const TUntypedImm& i) : Type(EType::Imm), Imm(i) {}

    template<typename T>
    void Visit(T&& visitor) const {
        switch (Type) {
        case EType::Tmp: visitor(Tmp); break;
        case EType::Slot: visitor(Slot); break;
        case EType::Local: visitor(Local); break;
        case EType::Imm: visitor(Imm); break;
        }
    }
};

struct TVMInstr {
    std::array<TVMOperand, 3> Operands;
    EVMOp Op;
};

std::ostream& operator<<(std::ostream& os, const TVMInstr& instr);

static_assert(sizeof(TVMInstr) == 56, "TVMInstr must be 56 bytes");

} // namespace NIR
} // namespace NQumir
