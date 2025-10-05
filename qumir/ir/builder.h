#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <optional>
#include <variant>
#include <ostream>
#include <unordered_map>
#include <memory>
#include <cstddef>
#include <set>

#include "type.h"

namespace NQumir {
namespace NIR {

struct TOp {
    constexpr TOp(uint32_t code): Code(code) {}
    // +,-,*,/
    constexpr TOp(char code): Code(code) {}
    // <=, >=, ==, !=
    constexpr TOp(const char* code) {
        Code = 0;
        while (*code) {
            Code = (Code << 8) | *code++;
        }
    }

    constexpr operator uint32_t() const {
        return Code;
    }

    std::string ToString() const {
        std::string s;
        int64_t v = Code;
        while (v != 0) {
            s = static_cast<char>(v & 0xFF) + s;
            v >>= 8;
        }
        return s;
    }

    uint64_t Code;
};

// Temporary variable
struct TTmp {
    int32_t Idx;
};

// Immediate value (e.g. constant)
struct TSlot {
    int32_t Idx;
};

struct TLabel {
    int32_t Idx;
};

inline bool operator<(const TLabel& a, const TLabel& b) {
    return a.Idx < b.Idx;
}

// Immediate value (e.g. constant), only int64_t for now
struct TImm {
    int64_t Value;
    bool IsFloat = false;
};

struct TOperand {
    union {
        TTmp  Tmp;
        TSlot Slot;
        TImm  Imm;
        TLabel Label;
    };

    enum class EType : uint8_t {
        Tmp,
        Slot,
        Imm,
        Label
    } Type;

    TOperand() : Type(EType::Tmp), Tmp({-1}) {}
    TOperand(const TTmp& t) : Type(EType::Tmp), Tmp(t) {}
    TOperand(const TSlot& s) : Type(EType::Slot), Slot(s) {}
    TOperand(const TImm& i) : Type(EType::Imm), Imm(i) {}
    TOperand(const TLabel& l) : Type(EType::Label), Label(l) {}

    template<typename T>
    void Visit(T&& visitor) const {
        switch (Type) {
        case EType::Tmp:   visitor(Tmp);   break;
        case EType::Slot:  visitor(Slot);  break;
        case EType::Imm:   visitor(Imm);   break;
        case EType::Label: visitor(Label); break;
        }
    }
};

// --- User-defined literal suffixes for IR helper types ---
namespace NLiterals {

// TOp from single-character literal: '+'_op, '*'
constexpr TOp operator""_op(char c) noexcept { return TOp(c); }
// TOp from string literal: "=="_op, "<="_op, ">="_op, "!="_op, "&&"_op, "||"_op
constexpr TOp operator""_op(const char* s, std::size_t) noexcept {
    return TOp(s);
}

// Immediate value: 42_imm
constexpr TImm operator""_imm(unsigned long long v) noexcept {
    return TImm{ static_cast<int64_t>(v) };
}

// Temporaries/slots/labels by index: 0_t, 1_sl, 2_lab
constexpr TTmp   operator""_t(unsigned long long v) noexcept   {
    return TTmp{static_cast<int32_t>(v) };
}
constexpr TSlot  operator""_sl(unsigned long long v) noexcept  {
    return TSlot{static_cast<int32_t>(v) };
}
constexpr TLabel operator""_lab(unsigned long long v) noexcept {
    return TLabel{static_cast<int32_t>(v) };
}

} // namespace NLiterals

struct TInstr {
    TOp Op;
    TTmp Dest = {.Idx = -1};
    std::array<TOperand, 4> Operands;
    uint8_t OperandCount = 0;
};

struct TBlock {
    TLabel Label;
    std::vector<TInstr> Instrs;
};

struct TExecFunc;
struct TModule;

struct TFunction {
    std::string Name;
    std::vector<TSlot> Slots;
    std::vector<TBlock> Blocks;
    std::vector<int> TmpTypes; // TmpId -> TypeId
    std::set<std::string> StringLiterals; // unique string literals used in the function
    int ReturnTypeId = -1;

    int SymId;
    int UniqueId; // unique within module, updated function will have same SymId and new UniqueId
    int32_t NextTmpIdx;
    int32_t NextLabelIdx;
    TExecFunc* Exec{nullptr};
    std::map<TLabel, int> LabelToBlockIdx;

    int GetTmpType(int tmpId) const {
        if (tmpId < 0 || tmpId >= (int)TmpTypes.size()) {
            return -1;
        }
        return TmpTypes[tmpId];
    }
    void Print(std::ostream& out, const TModule& module) const;
};

struct TModule {
    std::vector<TFunction> Functions;
    std::unordered_map<int, int> SymIdToFuncIdx;
    TTypeTable Types;

    std::vector<int> SlotTypes; // SlotId -> TypeId

    int GetSlotType(int slotId) const {
        if (slotId < 0 || slotId >= (int)SlotTypes.size()) {
            return -1;
        }
        return SlotTypes[slotId];
    }

    TFunction* GetFunctionByName(const std::string& name);
    void Print(std::ostream& out) const;
};

class TBuilder {
public:
    TBuilder(TModule& m);

    int NewFunction(std::string name, std::vector<TSlot> args, int symId); // returns function index
    std::pair<TLabel, int> NewBlock(TLabel label = {-1}); // label,id

    int CurrentFunctionIdx() const;
    int CurrentBlockIdx() const;
    TLabel CurrentBlockLabel() const;
    void SetCurrentBlock(int idx = -1); // -1 = last
    void SetCurrentBlock(TLabel label);
    void SetCurrentFunction(int idx = -1); // -1 = last

    TTmp Emit1(TOp op, std::initializer_list<TOperand> operands);
    void SetType(TTmp tmp, int typeId);
    int GetType(TTmp tmp) const;
    void SetType(TSlot slot, int typeId);
    void UnifyTypes(TTmp left, TTmp right);
    void SetReturnType(int typeId);
    void Emit0(TOp op, std::initializer_list<TOperand> operands);
    void* StringLiteral(const std::string& str);

    // Returns true if the last instruction in the current block unconditionally
    // or conditionally transfers control (e.g., jmp, ret, cmp), so no more
    // instructions should be appended to this block.
    bool IsCurrentBlockTerminated() const;
    TLabel NewLabel();

private:
    TTmp NewTmp();

    TModule& Module;
    TFunction* CurrentFunction = nullptr;
    TBlock* CurrentBlock = nullptr;

    int NextUniqueFunctionId = 0;
};

} // namespace NIR
} // namespace NOz
