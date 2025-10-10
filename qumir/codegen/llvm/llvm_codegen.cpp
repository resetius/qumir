#include "llvm_codegen.h"
#include "llvm_codegen_impl.h"

#include <qumir/ir/builder.h>
#include <qumir/ir/eval.h>

#include <memory>
#include <string>
#include <iostream>
#include <unordered_set>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Config/llvm-config.h>

// For optimization
#include <llvm/Passes/PassBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

namespace NQumir::NCodeGen {

using namespace NQumir::NIR;
using namespace NQumir::NIR::NLiterals;

namespace {

// todo: type system
bool FunHasVoidRet(const TFunction& fun) {
    for (const auto& b : fun.Blocks) {
        for (const auto& instr : b.Instrs) {
            if (instr.Op == "ret"_op && instr.OperandCount == 0) {
                return true;
            }
        }
    }
    return false;
}

llvm::Type* GetTypeById(int typeId, const TTypeTable& tt, llvm::LLVMContext& ctx) {
    if (typeId < 0) {
        // untyped, assume int64
        return llvm::Type::getInt64Ty(ctx);
    }
    switch (tt.GetKind(typeId)) {
        case EKind::I1: return llvm::Type::getInt1Ty(ctx);
        case EKind::I64: return llvm::Type::getInt64Ty(ctx);
        case EKind::F64: return llvm::Type::getDoubleTy(ctx);
        case EKind::Void: return llvm::Type::getVoidTy(ctx);
        case EKind::Ptr: return llvm::Type::getInt64Ty(ctx); // TODO
        case EKind::Func: return llvm::Type::getInt64Ty(ctx); // Function Id so far, TODO
        default:
            throw std::runtime_error("unsupported primitive type");
    }
}

llvm::Type* GetFunctionRetType(const TFunction& fun, const TTypeTable& tt, llvm::LLVMContext& ctx) {
    if (fun.ReturnTypeId < 0) {
        // untyped function, assume int64
        if (FunHasVoidRet(fun)) {
            return llvm::Type::getVoidTy(ctx);
        } else {
            return llvm::Type::getInt64Ty(ctx);
        }
    }

    return GetTypeById(fun.ReturnTypeId, tt, ctx);
}

std::unordered_map<uint64_t, llvm::CmpInst::Predicate> icmpMap = {
    { "<"_op,  llvm::CmpInst::ICMP_SLT },
    { "<="_op, llvm::CmpInst::ICMP_SLE },
    { ">"_op,  llvm::CmpInst::ICMP_SGT },
    { ">="_op, llvm::CmpInst::ICMP_SGE },
    { "=="_op, llvm::CmpInst::ICMP_EQ },
    { "!="_op, llvm::CmpInst::ICMP_NE },
};

std::unordered_map<uint64_t, llvm::CmpInst::Predicate> fcmpMap = {
    { "<"_op,  llvm::CmpInst::FCMP_ULT },
    { "<="_op, llvm::CmpInst::FCMP_ULE },
    { ">"_op,  llvm::CmpInst::FCMP_UGT },
    { ">="_op, llvm::CmpInst::FCMP_UGE },
    { "=="_op, llvm::CmpInst::FCMP_UEQ },
    { "!="_op, llvm::CmpInst::FCMP_UNE },
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> ibinOpMap = {
    { "+"_op, llvm::Instruction::Add },
    { "-"_op, llvm::Instruction::Sub },
    { "*"_op, llvm::Instruction::Mul },
    { "/"_op, llvm::Instruction::SDiv }, // signed division
    { "%"_op, llvm::Instruction::SRem }, // signed remainder
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> ubinOpMap = {
    { "+"_op, llvm::Instruction::Add },
    { "-"_op, llvm::Instruction::Sub },
    { "*"_op, llvm::Instruction::Mul },
    { "/"_op, llvm::Instruction::UDiv }, // unsigned division
    { "%"_op, llvm::Instruction::URem }, // unsigned remainder
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> fbinOpMap = {
    { "+"_op, llvm::Instruction::FAdd },
    { "-"_op, llvm::Instruction::FSub },
    { "*"_op, llvm::Instruction::FMul },
    { "/"_op, llvm::Instruction::FDiv },
};

} // namespace

TLLVMCodeGen::TLLVMCodeGen(const TLLVMCodeGenOptions& opts): Opts(opts) {}
TLLVMCodeGen::~TLLVMCodeGen() = default;

std::unique_ptr<ILLVMModuleArtifacts> TLLVMCodeGen::Emit(const TModule& module, int optLevel) {
    Ctx = std::make_unique<llvm::LLVMContext>();
    LModule = std::make_unique<llvm::Module>(Opts.ModuleName, *Ctx);

    if (optLevel > 0) {
        CreateTargetMachine();
    }

    auto builder = std::make_unique<llvm::IRBuilder<>>(*Ctx);
    BuilderBase = std::move(builder);

    std::unordered_set<int> newSymIds;
    // Pass 1: predeclare all functions so calls can reference them by SymId in any order
    for (const auto& f : module.Functions) {
        auto maybeUniqueIdIt = SymIdToUniqueFunId.find(f.SymId);
        if (maybeUniqueIdIt != SymIdToUniqueFunId.end()) {
            if (maybeUniqueIdIt->second == f.UniqueId) {
                continue;
            } else {
                // delete old function and re-declare
                auto* oldFun = LModule->getFunction(f.Name);
                if (oldFun) {
                    oldFun->eraseFromParent();
                }
            }
        }

        auto& ctx = *Ctx;
        std::vector<llvm::Type*> argTys(f.ArgLocals.size(), nullptr);
        for (size_t i = 0; i < f.ArgLocals.size(); ++i) {
            const auto& s = f.ArgLocals[i];
            auto typeId = f.LocalTypes[s.Idx];
            auto type = GetTypeById(typeId, module.Types, ctx);
            argTys[i] = type;
        }

        llvm::Type* retTy = GetFunctionRetType(f, module.Types, ctx);
        auto fty = llvm::FunctionType::get(retTy, argTys, false);
        auto lfun = LModule->getFunction(f.Name);
        if (!lfun) {
            lfun = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, f.Name, LModule.get());
        } else {
            throw std::runtime_error("function already declared");
        }
        if (f.Name.find("__repl") == 0) { // repl functions should not be optimized
            lfun->addFnAttr(llvm::Attribute::NoInline);
            lfun->addFnAttr(llvm::Attribute::OptimizeNone);
            lfun->addFnAttr("disable-tail-calls", "true");
        }
        SymIdToLFun[f.SymId] = lfun;
        newSymIds.insert(f.SymId);
        SymIdToUniqueFunId[f.SymId] = f.UniqueId;
    }

    // Pass 2: lower function bodies
    for (const auto& f : module.Functions) {
        if (newSymIds.find(f.SymId) == newSymIds.end()) continue;
        LowerFunction(f, module);
    }

    if (llvm::verifyModule(*LModule, &llvm::errs())) {
        llvm::errs() << "\n[LLVMCodeGen] Module verify failed. Dumping IR:\n";
        LModule->print(llvm::errs(), nullptr);
        throw std::runtime_error("LLVM verify failed");
    }

    if (optLevel > 0) {
        Optimize(optLevel);
    }

    auto out = std::make_unique<TLLVMModuleArtifacts>();
    out->Ctx = std::move(Ctx);
    out->Module = std::move(LModule);
    // Collect defined function names for tests without pulling in LLVM headers
    if (out->Module) {
        for (auto& F : *out->Module) {
            if (!F.isDeclaration()) {
                out->FunctionNames.push_back(F.getName().str());
            }
        }
    }
    return out;
}

llvm::GlobalVariable* TLLVMCodeGen::EnsureSlotGlobal(int64_t sidx, const NIR::TModule& module)
{
    if (sidx < 0) throw std::runtime_error("negative slot index");
    if (sidx >= (int64_t)ModuleSlots.size()) ModuleSlots.resize(sidx + 1, nullptr);
    if (!ModuleSlots[sidx]) {
        auto *slotTy = GetTypeById(module.GetSlotType(sidx), module.Types, *Ctx);
        auto *g = new llvm::GlobalVariable(*LModule, slotTy, /*isConstant*/false, llvm::GlobalValue::InternalLinkage,
            // slotTy is float ? f64 : i64
            slotTy->isFloatingPointTy() ? llvm::ConstantFP::get(slotTy, 0.0) : llvm::ConstantInt::get(slotTy, 0, true),
            "slot" + std::to_string(sidx));
        ModuleSlots[sidx] = g;
    }
    return static_cast<llvm::GlobalVariable*>(ModuleSlots[sidx]);
}

llvm::Function* TLLVMCodeGen::LowerFunction(const TFunction& fun, const NIR::TModule& module) {
    auto& ctx = *Ctx;
    auto lfun = LModule->getFunction(fun.Name);
    // Function has already been registered in Emit pre-pass

    CurFun = std::make_unique<TFunState>();
    CurFun->Fun = &fun;
    CurFun->LFun = lfun;
    CurFun->TmpValues.resize(fun.NextTmpIdx, nullptr);

    std::vector<llvm::BasicBlock*> bbs; bbs.reserve(fun.Blocks.size());
    for (const auto& b : fun.Blocks) {
        auto *bb = llvm::BasicBlock::Create(ctx, "bb" + std::to_string(b.Label.Idx), lfun);
        bbs.push_back(bb);
        CurFun->LabelToBB[b.Label.Idx] = bb;
    }

    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    irb->SetInsertPoint(bbs.front());

    CurFun->Allocas.resize(fun.LocalTypes.size(), nullptr);
    for (int i = 0; i < (int)fun.LocalTypes.size(); ++i) {
        auto* localTy = GetTypeById(fun.LocalTypes[i], module.Types, ctx);
        auto* alloca = irb->CreateAlloca(localTy, nullptr, "local" + std::to_string(i));
        CurFun->Allocas[i] = alloca;
    }

    for (int i = 0; i < (int)fun.ArgLocals.size(); ++i) {
        const auto& l = fun.ArgLocals[i];
        if (l.Idx < 0 || l.Idx >= (int)CurFun->Allocas.size()) {
            throw std::runtime_error("invalid argument local index");
        }
        auto* ptr = CurFun->Allocas[l.Idx];
        auto& arg = *lfun->getArg(i);
        auto* dstTy = ptr->getAllocatedType();
        llvm::Value* av = &arg;
        if (av->getType() != dstTy) {
            if (av->getType()->isIntegerTy() && dstTy->isIntegerTy()) {
                av = irb->CreateIntCast(av, dstTy, true);
            } else if (av->getType()->isFloatingPointTy() && dstTy->isFloatingPointTy()) {
                av = irb->CreateFPCast(av, dstTy);
            } else if (av->getType()->isIntegerTy() && dstTy->isFloatingPointTy()) {
                av = irb->CreateSIToFP(av, dstTy);
            } else if (av->getType()->isFloatingPointTy() && dstTy->isIntegerTy()) {
                av = irb->CreateFPToSI(av, dstTy);
            } else {
                av = irb->CreateBitCast(av, dstTy);
            }
        }
        irb->CreateStore(av, ptr);
    }

    for (size_t i = 0; i < fun.Blocks.size(); ++i) {
        auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
        irb->SetInsertPoint(bbs[i]);
        LowerBlock(fun.Blocks[i], module, lfun, bbs);
    }
    return lfun;
}

void TLLVMCodeGen::LowerBlock(const TBlock& blk, const NIR::TModule& module, llvm::Function*, std::vector<llvm::BasicBlock*>& orderedBBs) {
    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());

    for (const auto& instr : blk.Instrs) {
        if (irb->GetInsertBlock()->getTerminator()) {
            throw std::runtime_error("attempt to emit instruction after terminator");
        }
        LowerInstr(instr, module);
    }
}

llvm::Value* TLLVMCodeGen::LowerInstr(const TInstr& instr, const NIR::TModule& module) {
    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    auto& ctx = irb->getContext();
    auto i64 = llvm::Type::getInt64Ty(ctx);
    auto f64 = llvm::Type::getDoubleTy(ctx);
    llvm::Type* outputType = nullptr;
    int outputTypeId = -1;

    if (instr.Dest.Idx >= 0) {
        outputTypeId = CurFun->Fun->GetTmpType(instr.Dest.Idx);
        outputType = GetTypeById(outputTypeId, module.Types, ctx);
    }

    auto getOp = [&](const TOperand& op) -> llvm::Value* {
        switch (op.Type) {
            case TOperand::EType::Imm:
                if (op.Imm.IsFloat) {
                    return llvm::ConstantFP::get(f64, std::bit_cast<double>(op.Imm.Value));
                } else {
                    return llvm::ConstantInt::get(i64, op.Imm.Value, true);
                }
            case TOperand::EType::Tmp: {
                if (op.Tmp.Idx < 0 || op.Tmp.Idx >= CurFun->TmpValues.size()) {
                    throw std::runtime_error("invalid temporary index: " + std::to_string(op.Tmp.Idx));
                }
                if (!CurFun->TmpValues[op.Tmp.Idx]) {
                    throw std::runtime_error("use of uninitialized temporary: " + std::to_string(op.Tmp.Idx));
                }
                return CurFun->TmpValues[op.Tmp.Idx];
            }
            default:
                throw std::runtime_error("unsupported operand type in ALU instruction");
        };
    };

    auto opcode = instr.Op;
    auto storeTmp = [&](llvm::Value* v){
        if (instr.Dest.Idx >= 0) {
            if (instr.Dest.Idx >= CurFun->TmpValues.size()) {
                CurFun->TmpValues.resize(instr.Dest.Idx + 1, nullptr);
            }
            CurFun->TmpValues[instr.Dest.Idx] = v;
        }
        return v;
    };

    auto commonType = [&](llvm::Value* a, llvm::Value* b) -> llvm::Type* {
        auto left = a->getType();
        auto right = b->getType();
        if (left == right) {
            return left;
        } else if (left->isFloatingPointTy() || right->isFloatingPointTy()) {
            return f64;
        } else if (left->isFloatingPointTy() || right->isIntegerTy()) {
            return f64;
        } else if (left->isIntegerTy() || right->isFloatingPointTy()) {
            return f64;
        } else if (left->isIntegerTy() && right->isIntegerTy()) {
            return i64;
        }
        throw std::runtime_error("unsupported type for commonType");
    };

    auto cast = [&](llvm::Value* val, llvm::Type* expectedType) -> llvm::Value* {
        auto* actualTy = val->getType();

        if (actualTy == expectedType) {
            return val;
        } else if (actualTy->isIntegerTy() && expectedType->isIntegerTy()) {
            // integer to integer cast
            if (expectedType->getIntegerBitWidth() == 1) {
                return irb->CreateICmpNE(val, llvm::ConstantInt::get(val->getType(), 0), "asbool");
            }
            // downcast
            return irb->CreateIntCast(val, expectedType, true, "cast");
        } else if (actualTy->isFloatingPointTy() && expectedType->isFloatingPointTy()) {
            // Float cast (f32 <-> f64)
            return irb->CreateFPCast(val, expectedType, "cast");
        } else if (actualTy->isIntegerTy() && expectedType->isFloatingPointTy()) {
            // Int to float
            return irb->CreateSIToFP(val, expectedType, "cast");
        } else if (actualTy->isFloatingPointTy() && expectedType->isIntegerTy()) {
            if (expectedType->getIntegerBitWidth() == 1) {
                return irb->CreateFCmpUNE(val, llvm::ConstantFP::get(val->getType(), 0.0), "asbool");
            }
            // Float to int
            return irb->CreateFPToSI(val, expectedType, "cast");
        } else {
            // bitcast
            return irb->CreateBitCast(val, expectedType, "cast");
        }
    };

    auto cmpInsr = [&](llvm::CmpInst::Predicate pred, llvm::Value* lhs, llvm::Value* rhs) -> llvm::Value* {
        auto i1v = irb->CreateCmp(pred, lhs, rhs, "cmprtmp");
        return i1v;
    };

    auto binOp = [&](llvm::Instruction::BinaryOps op) -> llvm::Value* {
        auto lhs = cast(getOp(instr.Operands[0]), outputType);
        auto rhs = cast(getOp(instr.Operands[1]), outputType);
        return irb->CreateBinOp(op, lhs, rhs, "bintmp");
    };

    switch (opcode) {
        case "+"_op:
        case "-"_op:
        case "*"_op:
        case "/"_op: {
            if (outputType == nullptr) {
                throw std::runtime_error("arithmetic op needs a typed dest");
            }
            if (outputType->isFloatingPointTy()) {
                auto it = fbinOpMap.find(opcode);
                if (it == fbinOpMap.end()) throw std::runtime_error("unsupported fbinop opcode");
                return storeTmp(binOp(it->second));
            } else if (outputType->isIntegerTy()) {
                if (true /*module.Types.IsSigned(outputTypeId) always signed*/) {
                    // signed integer ops
                    auto it = ibinOpMap.find(opcode);
                    if (it == ibinOpMap.end()) throw std::runtime_error("unsupported ibinop opcode");
                    return storeTmp(binOp(it->second));
                } else {
                    // unsigned integer ops
                    auto it = ubinOpMap.find(opcode);
                    if (it == ubinOpMap.end()) throw std::runtime_error("unsupported ubinop opcode");
                    return storeTmp(binOp(it->second));
                }
            } else {
                throw std::runtime_error("unsupported type for arithmetic op");
            }
        }
        // Relational / equality operators: produce i64 0/1
        case "<"_op:
        case "<="_op:
        case ">"_op:
        case ">="_op:
        case "=="_op:
        case "!="_op: {
            // Output type is i1 (bool)
            // Should get common type of operands and do signed comparison for integers
            auto lhs = getOp(instr.Operands[0]);
            auto rhs = getOp(instr.Operands[1]);
            auto commonTy = commonType(lhs, rhs);
            lhs = cast(lhs, commonTy);
            rhs = cast(rhs, commonTy);
            if (commonTy->isFloatingPointTy()) {
                auto it = fcmpMap.find(opcode);
                if (it == fcmpMap.end()) throw std::runtime_error("unsupported fcmp opcode");
                return storeTmp(cmpInsr(it->second, lhs, rhs));
            } else if (commonTy->isIntegerTy()) {
                // todo: unsigned?
                auto it = icmpMap.find(opcode);
                if (it == icmpMap.end()) throw std::runtime_error("unsupported icmp opcode");
                return storeTmp(cmpInsr(it->second, lhs, rhs));
            } else {
                throw std::runtime_error("unsupported type for comparison");
            }
        }
        case "neg"_op: {
            auto v = getOp(instr.Operands[0]);
            if (outputType->isFloatingPointTy()) {
                return storeTmp(irb->CreateFNeg(cast(v, outputType), "fnegtmp"));
            } else if (outputType->isIntegerTy()) {
                return storeTmp(irb->CreateNeg(cast(v, outputType), "inegtmp"));
            }
        }
        case "load"_op: {
            if (instr.OperandCount != 1 || instr.Dest.Idx < 0) throw std::runtime_error("load needs 1 operand and a dest");
            if (instr.Operands[0].Type == TOperand::EType::Slot) {
                auto idx = instr.Operands[0].Slot.Idx;
                auto g = EnsureSlotGlobal(idx, module);
                auto val = irb->CreateLoad(outputType, g, "loadtmp");
                return storeTmp(val);
            } else if (instr.Operands[0].Type == TOperand::EType::Local) {
                auto lidx = instr.Operands[0].Local.Idx;
                if (lidx < 0 || lidx >= CurFun->Allocas.size()) throw std::runtime_error("invalid local index");
                auto ptr = CurFun->Allocas[lidx];
                auto val = irb->CreateLoad(ptr->getAllocatedType(), ptr, "loadtmp");
                return storeTmp(cast(val, outputType));
            } else {
                throw std::runtime_error("load operand must be slot or local");
            }
            return nullptr;
        }
        case "stre"_op: {
            if (instr.OperandCount != 2)  throw std::runtime_error("store needs 2 operands");
            if (instr.Operands[0].Type == TOperand::EType::Slot) {
                auto idx = instr.Operands[0].Slot.Idx;
                auto g = EnsureSlotGlobal(idx, module);
                auto value = getOp(instr.Operands[1]);
                irb->CreateStore(value, g);
            } else if (instr.Operands[0].Type == TOperand::EType::Local) {
                auto lidx = instr.Operands[0].Local.Idx;
                if (lidx < 0 || lidx >= CurFun->Allocas.size()) throw std::runtime_error("invalid local index");
                auto ptr = CurFun->Allocas[lidx];
                auto value = getOp(instr.Operands[1]);
                irb->CreateStore(cast(value, ptr->getAllocatedType()), ptr);
            } else {
                throw std::runtime_error("store first operand must be slot or local");
            }
            return nullptr;
        }
        case "ret"_op: {
            if (instr.OperandCount > 0) {
                auto val = getOp(instr.Operands[0]);
                irb->CreateRet(cast(val, CurFun->LFun->getFunctionType()->getReturnType()));
            } else {
                irb->CreateRetVoid();
            }
            return nullptr;
        }
        case "i2f"_op:
        case "cmov"_op:
        case "mov"_op: {
            auto val = getOp(instr.Operands[0]);
            return storeTmp(cast(val, outputType));
        }
        case "arg"_op: {
            if (instr.OperandCount != 1) throw std::runtime_error("arg needs 1 operand");
            auto v = getOp(instr.Operands[0]);
            CurFun->PendingArgs.push_back(v);
            return nullptr;
        }
        case "call"_op: {
            // IR: call has optional Dest (required only for non-void) and one operand: Imm(symId)
            if (instr.OperandCount < 1) throw std::runtime_error("call needs callee operand");
            if (instr.Operands[0].Type != TOperand::EType::Imm) throw std::runtime_error("call callee must be Imm(symId)");
            const int calleeSymId = static_cast<int>(instr.Operands[0].Imm.Value);
            auto it = SymIdToLFun.find(calleeSymId);
            if (it != SymIdToLFun.end()) {
                // internal function
                auto* callee = it->second;
                auto* retTy = callee->getFunctionType()->getReturnType();
                // Marshal arguments collected by 'arg'
                auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
                std::vector<llvm::Value*> args;
                for (int i = 0; i < CurFun->PendingArgs.size(); ++i) {
                    auto* paramTy = callee->getFunctionType()->getParamType(i);
                    args.push_back(cast(CurFun->PendingArgs[i], paramTy));
                }
                CurFun->PendingArgs.clear();
                if (retTy->isVoidTy()) {
                    // Void call: emit and produce no tmp
                    (void)irb->CreateCall(callee, args);
                    return nullptr;
                } else {
                    if (instr.Dest.Idx < 0) throw std::runtime_error("call needs a destination tmp");
                    auto call = irb->CreateCall(callee, args, "calltmp");
                    return storeTmp(call);
                }
            }
            auto jt = module.SymIdToExtFuncIdx.find(calleeSymId);
            if (jt != module.SymIdToExtFuncIdx.end()) {
                // external function
                auto& extFun = module.ExternalFunctions[jt->second];

                auto* i64Ty = llvm::Type::getInt64Ty(ctx);
                auto* fty   = llvm::FunctionType::get(i64Ty, /*isVarArg=*/false);

                // materialize pointer constant and cast to i64()*
                //auto addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(extFun.Addr));
                //auto* addrConst = llvm::ConstantInt::get(i64Ty, addr);
                //auto* calleePtr = llvm::ConstantExpr::getIntToPtr(addrConst, fty->getPointerTo());
                //auto* call = irb->CreateCall(fty, calleePtr, {});
                llvm::FunctionCallee callee = LModule->getOrInsertFunction(extFun.MangledName, fty);
                auto* call = irb->CreateCall(callee, {});
                return storeTmp(call);
            } else {
                throw std::runtime_error("call target function not found");
            }
        }
        case "jmp"_op: {
            if (instr.OperandCount != 1) throw std::runtime_error("jmp needs 1 operand");
            if (!CurFun) throw std::runtime_error("jmp not in function");
            if (instr.Operands[0].Type != TOperand::EType::Label) throw std::runtime_error("jmp operand must be label");
            int64_t lab = instr.Operands[0].Label.Idx;
            auto it = CurFun->LabelToBB.find(lab);
            if (it == CurFun->LabelToBB.end()) throw std::runtime_error("jmp target label not found");
            irb->CreateBr(it->second);
            return nullptr;
        }
        case "cmp"_op: {
            if (instr.OperandCount != 3) throw std::runtime_error("cmp needs 3 operands");
            auto condV = getOp(instr.Operands[0]);
            if (instr.Operands[1].Type != TOperand::EType::Label || instr.Operands[2].Type != TOperand::EType::Label) {
                throw std::runtime_error("cmp branch targets must be labels");
            }
            int64_t tLab = instr.Operands[1].Label.Idx;
            int64_t fLab = instr.Operands[2].Label.Idx;
            auto itT = CurFun->LabelToBB.find(tLab);
            auto itF = CurFun->LabelToBB.find(fLab);
            if (itT == CurFun->LabelToBB.end() || itF == CurFun->LabelToBB.end()) throw std::runtime_error("cmp branch target not found");
            llvm::Value* cmpNZ = nullptr;
            if (condV->getType()->isFloatingPointTy()) {
                auto zero = llvm::ConstantFP::get(condV->getType(), 0.0);
                cmpNZ = irb->CreateFCmpUNE(condV, zero, "cmptmp");
            } else {
                auto zero = llvm::ConstantInt::get(condV->getType(), 0, true);
                cmpNZ = irb->CreateICmpNE(condV, zero, "cmptmp");
            }
            if (itT->second == itF->second) {
                irb->CreateBr(itT->second);
            } else {
                irb->CreateCondBr(cmpNZ, itT->second, itF->second);
            }
            return nullptr;
        }
        case "phi2"_op: {
            // phi2 falseVal trueVal  (ordering from IR lowering). Use last cmp condition and a select.
            if (instr.Dest.Idx < 0) throw std::runtime_error("phi2 needs a dest");
            if (instr.OperandCount != 4) throw std::runtime_error("phi2 needs 4 operands");
            auto falseV = getOp(instr.Operands[0]);
            int64_t fLab = instr.Operands[1].Label.Idx;
            auto trueV  = getOp(instr.Operands[2]);
            int64_t tLab = instr.Operands[3].Label.Idx;
            auto phi = irb->CreatePHI(outputType, 2);

            auto itT = CurFun->LabelToBB.find(tLab);
            auto itF = CurFun->LabelToBB.find(fLab);
            if (itT == CurFun->LabelToBB.end() || itF == CurFun->LabelToBB.end()) {
                throw std::runtime_error("phi branch source not found");
            }

            phi->addIncoming(trueV, itT->second);
            phi->addIncoming(falseV, itF->second);
            return storeTmp(phi);
        }
    };
    std::cerr << "LLVMCodeGen: unhandled instruction: '" << instr.Op.ToString() << "'\n";
    return nullptr;
}

void TLLVMCodeGen::CreateTargetMachine() {
    std::string triple = LModule->getTargetTriple();
    if (triple.empty()) {
        triple = llvm::sys::getDefaultTargetTriple();
        LModule->setTargetTriple(triple);
    }

    std::string errStr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, errStr);
    if (!target) {
        throw std::runtime_error(std::string("lookupTarget failed: ") + errStr);
    }
    llvm::TargetOptions opt;
    auto RM = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    TM.reset(
        target->createTargetMachine(triple, "generic", "", opt, RM)
    );

    if (!TM) {
        throw std::runtime_error("createTargetMachine failed");
    }

    LModule->setDataLayout(TM->createDataLayout());
}

void TLLVMCodeGen::Optimize(int optLevel) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(TM.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    auto OL = llvm::OptimizationLevel::O3; // можно O2 для более быстрой компиляции
    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OL);
    MPM.run(*LModule, MAM);
}

void TLLVMCodeGen::PrintFunction(int symId, std::ostream& os) const {
    auto it = SymIdToLFun.find(symId);
    if (it == SymIdToLFun.end()) {
        os << "[PrintFunction] function not found by SymId " << symId << "\n";
        return;
    }
    auto* f = it->second;
    if (!f) {
        os << "[PrintFunction] function pointer is null for SymId " << symId << "\n";
        return;
    }
    std::string str;
    llvm::raw_string_ostream rso(str);
    f->print(rso);
    rso.flush();
    os << str << "\n";
}

void TLLVMModuleArtifacts::PrintModule(std::ostream& os) const {
    if (!Module) {
        os << "[PrintModule] module is null\n";
        return;
    }
    std::string str;
    llvm::raw_string_ostream rso(str);
    Module->print(rso, nullptr);
    rso.flush();
    os << str << "\n";
}

} // namespace NQumir::NCodeGen
