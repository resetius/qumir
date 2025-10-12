#include "eval.h"

#include <cstdint>
#include <iostream>
#include <cassert>
#include <sstream>
#include <cstring>
#include <iomanip>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace {

template<typename Dest=int64_t>
inline Dest ReadOperand(TFrame& f, const TVMOperand& op) {
    switch (op.Type) {
        case TVMOperand::EType::Tmp: {
            const auto& t = op.Tmp;
            assert(t.Idx >= 0 && t.Idx < f.Tmps.size());
            return std::bit_cast<Dest>(f.Tmps[t.Idx]);
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
inline int64_t EvalAlu(TFrame& f, const TVMInstr& instr, T lambda) {
    Dest lhs = ReadOperand<Dest>(f, instr.Operands[1]);
    Dest rhs = ReadOperand<Dest>(f, instr.Operands[2]);
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

TInterpreter::TInterpreter(TModule& module, TRuntime& runtime, std::ostream& out, std::istream& in)
    : Module(module)
    , Runtime(runtime)
    , Compiler(module)
    , Out(out)
    , In(in)
{ }

std::optional<std::string> TInterpreter::Eval(TFunction& function, std::vector<int64_t> args) {
    if (!function.Exec) {
        function.Exec = &Compiler.Compile(function);
    }
    std::vector<TFrame> callStack;
    auto* execFunc = function.Exec;
    callStack.push_back(TFrame {
        .Exec = execFunc,
        .Tmps = std::vector<int64_t>(execFunc->MaxTmpIdx + 1, 0),
        .Locals = std::vector<int64_t>(execFunc->NumLocals, 0),
        .Args = {},
        .PC = &execFunc->VMCode[0],
        .LastCmp = 0
    });

    auto& locals = callStack.back().Locals;
    if (args.size() != function.ArgLocals.size()) {
        std::cerr << "Function " << function.Name << " expects " << function.ArgLocals.size() << " arguments, got " << args.size() << "\n";
        return std::nullopt;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const int64_t sid = function.ArgLocals[i].Idx;
        locals[i] = args[i];
    }

    std::optional<std::string> result;
    std::optional<int64_t> retVal;

    while (!callStack.empty()) {
        auto& frame = callStack.back();
        assert(frame.PC <= &frame.Exec->VMCode[frame.Exec->VMCode.size()-1]);
        assert(frame.PC >= &frame.Exec->VMCode[0]);
        const auto& instr = *frame.PC++;

        switch (instr.Op) {
        case EVMOp::Load64: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            if (instr.Operands[1].Type == TVMOperand::EType::Slot) {
                const auto& s = instr.Operands[1].Slot;
                assert(s.Idx >= 0 && s.Idx < Runtime.Slots.size());
                frame.Tmps[instr.Operands[0].Tmp.Idx] = Runtime.Slots[s.Idx];
            } else if (instr.Operands[1].Type == TVMOperand::EType::Local) {
                const auto& l = instr.Operands[1].Local;
                assert(l.Idx >= 0 && l.Idx < frame.Locals.size());
                frame.Tmps[instr.Operands[0].Tmp.Idx] = frame.Locals[l.Idx];
            } else {
                assert(false && "Invalid operand for load");
            }
            break;
        }
        case EVMOp::Store64: {
            int64_t val = ReadOperand(frame, instr.Operands[1]);
            if (instr.Operands[0].Type == TVMOperand::EType::Slot) {
                // TODO:
                const auto& s = instr.Operands[0].Slot;
                if (s.Idx >= (int64_t)Runtime.Slots.size()) {
                    Runtime.Slots.resize(s.Idx + 1, 0);
                    Runtime.Inited.resize(s.Idx + 1, 0);
                }
                Runtime.Slots[s.Idx] = val;
                Runtime.Inited[s.Idx] = 1;
            } else if (instr.Operands[0].Type == TVMOperand::EType::Local) {
                const auto& l = instr.Operands[0].Local;
                assert(l.Idx >= 0 && l.Idx < frame.Locals.size());
                frame.Locals[l.Idx] = val;
            } else {
                assert(false && "Invalid operand for store");
            }
            break;
        }

        case EVMOp::INeg:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = -ReadOperand(frame, instr.Operands[1]);
            break;
        case EVMOp::FNeg: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            double tmp = ReadOperand<double>(frame, instr.Operands[1]);
            tmp = -tmp;
            std::memcpy(&frame.Tmps[instr.Operands[0].Tmp.Idx], &tmp, sizeof(double));
            break;
        }
        case EVMOp::INot:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = !ReadOperand(frame, instr.Operands[1]);
            break;

        case EVMOp::IAdd:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::plus<int64_t>{});
            break;
        case EVMOp::FAdd:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::plus<double>{});
            break;

        case EVMOp::ISub:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::minus<int64_t>{});
            break;
        case EVMOp::FSub:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::minus<double>{});
            break;

        case EVMOp::IMulS:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::multiplies<int64_t>{});
            break;
        case EVMOp::IMulU:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::multiplies<uint64_t>{});
            break;
        case EVMOp::FMul:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::multiplies<double>{});
            break;

        case EVMOp::IDivS:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::divides<int64_t>{});
            break;
        case EVMOp::IDivU:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::divides<uint64_t>{});
            break;
        case EVMOp::FDiv:
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::divides<double>{});
            break;

        case EVMOp::ICmpLTS: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::less<int64_t>{});
            break;
        case EVMOp::ICmpLTU: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::less<uint64_t>{});
            break;
        case EVMOp::FCmpLT: // <
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::less<double>{});
            break;

        case EVMOp::ICmpGTS: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::greater<int64_t>{});
            break;
        case EVMOp::ICmpGTU: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::greater<uint64_t>{});
            break;
        case EVMOp::FCmpGT: // >
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::greater<double>{});
            break;

        case EVMOp::ICmpLES: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::less_equal<int64_t>{});
            break;
        case EVMOp::ICmpLEU: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::less_equal<uint64_t>{});
            break;
        case EVMOp::FCmpLE: // <=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::less_equal<double>{});
            break;

        case EVMOp::ICmpGES: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::greater_equal<int64_t>{});
            break;
        case EVMOp::ICmpGEU: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<uint64_t>(frame, instr, std::greater_equal<uint64_t>{});
            break;
        case EVMOp::FCmpGE: // >=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::greater_equal<double>{});
            break;

        case EVMOp::ICmpEQ: // ==
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::equal_to<int64_t>{});
            break;
        case EVMOp::FCmpEQ: // ==
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::equal_to<double>{});
            break;

        case EVMOp::ICmpNE: // !=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<int64_t>(frame, instr, std::not_equal_to<int64_t>{});
            break;
        case EVMOp::FCmpNE: // !=
            assert(instr.Operands[0].Tmp.Idx >= 0);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = EvalAlu<double>(frame, instr, std::not_equal_to<double>{});
            break;

        case EVMOp::Cmov:
            // TODO: dont use ReadOperand
        case EVMOp::Mov: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            int64_t val = ReadOperand(frame, instr.Operands[1]);
            frame.Tmps[instr.Operands[0].Tmp.Idx] = val;
            break;
        }
        case EVMOp::I2F: {
            assert(instr.Operands[0].Tmp.Idx >= 0);
            int64_t ival = ReadOperand<int64_t>(frame, instr.Operands[1]);
            double fval = static_cast<double>(ival);
            int64_t ret = 0;
            std::memcpy(&ret, &fval, sizeof(fval));
            frame.Tmps[instr.Operands[0].Tmp.Idx] = ret;
            break;
        }

        case EVMOp::Jmp: {
            assert(instr.Operands[0].Type == TVMOperand::EType::Imm);
            frame.PC = reinterpret_cast<TVMInstr*>(instr.Operands[0].Imm.Value);
            break;
        }
        case EVMOp::Cmp: {
            int64_t cmp = ReadOperand(frame, instr.Operands[0]);
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
            auto value = ReadOperand(frame, instr.Operands[0]);
            frame.Args.push_back(value);
            break;
        }
        case EVMOp::ECall: {// external call
            using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);
            void* addr = reinterpret_cast<void*>(instr.Operands[1].Imm.Value);
            TPacked func = reinterpret_cast<TPacked>(addr);

            if (instr.Operands[0].Tmp.Idx >= 0) {
                frame.Tmps[instr.Operands[0].Tmp.Idx] = func(reinterpret_cast<const uint64_t*>(frame.Args.data()), frame.Args.size());
            } else {
                func(reinterpret_cast<const uint64_t*>(frame.Args.data()), frame.Args.size());
            }
            frame.Args.clear();

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
            const int argCount = (int)frame.Args.size();
            assert(argCount <= (int)localArgs.size() && "too many arguments for callee");

            auto locals = std::vector<int64_t>(calleeExec->NumLocals, 0);

            // save slots that will be overwritten by parameters
            for (int i = 0; i < argCount; ++i) {
                const int64_t sid = localArgs[i].Idx;
                locals[sid] = frame.Args[i];
            }

            ReturnLinks.emplace_back(TReturnLink {
                .FrameIdx = (int64_t) callStack.size() - 1,
                .CallerDst = instr.Operands[0].Tmp.Idx,
            });

            frame.Args.clear();
            callStack.push_back(TFrame {
                .Exec = calleeExec,
                .Tmps = std::vector<int64_t>(calleeExec->MaxTmpIdx + 1, 0),
                .Locals = std::move(locals),
                .Args = {},
                .PC = &calleeExec->VMCode[0],
                .LastCmp = 0
            });
            break;
        }
        case EVMOp::Ret:
            retVal = ReadOperand(frame, instr.Operands[0]);
        case EVMOp::RetVoid: {
            callStack.pop_back();
            if (callStack.empty()) {
                break;
            } else {
                assert(!ReturnLinks.empty());
                auto link = std::move(ReturnLinks.back());
                ReturnLinks.pop_back();

                auto& linkFrame = callStack[link.FrameIdx];
                if (retVal.has_value()) {
                    linkFrame.Tmps[link.CallerDst] = *retVal;
                }
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
        if (function.ReturnTypeId >= 0) {
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
