#include <qumir/ir/eval.h>
#include <qumir/ir/passes/transforms/pipeline.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <sstream>

using namespace NQumir;
using namespace NQumir::NIR;
using namespace NQumir::NIR::NLiterals;

namespace {

struct TVMTest {
    TModule Module;
    TBuilder Builder{Module};
    TVMCompiler Compiler{Module};
    std::ostringstream Out;
    std::istringstream In;
    TInterpreter Interpreter{Module, Compiler, Out, In};

    TTmp Emit(TOp op, std::initializer_list<TOperand> operands, int typeId) {
        auto tmp = Builder.Emit1(op, operands);
        Builder.SetType(tmp, typeId);
        return tmp;
    }
};

TEST(VMRegisterFile, AlignedTypedAccessAndBufferGrowth) {
    TRegisterFile registers;
    registers.Reset(48, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(registers.Data()) % 16, 0);
    const __int128_t wide = static_cast<__int128_t>(1) << 100;
    registers.Get<int64_t>(0) = 17;
    registers.Get<__int128_t>(16) = wide;
    registers.Get<int64_t>(32) = std::bit_cast<int64_t>(3.25);
    const auto& values = registers;
    EXPECT_EQ(values.Get<int64_t>(0), 17);
    EXPECT_EQ(values.Get<__int128_t>(16), wide);
    EXPECT_EQ(std::bit_cast<double>(values.Get<int64_t>(32)), 3.25);

    struct alignas(64) TWideValue {
        std::array<uint64_t, 8> Lanes;
    };
    registers.Reset(128, alignof(TWideValue));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(registers.Data()) % 64, 0);
    EXPECT_EQ(registers.Size(), 128);
    registers.Get<TWideValue>(64) = {{1, 2, 3, 4, 5, 6, 7, 8}};
    EXPECT_EQ(values.Get<TWideValue>(64).Lanes.back(), 8);
    registers.Reset(8, 8);
    EXPECT_EQ(values.Get<int64_t>(0), 0);
}

TEST(VMRegisterFile, SparseIRIdsBecomeAlignedByteOffsets) {
    TVMTest vm;
    const int i64 = vm.Module.Types.I(EKind::I64);
    const int i128 = vm.Module.Types.I(EKind::I128);
    const int f64 = vm.Module.Types.I(EKind::F64);
    vm.Builder.NewFunction("sparse", {}, 0);
    vm.Module.Functions[0].NextTmpIdx = 100;
    const auto narrow = vm.Emit("mov"_op, {TImm{7, i64}}, i64);
    const auto wide = vm.Emit("mov"_op, {TImm{13, i128}}, i128);
    const auto floating = vm.Emit("mov"_op, {TImm{std::bit_cast<int64_t>(2.5), f64}}, f64);
    const auto sum = vm.Emit('+'_op, {narrow, TImm{4, i64}}, i64);
    vm.Builder.Emit0("ret"_op, {sum});
    vm.Builder.SetReturnType(i64);
    vm.Builder.SetType(TTmp{10000}, i128);
    vm.Builder.SetType(TTmp{20000}, vm.Module.Types.Struct({i64, i64, i64}));

    auto& function = vm.Module.Functions[0];
    const auto originalTypes = function.TmpTypes;
    const auto originalNextTmp = function.NextTmpIdx;
    const auto& code = vm.Compiler.Compile(function);
    EXPECT_EQ(code.RegisterFileSize, 48);
    EXPECT_EQ(code.RegisterFileAlignment, 16);
    EXPECT_EQ(code.NumLocals, 0);
    EXPECT_EQ(code.TmpByteOffsets.size(), 4);
    EXPECT_EQ(code.TmpByteOffsets.at(narrow.Idx), 0);
    EXPECT_EQ(code.TmpByteOffsets.at(wide.Idx), 16);
    EXPECT_EQ(code.TmpByteOffsets.at(floating.Idx), 32);
    EXPECT_EQ(code.TmpByteOffsets.at(sum.Idx), 40);
    EXPECT_EQ(code.VMCode[3].Operands[1].Tmp.Idx, code.TmpByteOffsets.at(narrow.Idx));
    EXPECT_EQ(function.Blocks[0].Instrs[0].Dest.Idx, narrow.Idx);
    EXPECT_EQ(function.Blocks[0].Instrs[3].Operands[0].Tmp.Idx, narrow.Idx);
    EXPECT_EQ(function.TmpTypes, originalTypes);
    EXPECT_EQ(function.NextTmpIdx, originalNextTmp);
    EXPECT_EQ(vm.Interpreter.EvalRaw(function, {}, {}), 11);
}

TEST(VMRegisterFile, IRPipelinePreservesTemporaryIds) {
    TVMTest vm;
    const int i64 = vm.Module.Types.I(EKind::I64);
    vm.Builder.NewFunction("sparse", {}, 0);
    vm.Module.Functions[0].NextTmpIdx = 100;
    const auto tmp = vm.Emit("call"_op, {TImm{123}}, i64);
    vm.Builder.Emit0("ret"_op, {tmp});
    vm.Builder.SetReturnType(i64);
    auto& function = vm.Module.Functions[0];
    NPasses::Pipeline(function, vm.Module);
    EXPECT_EQ(function.Blocks[0].Instrs[0].Dest.Idx, tmp.Idx);
    EXPECT_EQ(function.Blocks[0].Instrs[1].Operands[0].Tmp.Idx, tmp.Idx);
    EXPECT_EQ(function.GetTmpType(tmp.Idx), i64);
    EXPECT_EQ(function.NextTmpIdx, 101);
}

TEST(VMRegisterFile, CallsPreserveMixedRegistersAcrossBufferGrowth) {
    TVMTest vm;
    const int i64 = vm.Module.Types.I(EKind::I64);
    const int i128 = vm.Module.Types.I(EKind::I128);
    const int f64 = vm.Module.Types.I(EKind::F64);
    const int callee = vm.Builder.NewFunction("callee", {TLocal{0}, TLocal{1}}, 1);
    vm.Builder.SetType(TLocal{0}, i128, {});
    vm.Builder.SetType(TLocal{1}, f64, {});
    const auto arg = vm.Emit("load"_op, {TLocal{0}}, i128);
    auto floatingArg = vm.Emit("load"_op, {TLocal{1}}, f64);
    for (int i = 0; i < 64; ++i) {
        floatingArg = vm.Emit('+'_op, {floatingArg, TImm{std::bit_cast<int64_t>(0.125), f64}}, f64);
    }
    const auto incremented = vm.Emit('+'_op, {arg, TImm{1, i128}}, i128);
    vm.Builder.Emit0("ret"_op, {incremented});
    vm.Builder.SetReturnType(i128);

    const int caller = vm.Builder.NewFunction("caller", {}, 2);
    const auto narrow = vm.Emit("mov"_op, {TImm{7, i64}}, i64);
    const auto one = vm.Emit("mov"_op, {TImm{1, i128}}, i128);
    const auto wide = vm.Emit("<<"_op, {one, TImm{80, i128}}, i128);
    const auto floating = vm.Emit("mov"_op, {TImm{std::bit_cast<int64_t>(3.25), f64}}, f64);
    vm.Builder.Emit0("arg"_op, {wide});
    vm.Builder.Emit0("arg"_op, {floating});
    const auto returned = vm.Emit("call"_op, {TImm{1}}, i128);
    const auto originalHigh = vm.Emit(">>"_op, {wide, TImm{64, i128}}, i128);
    const auto returnedHigh = vm.Emit(">>"_op, {returned, TImm{64, i128}}, i128);
    const auto low = vm.Emit("mov"_op, {returned}, i64);
    const auto high = vm.Emit("mov"_op, {returnedHigh}, i64);
    const auto original = vm.Emit("mov"_op, {originalHigh}, i64);
    const auto fraction = vm.Emit('+'_op, {floating, TImm{std::bit_cast<int64_t>(1.5), f64}}, f64);
    const auto integral = vm.Emit("f2i"_op, {fraction}, i64);
    auto sum = vm.Emit('+'_op, {high, low}, i64);
    sum = vm.Emit('+'_op, {sum, original}, i64);
    sum = vm.Emit('+'_op, {sum, integral}, i64);
    sum = vm.Emit('+'_op, {sum, narrow}, i64);
    vm.Builder.Emit0("ret"_op, {sum});
    vm.Builder.SetReturnType(i64);

    EXPECT_EQ(vm.Interpreter.EvalRaw(vm.Module.Functions[caller], {}, {}), 131084);
    EXPECT_GT(vm.Module.Functions[callee].Exec->RegisterFileSize, vm.Module.Functions[caller].Exec->RegisterFileSize);
    EXPECT_EQ(vm.Interpreter.GetRuntime().Regs.Size(), vm.Module.Functions[caller].Exec->RegisterFileSize);
}

TEST(VMRegisterFile, CallsWithoutRegistersCanDiscardAValue) {
    TVMTest vm;
    const int i64 = vm.Module.Types.I(EKind::I64);
    vm.Builder.NewFunction("callee", {}, 1);
    vm.Builder.Emit0("ret"_op, {TImm{42, i64}});
    vm.Builder.SetReturnType(i64);
    const int caller = vm.Builder.NewFunction("caller", {}, 2);
    vm.Builder.Emit0("call"_op, {TImm{1}});
    vm.Builder.Emit0("ret"_op, {TImm{7, i64}});
    vm.Builder.SetReturnType(i64);
    EXPECT_EQ(vm.Interpreter.EvalRaw(vm.Module.Functions[caller], {}, {}), 7);
    EXPECT_EQ(vm.Interpreter.GetRuntime().Regs.Size(), 0);
}

TEST(VMRegisterFile, StructReturnUsesMetadataAtRegisterByteOffset) {
    TVMTest vm;
    const int i64 = vm.Module.Types.I(EKind::I64);
    const int i128 = vm.Module.Types.I(EKind::I128);
    const int structType = vm.Module.Types.Struct({i64, i64});
    const int pointer = vm.Module.Types.Ptr(i64);
    vm.Builder.NewFunction("callee", {}, 1);
    vm.Builder.SetType(TLocal{0}, structType, {});
    const auto address = vm.Emit("lea"_op, {TLocal{0}}, pointer);
    vm.Builder.Emit0("ste"_op, {address, TImm{19, i64}});
    const auto value = vm.Emit("load"_op, {TLocal{0}}, structType);
    vm.Builder.Emit0("ret"_op, {value});
    vm.Builder.SetReturnType(structType);

    const int caller = vm.Builder.NewFunction("caller", {}, 2);
    vm.Emit("mov"_op, {TImm{1, i128}}, i128);
    const auto returned = vm.Emit("call"_op, {TImm{1}}, structType);
    const auto firstField = vm.Emit("lde"_op, {returned}, i64);
    vm.Builder.Emit0("ret"_op, {firstField});
    vm.Builder.SetReturnType(i64);
    auto& function = vm.Module.Functions[caller];
    EXPECT_EQ(vm.Interpreter.EvalRaw(function, {}, {}), 19);
    const int byteOffset = function.Exec->TmpByteOffsets.at(returned.Idx);
    EXPECT_NE(byteOffset, returned.Idx);
    EXPECT_EQ(function.Exec->StructRegisters.at(byteOffset).Size, 16);
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
