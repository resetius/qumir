#include "vminstr.h"

namespace NQumir {
namespace NIR {

std::ostream& operator<<(std::ostream& os, EVMOp op) {
    switch (op) {
    case EVMOp::INeg: return os << "INeg";
    case EVMOp::INot: return os << "INot";
    case EVMOp::IAdd: return os << "IAdd";
    case EVMOp::ISub: return os << "ISub";
    case EVMOp::IMulS: return os << "IMulS";
    case EVMOp::IMulU: return os << "IMulU";
    case EVMOp::IDivS: return os << "IDivS";
    case EVMOp::IDivU: return os << "IDivU";
    case EVMOp::ICmpLTS: return os << "ICmpLTS";
    case EVMOp::ICmpLTU: return os << "ICmpLTU";
    case EVMOp::ICmpGTS: return os << "ICmpGTS";
    case EVMOp::ICmpGTU: return os << "ICmpGTU";
    case EVMOp::ICmpLES: return os << "ICmpLES";
    case EVMOp::ICmpLEU: return os << "ICmpLEU";
    case EVMOp::ICmpGES: return os << "ICmpGES";
    case EVMOp::ICmpGEU: return os << "ICmpGEU";
    case EVMOp::ICmpEQ: return os << "ICmpEQ";
    case EVMOp::ICmpNE: return os << "ICmpNE";

    case EVMOp::FNeg: return os << "FNeg";
    case EVMOp::FAdd: return os << "FAdd";
    case EVMOp::FSub: return os << "FSub";
    case EVMOp::FMul: return os << "FMul";
    case EVMOp::FDiv: return os << "FDiv";
    case EVMOp::FCmpLT: return os << "FCmpLT";
    case EVMOp::FCmpGT: return os << "FCmpGT";
    case EVMOp::FCmpLE: return os << "FCmpLE";
    case EVMOp::FCmpGE: return os << "FCmpGE";
    case EVMOp::FCmpEQ: return os << "FCmpEQ";
    case EVMOp::FCmpNE: return os << "FCmpNE";
    case EVMOp::Load8: return os << "Load8";
    case EVMOp::Load16: return os << "Load16";
    case EVMOp::Load32: return os << "Load32";
    case EVMOp::Load64: return os << "Load64";
    case EVMOp::Store8: return os << "Store8";
    case EVMOp::Store16: return os << "Store16";
    case EVMOp::Store32: return os << "Store32";
    case EVMOp::Store64: return os << "Store64";
    case EVMOp::Mov: return os << "Mov";
    case EVMOp::Cmov: return os << "Cmov";
    case EVMOp::I2F: return os << "I2F";
    case EVMOp::Jmp: return os << "Jmp";
    case EVMOp::Cmp: return os << "Cmp";
    case EVMOp::ArgTmp: return os << "ArgTmp";
    case EVMOp::ArgConst: return os << "ArgConst";
    case EVMOp::Call: return os << "Call";
    case EVMOp::Ret: return os << "Ret";
    case EVMOp::RetVoid: return os << "RetVoid";
    case EVMOp::OutI64: return os << "OutI64";
    case EVMOp::OutF64: return os << "OutF64";
    case EVMOp::OutS: return os << "OutS";
    default: return os << "EVMOp(" << static_cast<int>(op) << ")";
    }
}


std::ostream& operator<<(std::ostream& os, const TVMInstr& instr) {
    os << instr.Op << " ";
    for (size_t i = 0; i < instr.Operands.size(); ++i) {
        if (instr.Operands[i].Type == TVMOperand::EType::Tmp && instr.Operands[i].Tmp.Idx >= 0) {
            os << "tmp(" << instr.Operands[i].Tmp.Idx << ") ";
        } else if (instr.Operands[i].Type == TVMOperand::EType::Slot && instr.Operands[i].Slot.Idx >= 0) {
            os << "slot(" << instr.Operands[i].Slot.Idx << ") ";
        } else if (instr.Operands[i].Type == TVMOperand::EType::Imm) {
            os << "imm(" << instr.Operands[i].Imm.Value << ") ";
        }
    }
    return os;
}

} // namespace NIR
} // namespace NQumir