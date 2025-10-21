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
    IAdd, // +
    ISub, // -
    IMulS, // * signed
    IMulU, // * unsigned
    IDivS, // / signed
    IDivU, // / unsigned
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

    // control flow
    Jmp,
    Cmp,
    ArgTmp, // temporary to argument
    ArgConst, // constant to argument
    Call,
    ECall, // external call
    Ret,
    RetVoid,
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