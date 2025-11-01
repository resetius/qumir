#include "eval.h"

#include <cstdint>
#include <iostream>
#include <cassert>
#include <sstream>
#include <cstring>
#include <iomanip>

#include <qumir/runtime/string.h> // for str_release

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace {

template<typename Dest=int64_t>
inline Dest ReadOperand(const std::vector<int64_t>& regs, const TVMOperand& op) {
    switch (op.Type) {
        case TVMOperand::EType::Tmp: {
            const auto& t = op.Tmp;
            assert(t.Idx >= 0 && t.Idx < regs.size());
            return std::bit_cast<Dest>(regs[t.Idx]);
        }
        case TVMOperand::EType::Imm: {
            const auto& i = op.Imm;
            return std::bit_cast<Dest>(i.Value);
        }
        default: {
            assert(false && "Slot operand not supported in ALU operations");
            return 0;
        }
    }
}

template<typename Dest, typename T>
inline int64_t EvalAlu(const std::vector<int64_t>& regs, const TVMInstr& instr, T lambda) {
    Dest lhs = ReadOperand<Dest>(regs, instr.Operands[1]);
    Dest rhs = ReadOperand<Dest>(regs, instr.Operands[2]);
    auto res = lambda(lhs, rhs);
    if constexpr (std::is_same_v<decltype(res), int64_t>) {
        return res;
    } else {
        int64_t ret = 0;
        std::memcpy(&ret, &res, std::min(sizeof(res), sizeof(ret)));
        return ret;
    }
}

} // namespace

TInterpreter::TInterpreter(TModule& module, std::ostream& out, std::istream& in)
    : Module(module)
    , Compiler(module)
    , Out(out)
    , In(in)
{ }

std::optional<std::string> TInterpreter::Eval(TFunction& function, std::vector<int64_t> args) {
    if (!function.Exec) {
        function.Exec = &Compiler.Compile(function);
    }
    std::vector<TFrame> callStack; callStack.reserve(16);
    auto* execFunc = function.Exec;
    callStack.push_back(TFrame {
        .Exec = execFunc,
        .UsedRegs = execFunc->MaxTmpIdx + 1,
        .StackBase = 0,
        .PC = &execFunc->VMCode[0],
        .Name = function.Name,
    });

    Runtime.Regs.resize(execFunc->MaxTmpIdx + 1, 0);
    Runtime.Stack.resize(execFunc->NumLocals, 0);
    if (args.size() != function.ArgLocals.size()) {
        std::cerr << "Function " << function.Name << " expects " << function.ArgLocals.size() << " arguments, got " << args.size() << "\n";
        return std::nullopt;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const int64_t sid = function.ArgLocals[i].Idx;
        Runtime.Stack[i] = args[i];
    }

    std::optional<std::string> result;
    std::optional<int64_t> retVal;

    while (!callStack.empty()) {
        auto& frame = callStack.back();
        assert(frame.PC <= &frame.Exec->VMCode[frame.Exec->VMCode.size()-1]);
        assert(frame.PC >= &frame.Exec->VMCode[0]);
        const auto& instr = *frame.PC++;

        switch (instr.Op) {
        case EVMOp::Ste: {
            int64_t intAddr = ReadOperand<int64_t>(Runtime.Regs, instr.Operands[0]);
            void* addr = reinterpret_cast<void*>(intAddr);
            int64_t value = ReadOperand<int64_t>(Runtime.Regs, instr.Operands[1]);
            std::memcpy(addr, &value, sizeof(int64_t)); // TODO: size (add size operand?)
            break;
        }
        case EVMOp::Lde: {
            int64_t intAddr = ReadOperand<int64_t>(Runtime.Regs, instr.Operands[1]);
            void* addr = reinterpret_cast<void*>(intAddr);
            int64_t value = 0;
            std::memcpy(&value, addr, sizeof(int64_t)); // TODO: size (add size operand?)
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = value;
            break;
        }
        case EVMOp::Load64: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            if (instr.Operands[1].Type == TVMOperand::EType::Slot) {
                const auto& s = instr.Operands[1].Slot;
                assert(s.Idx >= 0 && s.Idx < Runtime.Globals.size());
                Runtime.Regs[instr.Operands[0].Tmp.Idx] = Runtime.Globals[s.Idx];
            } else if (instr.Operands[1].Type == TVMOperand::EType::Local) {
                const auto& l = instr.Operands[1].Local;
                assert(l.Idx >= 0 && l.Idx < Runtime.Stack.size() - frame.StackBase);
                Runtime.Regs[instr.Operands[0].Tmp.Idx] = Runtime.Stack[frame.StackBase + l.Idx];
            } else {
                assert(false && "Invalid operand for load");
            }
            break;
        }
        case EVMOp::Store64: {
            int64_t val = ReadOperand(Runtime.Regs, instr.Operands[1]);
            if (instr.Operands[0].Type == TVMOperand::EType::Slot) {
                // TODO:
                const auto& s = instr.Operands[0].Slot;
                if (s.Idx >= (int64_t)Runtime.Globals.size()) {
                    Runtime.Globals.resize(s.Idx + 1, 0);
                }
                Runtime.Globals[s.Idx] = val;
            } else if (instr.Operands[0].Type == TVMOperand::EType::Local) {
                const auto& l = instr.Operands[0].Local;
                assert(l.Idx >= 0 && l.Idx < Runtime.Stack.size() - frame.StackBase);
                Runtime.Stack[frame.StackBase + l.Idx] = val;
            } else {
                assert(false && "Invalid operand for store");
            }
            break;
        }

        case EVMOp::INeg:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = -ReadOperand(Runtime.Regs, instr.Operands[1]);
            break;
        case EVMOp::FNeg: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            double tmp = ReadOperand<double>(Runtime.Regs, instr.Operands[1]);
            tmp = -tmp;
            std::memcpy(&Runtime.Regs[instr.Operands[0].Tmp.Idx], &tmp, sizeof(double));
            break;
        }
        case EVMOp::INot:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = !ReadOperand(Runtime.Regs, instr.Operands[1]);
            break;

        case EVMOp::IAdd:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::plus<int64_t>{});
            break;
        case EVMOp::FAdd:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::plus<double>{});
            break;

        case EVMOp::ISub:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::minus<int64_t>{});
            break;
        case EVMOp::FSub:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::minus<double>{});
            break;

        case EVMOp::IMulS:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::multiplies<int64_t>{});
            break;
        case EVMOp::IMulU:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::multiplies<uint64_t>{});
            break;
        case EVMOp::FMul:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::multiplies<double>{});
            break;

        case EVMOp::IDivS:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::divides<int64_t>{});
            break;
        case EVMOp::IDivU:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::divides<uint64_t>{});
            break;
        case EVMOp::FDiv:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::divides<double>{});
            break;

        case EVMOp::ICmpLTS: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::less<int64_t>{});
            break;
        case EVMOp::ICmpLTU: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::less<uint64_t>{});
            break;
        case EVMOp::FCmpLT: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::less<double>{});
            break;

        case EVMOp::ICmpGTS: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::greater<int64_t>{});
            break;
        case EVMOp::ICmpGTU: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::greater<uint64_t>{});
            break;
        case EVMOp::FCmpGT: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::greater<double>{});
            break;

        case EVMOp::ICmpLES: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::less_equal<int64_t>{});
            break;
        case EVMOp::ICmpLEU: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::less_equal<uint64_t>{});
            break;
        case EVMOp::FCmpLE: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::less_equal<double>{});
            break;

        case EVMOp::ICmpGES: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::greater_equal<int64_t>{});
            break;
        case EVMOp::ICmpGEU: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(Runtime.Regs, instr, std::greater_equal<uint64_t>{});
            break;
        case EVMOp::FCmpGE: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::greater_equal<double>{});
            break;

        case EVMOp::ICmpEQ: // ==
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::equal_to<int64_t>{});
            break;
        case EVMOp::FCmpEQ: // ==
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::equal_to<double>{});
            break;

        case EVMOp::ICmpNE: // !=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(Runtime.Regs, instr, std::not_equal_to<int64_t>{});
            break;
        case EVMOp::FCmpNE: // !=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(Runtime.Regs, instr, std::not_equal_to<double>{});
            break;

        case EVMOp::Cmov:
            // TODO: dont use ReadOperand
        case EVMOp::Mov: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            int64_t val = ReadOperand(Runtime.Regs, instr.Operands[1]);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = val;
            break;
        }
        case EVMOp::I2F: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            int64_t ival = ReadOperand<int64_t>(Runtime.Regs, instr.Operands[1]);
            double fval = static_cast<double>(ival);
            int64_t ret = 0;
            std::memcpy(&ret, &fval, sizeof(fval));
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = ret;
            break;
        }
        case EVMOp::F2I: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            double fval = ReadOperand<double>(Runtime.Regs, instr.Operands[1]);
            int64_t ival = static_cast<int64_t>(fval);
            Runtime.Regs[instr.Operands[0].Tmp.Idx] = ival;
            break;
        }

        case EVMOp::Jmp: {
            assert(instr.Operands[0].Type == TVMOperand::EType::Imm);
            frame.PC = reinterpret_cast<TVMInstr*>(instr.Operands[0].Imm.Value);
            break;
        }
        case EVMOp::Cmp: {
            int64_t cmp = ReadOperand(Runtime.Regs, instr.Operands[0]);
            assert(instr.Operands[1].Type == TVMOperand::EType::Imm);
            assert(instr.Operands[2].Type == TVMOperand::EType::Imm);
            int64_t trueLabel = instr.Operands[1].Imm.Value;
            int64_t falseLabel = instr.Operands[2].Imm.Value;
            if (cmp) {
                frame.PC = reinterpret_cast<TVMInstr*>(trueLabel);
            } else {
                frame.PC = reinterpret_cast<TVMInstr*>(falseLabel);
            }
            break;
        }
        case EVMOp::ArgTmp: // TODO: optimize
        case EVMOp::ArgConst: {
            auto value = ReadOperand(Runtime.Regs, instr.Operands[0]);
            Runtime.Args.push_back(value);
            break;
        }
        case EVMOp::ECall: {// external call
            using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);
            void* addr = reinterpret_cast<void*>(instr.Operands[1].Imm.Value);
            TPacked func = reinterpret_cast<TPacked>(addr);

            if (instr.Operands[0].Tmp.Idx >= 0) {
                Runtime.Regs[instr.Operands[0].Tmp.Idx] = func(reinterpret_cast<const uint64_t*>(Runtime.Args.data()), Runtime.Args.size());
            } else {
                func(reinterpret_cast<const uint64_t*>(Runtime.Args.data()), Runtime.Args.size());
            }
            Runtime.Args.clear();

            break;
        }
        case EVMOp::Call: {
            assert(instr.Operands[1].Type == TVMOperand::EType::Imm && "callee must be Imm(id)");
            const int64_t calleeId = instr.Operands[1].Imm.Value;

            assert(calleeId >=0 && calleeId < Module.Functions.size() && "Invalid callee id");
            TFunction* calleeFn = Module.Functions.data() + calleeId;

            if (!calleeFn->Exec) {
                calleeFn->Exec = &Compiler.Compile(*calleeFn);
            }
            auto* calleeExec = calleeFn->Exec;

            const auto& localArgs = calleeFn->ArgLocals;
            const int argCount = (int)Runtime.Args.size();
            assert(argCount <= (int)localArgs.size() && "too many arguments for callee");

            for (int i = 0; i < frame.UsedRegs; ++i) {
                Runtime.Stack.push_back(Runtime.Regs[i]);
            }
            auto base = Runtime.Stack.size();

            Runtime.Regs.resize(calleeExec->MaxTmpIdx + 1, 0);
            Runtime.Stack.resize(Runtime.Stack.size() + calleeExec->NumLocals, 0);

            // save slots that will be overwritten by parameters
            for (int i = 0; i < argCount; ++i) {
                const int64_t sid = localArgs[i].Idx;
                Runtime.Stack[base + sid] = Runtime.Args[i];
            }

            ReturnLinks.emplace_back(TReturnLink {
                .FrameIdx = (int64_t) callStack.size() - 1,
                .CallerDst = instr.Operands[0].Tmp.Idx,
            });

            Runtime.Args.clear();
            callStack.push_back(TFrame {
                .Exec = calleeExec,
                .UsedRegs = calleeExec->MaxTmpIdx + 1,
                .StackBase = base,
                .PC = &calleeExec->VMCode[0],
                .Name = calleeFn->Name,
            });
            break;
        }
        case EVMOp::Ret:
            retVal = ReadOperand(Runtime.Regs, instr.Operands[0]);
        case EVMOp::RetVoid: {
            auto base = frame.StackBase;
            callStack.pop_back();
            if (callStack.empty()) {
                break;
            } else {
                auto& callerFrame = callStack.back();
                assert(!ReturnLinks.empty());
                auto link = std::move(ReturnLinks.back());
                ReturnLinks.pop_back();

                auto& linkFrame = callStack[link.FrameIdx];
                Runtime.Stack.resize(base);
                // restore used regs
                Runtime.Regs.resize(callerFrame.UsedRegs);
                for (int i = 0; i < callerFrame.UsedRegs; ++i) {
                    // TODO: test this
                    Runtime.Regs[callerFrame.UsedRegs - i - 1] = Runtime.Stack[base - i - 1];
                }
                if (retVal.has_value()) {
                    Runtime.Regs[link.CallerDst] = *retVal;
                }
                Runtime.Stack.resize(base - callerFrame.UsedRegs);
                retVal = std::nullopt;
            }
            break;
        }
        default:
            std::cerr << "Unknown instruction: '" << (int)instr.Op << "'\n";
            throw std::runtime_error("Unknown instruction");
            break;
        }
    }

    if (retVal.has_value()) {
        std::ostringstream out;
        if (function.ReturnTypeIsString) {
            // TODO: remove me, clutch: support string returnType
            char* strPtr = reinterpret_cast<char*>(std::bit_cast<uint64_t>(*retVal));
            out << strPtr;
            NRuntime::str_release(strPtr);
        } else if (function.ReturnTypeId >= 0) {
            Module.Types.Format(out, std::bit_cast<uint64_t>(*retVal), function.ReturnTypeId);
        } else {
            out << *retVal;
        }
        result = out.str();
    }
    return result;
}

} // namespace NIR
} // namespace NQumir
