#include <gtest/gtest.h>

#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/codegen/llvm/llvm_runner.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/type.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <type_traits>

using namespace NQumir;
using namespace NQumir::NIR;
using namespace NQumir::NIR::NLiterals;

namespace {

// A module with two trivial void functions "A" and "B".
void BuildTwoFuns(NIR::TModule& module) {
    NIR::TBuilder b(module);
    int voidTy = module.Types.I(EKind::Void);
    auto mk = [&](std::string name, int symId) {
        b.NewFunction(std::move(name), {}, symId); // creates the entry block
        b.SetReturnType(voidTy);
        b.Emit0("ret"_op, {});
    };
    mk("A", 1);
    mk("B", 2);
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::vector<std::string> Emit(const NCodeGen::TLLVMCodeGenOptions& opts) {
    NIR::TModule module;
    BuildTwoFuns(module);
    NCodeGen::TLLVMCodeGen cg(opts);
    auto art = cg.Emit(module);
    return art->GetDefinedFunctionNames();
}

void BuildVectorKernel(TModule& module, EKind kind, int count, TOp opcode, int broadcast) {
    TBuilder builder(module);
    const int elementType = module.Types.I(kind);
    const int vectorType = module.Types.Vec(elementType, count);
    const int pointerType = module.Types.Ptr(vectorType);
    const int i64 = module.Types.I(EKind::I64);
    builder.NewFunction("kernel", {TLocal{0}, TLocal{1}, TLocal{2}, TLocal{3}}, 1);
    builder.SetReturnType(module.Types.I(EKind::Void));
    for (int i = 0; i < 3; ++i) {
        builder.SetType(TLocal{i}, pointerType, {});
    }
    builder.SetType(TLocal{3}, elementType, {});
    builder.SetType(TLocal{4}, vectorType, {});
    auto emit = [&](TOp op, std::initializer_list<TOperand> operands, int type) {
        auto tmp = builder.Emit1(op, operands);
        builder.SetType(tmp, type);
        return tmp;
    };
    auto output = emit("load"_op, {TLocal{0}}, pointerType);
    auto leftPtr = emit("load"_op, {TLocal{1}}, pointerType);
    auto rightPtr = emit("load"_op, {TLocal{2}}, pointerType);
    auto left = emit("lde"_op, {leftPtr}, vectorType);
    auto right = emit("lde"_op, {rightPtr}, vectorType);
    auto scalar = emit("load"_op, {TLocal{3}}, elementType);
    auto result = emit(opcode, {broadcast == 1 ? scalar : left, broadcast == 2 ? scalar : right}, vectorType);
    builder.Emit0("stre"_op, {TLocal{4}, result});
    auto reloaded = emit("load"_op, {TLocal{4}}, vectorType);
    auto copied = emit("mov"_op, {reloaded}, vectorType);
    builder.Emit0("ste"_op, {output, copied});
    auto tail = emit("+"_op, {output, TImm{module.Types.SizeInBytes(vectorType), i64}}, pointerType);
    auto zero = emit("mov"_op, {TImm{0, vectorType}}, vectorType);
    builder.Emit0("ste"_op, {tail, zero});
    builder.Emit0("ret"_op, {});
}

template<typename T>
void CheckVectorKernels(EKind kind, int count = 4) {
    SCOPED_TRACE(static_cast<int>(kind));
    std::vector<T> left(count);
    std::vector<T> right(count);
    for (int i = 0; i < count; ++i) {
        left[i] = static_cast<T>(i * 7 + 1);
        right[i] = static_cast<T>(i * 3 + 2);
    }
    if constexpr (std::is_integral_v<T>) {
        left[0] = std::numeric_limits<T>::max();
        left[1] = std::numeric_limits<T>::min();
        right[1] = static_cast<T>(-1);
    } else {
        left[0] = -static_cast<T>(0);
        right[0] = static_cast<T>(0);
        left[1] = std::numeric_limits<T>::denorm_min();
        right[1] = static_cast<T>(-1);
        left[2] = std::numeric_limits<T>::infinity();
        right[2] = -std::numeric_limits<T>::infinity();
        left[3] = std::numeric_limits<T>::quiet_NaN();
    }
    for (int optLevel : {0, 3}) {
        for (auto opcode : {"+"_op, "-"_op, "*"_op}) {
            for (int broadcast : {0, 1, 2}) {
                SCOPED_TRACE(optLevel);
                SCOPED_TRACE(opcode.ToString());
                SCOPED_TRACE(broadcast);
                TModule module;
                BuildVectorKernel(module, kind, count, opcode, broadcast);
                NCodeGen::TLLVMCodeGen codegen;
                NCodeGen::TLlvmRunner runner;
                std::string error;
                auto* address = runner.Lookup(codegen.Emit(module, optLevel), "kernel", &error);
                ASSERT_NE(address, nullptr) << error;
                using TKernel = void (*)(T*, const T*, const T*, T);
                std::vector<T> output(count * 2, static_cast<T>(42));
                reinterpret_cast<TKernel>(address)(output.data(), left.data(), right.data(), static_cast<T>(5));
                using TCalc = std::conditional_t<std::is_floating_point_v<T>, T, uint64_t>;
                for (int i = 0; i < count; ++i) {
                    const TCalc a = (broadcast == 1)
                        ? 5
                        : static_cast<TCalc>(left[i]);
                    const TCalc b = (broadcast == 2)
                        ? 5
                        : static_cast<TCalc>(right[i]);
                    TCalc expected;
                    switch (opcode) {
                        case "+"_op: expected = a + b; break;
                        case "-"_op: expected = a - b; break;
                        case "*"_op: expected = a * b; break;
                        default: FAIL() << "Unexpected opcode";
                    }
                    if constexpr (std::is_floating_point_v<T>) {
                        if (std::isnan(expected)) {
                            EXPECT_TRUE(std::isnan(output[i]));
                        } else {
                            EXPECT_EQ(output[i], expected);
                            if (expected == 0) {
                                EXPECT_EQ(std::signbit(output[i]), std::signbit(expected));
                            }
                        }
                    } else {
                        EXPECT_EQ(output[i], static_cast<T>(expected));
                    }
                    EXPECT_EQ(output[count + i], 0);
                }
            }
        }
    }
}

} // namespace

TEST(CodegenPartition, DefaultDefinesEverything) {
    auto defs = Emit({});
    EXPECT_TRUE(Has(defs, "A"));
    EXPECT_TRUE(Has(defs, "B"));
}

TEST(CodegenPartition, RestrictToDefinitionsDefinesOnlyListed) {
    std::unordered_set<std::string> only{"A"};
    NCodeGen::TLLVMCodeGenOptions opts;
    opts.RestrictToDefinitions = &only;
    auto defs = Emit(opts);
    EXPECT_TRUE(Has(defs, "A"));
    EXPECT_FALSE(Has(defs, "B")); // B is an external declaration
}

TEST(CodegenPartition, EmitAsExternalDeclaresListed) {
    std::unordered_set<std::string> ext{"A"};
    NCodeGen::TLLVMCodeGenOptions opts;
    opts.EmitAsExternal = &ext;
    auto defs = Emit(opts);
    EXPECT_FALSE(Has(defs, "A")); // A is an external declaration
    EXPECT_TRUE(Has(defs, "B"));
}

TEST(CodegenPartition, ImportedBodyIsAvailableButDoesNotOwnAnExport) {
    NIR::TModule module;
    BuildTwoFuns(module);
    std::unordered_set<std::string> only{"A"};
    std::unordered_set<std::string> imports{"B"};
    NCodeGen::TLLVMCodeGen codegen({
        .RestrictToDefinitions = &only,
        .InlineDefinitions = &imports,
    });
    auto artifacts = codegen.Emit(module, 0);
    EXPECT_EQ(artifacts->GetDefinedFunctionNames(), (std::vector<std::string>{"A"}));
    std::ostringstream ir;
    artifacts->PrintModule(ir);
    EXPECT_NE(ir.str().find("define available_externally void @B("), std::string::npos);
}

TEST(LLVMVector, DynamicArithmeticCopiesZerosAndBroadcasts) {
    CheckVectorKernels<int8_t>(EKind::I8);
    CheckVectorKernels<uint8_t>(EKind::U8);
    CheckVectorKernels<int16_t>(EKind::I16);
    CheckVectorKernels<uint16_t>(EKind::U16);
    CheckVectorKernels<int32_t>(EKind::I32);
    CheckVectorKernels<uint32_t>(EKind::U32);
    CheckVectorKernels<int64_t>(EKind::I64);
    CheckVectorKernels<uint64_t>(EKind::U64, 32);
    CheckVectorKernels<float>(EKind::F32);
    CheckVectorKernels<double>(EKind::F64);
}

TEST(LLVMVector, NativeArithmeticUsesSIMD) {
#if defined(__aarch64__) || defined(__x86_64__)
    for (auto kind : {EKind::I32, EKind::F64}) {
        for (auto opcode : {"+"_op, "-"_op, "*"_op}) {
            std::string mnemonic;
#if defined(__aarch64__)
            switch (opcode) {
                case "+"_op: mnemonic = "add"; break;
                case "-"_op: mnemonic = "sub"; break;
                case "*"_op: mnemonic = "mul"; break;
                default: FAIL() << "Unexpected opcode";
            }
            if (kind == EKind::F64) {
                mnemonic = "f" + mnemonic;
            }
            const std::string lanes = (kind == EKind::F64)
                ? "2d"
                : "4s";
            const std::string pattern = "\\b" + mnemonic + "(\\." + lanes + "\\s+v|\\s+v[0-9]+\\." + lanes + ")";
#else
            switch (opcode) {
                case "+"_op:
                    mnemonic = kind == EKind::F64
                        ? "addpd"
                        : "paddd";
                    break;
                case "-"_op:
                    mnemonic = kind == EKind::F64
                        ? "subpd"
                        : "psubd";
                    break;
                case "*"_op:
                    mnemonic = kind == EKind::F64
                        ? "mulpd"
                        : "pmul(ld|udq)";
                    break;
                default: FAIL() << "Unexpected opcode";
            }
            const std::string pattern = "\\bv?" + mnemonic + "\\s";
#endif
            for (int broadcast : {0, 1, 2}) {
                SCOPED_TRACE(mnemonic);
                SCOPED_TRACE(broadcast);
                TModule module;
                BuildVectorKernel(module, kind, kind == EKind::F64 ? 2 : 4, opcode, broadcast);
                NCodeGen::TLLVMCodeGen codegen;
                auto artifacts = codegen.Emit(module, 3);
                std::ostringstream assembly;
                artifacts->Generate(assembly, true, false);
                EXPECT_TRUE(std::regex_search(assembly.str(), std::regex(pattern))) << assembly.str();
            }
        }
    }
#else
    GTEST_SKIP() << "SIMD mnemonic checks cover AArch64 and x86-64";
#endif
}

TEST(LLVMVector, BooleanIndexUsesByteLanes) {
    for (int optLevel : {0, 3}) {
        for (int count : {2, 4, 8, 16, 32}) {
            SCOPED_TRACE(optLevel);
            SCOPED_TRACE(count);
            TModule module;
            TBuilder builder(module);
            const int boolean = module.Types.I(EKind::I1);
            const int vector = module.Types.Vec(boolean, count);
            const int pointer = module.Types.Ptr(vector);
            const int i64 = module.Types.I(EKind::I64);
            builder.NewFunction("index", {TLocal{0}, TLocal{1}}, 1);
            builder.SetReturnType(boolean);
            builder.SetType(TLocal{0}, pointer, {});
            builder.SetType(TLocal{1}, i64, {});
            auto address = builder.Emit1("load"_op, {TLocal{0}});
            builder.SetType(address, pointer);
            auto value = builder.Emit1("lde"_op, {address});
            builder.SetType(value, vector);
            auto index = builder.Emit1("load"_op, {TLocal{1}});
            builder.SetType(index, i64);
            auto element = builder.Emit1("index"_op, {value, index});
            builder.SetType(element, boolean);
            builder.Emit0("ret"_op, {element});
            NCodeGen::TLLVMCodeGen codegen;
            NCodeGen::TLlvmRunner runner;
            std::string error;
            auto* entry = runner.Lookup(codegen.Emit(module, optLevel), "index", &error);
            ASSERT_NE(entry, nullptr) << error;
            using TIndex = bool (*)(const uint8_t*, int64_t);
            std::array<uint8_t, 32> lanes{};
            for (int i = 0; i < count; ++i) {
                lanes[i] = static_cast<uint8_t>(i % 3 == 1);
            }
            for (int i = 0; i < count; ++i) {
                EXPECT_EQ(reinterpret_cast<TIndex>(entry)(lanes.data(), i), lanes[i] != 0);
            }
        }
    }
}

TEST(LLVMVector, InvalidIndexRaisesRuntimeError) {
    for (int optLevel : {0, 3}) {
        for (int64_t index : std::array<int64_t, 3>{-1, 2, std::numeric_limits<int64_t>::max()}) {
            SCOPED_TRACE(optLevel);
            SCOPED_TRACE(index);
            TModule module;
            TBuilder builder(module);
            const int i64 = module.Types.I(EKind::I64);
            const int vector = module.Types.Vec(i64, 2);
            builder.NewFunction("bounds", {}, 1);
            builder.SetReturnType(i64);
            auto value = builder.Emit1("mov"_op, {TImm{0, vector}});
            builder.SetType(value, vector);
            auto element = builder.Emit1("index"_op, {value, TImm{index, i64}});
            builder.SetType(element, i64);
            builder.Emit0("ret"_op, {element});
            NCodeGen::TLLVMCodeGen codegen;
            NCodeGen::TLlvmRunner runner;
            EXPECT_THROW(runner.Run(codegen.Emit(module, optLevel), "bounds"), std::runtime_error);
        }
    }
}

int main(int argc, char** argv) {
    NQumir::NCodeGen::TLLVMInitializer llvmInit;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
