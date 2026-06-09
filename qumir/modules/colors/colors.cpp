#include "colors.h"

#include <qumir/runtime/colors.h>

#include <array>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

ColorsModule::ColorsModule() {
    auto integerType  = std::make_shared<NAst::TIntegerType>();
    auto colorStorageType = std::make_shared<NAst::TIntegerType>(NAst::TIntegerType::EKind::U32);
    auto floatType    = std::make_shared<NAst::TFloatType>();
    auto boolType     = std::make_shared<NAst::TBoolType>();
    auto voidType     = std::make_shared<NAst::TVoidType>();
    auto stringType   = std::make_shared<NAst::TStringType>();
    auto intArrayType = std::make_shared<NAst::TArrayType>(integerType, 1);

    auto colorType = std::make_shared<NAst::TNamedType>("цвет", colorStorageType);

    auto colorConst = [colorType](int64_t value) {
        return [colorType, value](std::vector<NAst::TExprPtr>) -> NAst::TExprPtr {
            auto expr = std::make_shared<NAst::TNumberExpr>(TLocation{}, value);
            expr->Type = colorType;
            return expr;
        };
    };

    auto intLiteral = [integerType](TLocation loc, int64_t value) -> NAst::TExprPtr {
        auto expr = std::make_shared<NAst::TNumberExpr>(std::move(loc), value);
        expr->Type = integerType;
        return expr;
    };

    auto floatLiteral = [floatType](TLocation loc, double value) -> NAst::TExprPtr {
        auto expr = std::make_shared<NAst::TNumberExpr>(std::move(loc), value);
        expr->Type = floatType;
        return expr;
    };

    auto binary = [](const char* op, NAst::TExprPtr left, NAst::TExprPtr right, NAst::TTypePtr type) -> NAst::TExprPtr {
        auto loc = left->Location;
        auto expr = std::make_shared<NAst::TBinaryExpr>(std::move(loc), NAst::TOperator(op),
            std::move(left), std::move(right));
        expr->Type = std::move(type);
        return expr;
    };

    auto cast = [](NAst::TExprPtr value, NAst::TTypePtr type) -> NAst::TExprPtr {
        auto loc = value->Location;
        auto expr = std::make_shared<NAst::TCastExpr>(std::move(loc), std::move(value), std::move(type));
        return expr;
    };

    auto ident = [](TLocation loc, const std::string& name, NAst::TTypePtr type) -> NAst::TExprPtr {
        auto expr = std::make_shared<NAst::TIdentExpr>(std::move(loc), name);
        expr->Type = std::move(type);
        return expr;
    };

    auto ifExpr = [](NAst::TExprPtr cond, NAst::TExprPtr thenExpr, NAst::TExprPtr elseExpr, NAst::TTypePtr type) -> NAst::TExprPtr {
        auto loc = cond->Location;
        auto expr = std::make_shared<NAst::TIfExpr>(std::move(loc), std::move(cond), std::move(thenExpr), std::move(elseExpr));
        expr->Type = std::move(type);
        return expr;
    };

    auto colorPack = [integerType, colorType, intLiteral, binary](NAst::TExprPtr r, NAst::TExprPtr g,
                                                          NAst::TExprPtr b, NAst::TExprPtr a) -> NAst::TExprPtr {
        auto mask = [&](NAst::TExprPtr value) {
            auto loc = value->Location;
            return binary("&", std::move(value), intLiteral(loc, 255), integerType);
        };
        auto shift = [&](NAst::TExprPtr value, int64_t bits) {
            auto loc = value->Location;
            return binary("<<", mask(std::move(value)), intLiteral(loc, bits), integerType);
        };
        auto bor = [&](NAst::TExprPtr left, NAst::TExprPtr right, NAst::TTypePtr type) {
            return binary("|", std::move(left), std::move(right), std::move(type));
        };

        auto packed = bor(
            bor(
                bor(shift(std::move(a), 24), shift(std::move(r), 16), integerType),
                shift(std::move(g), 8),
                integerType),
            mask(std::move(b)),
            integerType);
        auto expr = std::make_shared<NAst::TCastExpr>(packed->Location, std::move(packed), colorType);
        expr->Type = colorType;
        return expr;
    };

    auto inlineCmyk = [integerType, colorPack, intLiteral, binary, cast](bool withAlpha) {
        return [integerType, colorPack, intLiteral, binary, cast, withAlpha](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
            auto loc = args[0]->Location;
            auto hundred = [&](TLocation l) { return intLiteral(l, 100); };
            auto roundedComponent = [&](const std::string& channelName) -> NAst::TExprPtr {
                auto channel = std::make_shared<NAst::TIdentExpr>(loc, channelName);
                channel->Type = integerType;
                auto k = std::make_shared<NAst::TIdentExpr>(loc, "$$k");
                k->Type = integerType;
                auto value = binary("*",
                    binary("*",
                        binary("-", hundred(loc), std::move(channel), integerType),
                        binary("-", hundred(loc), std::move(k), integerType),
                        integerType),
                    intLiteral(loc, 255),
                    integerType);
                return cast(
                    binary("+",
                        binary("/", std::move(value), intLiteral(loc, 10000), std::make_shared<NAst::TFloatType>()),
                        std::make_shared<NAst::TNumberExpr>(loc, 0.5),
                        std::make_shared<NAst::TFloatType>()),
                    integerType);
            };

            std::vector<NAst::TLetExpr::TBinding> bindings;
            bindings.push_back({ .Name = "$$c", .Value = args[0] });
            bindings.push_back({ .Name = "$$m", .Value = args[1] });
            bindings.push_back({ .Name = "$$y", .Value = args[2] });
            bindings.push_back({ .Name = "$$k", .Value = args[3] });
            if (withAlpha) {
                bindings.push_back({ .Name = "$$a", .Value = args[4] });
            }
            auto alpha = withAlpha
                ? std::make_shared<NAst::TIdentExpr>(loc, "$$a")
                : intLiteral(loc, 255);
            alpha->Type = integerType;
            auto body = colorPack(
                roundedComponent("$$c"),
                roundedComponent("$$m"),
                roundedComponent("$$y"),
                std::move(alpha));
            auto expr = std::make_shared<NAst::TLetExpr>(loc, std::move(bindings), std::move(body));
            expr->Type = colorPack(intLiteral(loc, 0), intLiteral(loc, 0), intLiteral(loc, 0), intLiteral(loc, 0))->Type;
            return expr;
        };
    };

    auto inlineHsl = [integerType, floatType, boolType, colorType, colorPack, intLiteral, floatLiteral, ident, binary, cast, ifExpr](bool withAlpha) {
        return [integerType, floatType, boolType, colorType, colorPack, intLiteral, floatLiteral, ident, binary, cast, ifExpr, withAlpha](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
            auto loc = args[0]->Location;
            auto f = [&](double value) { return floatLiteral(loc, value); };
            auto idf = [&](const std::string& name) { return ident(loc, name, floatType); };
            auto idi = [&](const std::string& name) { return ident(loc, name, integerType); };
            auto round255 = [&](NAst::TExprPtr value) -> NAst::TExprPtr {
                return cast(binary("+", binary("*", std::move(value), f(255.0), floatType), f(0.5), floatType), integerType);
            };
            auto makeLet = [&](std::vector<NAst::TLetExpr::TBinding> bindings, NAst::TExprPtr body, NAst::TTypePtr type) {
                auto expr = std::make_shared<NAst::TLetExpr>(loc, std::move(bindings), std::move(body));
                expr->Type = std::move(type);
                return expr;
            };
            auto hueToRgb = [&](NAst::TExprPtr tValue) -> NAst::TExprPtr {
                auto p = idf("$$p");
                auto q = idf("$$q");
                auto qMinusP = [&]() { return binary("-", idf("$$q"), idf("$$p"), floatType); };
                auto body = ifExpr(
                    binary("<", idf("$$t"), f(1.0 / 6.0), boolType),
                    binary("+", idf("$$p"), binary("*", binary("*", qMinusP(), f(6.0), floatType), idf("$$t"), floatType), floatType),
                    ifExpr(
                        binary("<", idf("$$t"), f(0.5), boolType),
                        std::move(q),
                        ifExpr(
                            binary("<", idf("$$t"), f(2.0 / 3.0), boolType),
                            binary("+", std::move(p), binary("*", binary("*", qMinusP(), binary("-", f(2.0 / 3.0), idf("$$t"), floatType), floatType), f(6.0), floatType), floatType),
                            idf("$$p"),
                            floatType),
	                        floatType),
	                    floatType);
                std::vector<NAst::TLetExpr::TBinding> normalizedBinding;
                normalizedBinding.push_back({
                    .Name = "$$t",
                    .Value = ifExpr(
                        binary("<", idf("$$t_raw"), f(0.0), boolType),
                        binary("+", idf("$$t_raw"), f(1.0), floatType),
                        ifExpr(
                            binary(">", idf("$$t_raw"), f(1.0), boolType),
                            binary("-", idf("$$t_raw"), f(1.0), floatType),
                            idf("$$t_raw"),
                            floatType),
                        floatType),
                });
                auto normalized = makeLet(std::move(normalizedBinding), std::move(body), floatType);
                std::vector<NAst::TLetExpr::TBinding> rawBinding;
                rawBinding.push_back({ .Name = "$$t_raw", .Value = std::move(tValue) });
                return makeLet(std::move(rawBinding), std::move(normalized), floatType);
            };

            std::vector<NAst::TLetExpr::TBinding> bindings;
            bindings.push_back({ .Name = "$$h", .Value = args[0] });
            bindings.push_back({ .Name = "$$s", .Value = args[1] });
            bindings.push_back({ .Name = "$$l", .Value = args[2] });
            if (withAlpha) {
                bindings.push_back({ .Name = "$$a", .Value = args[3] });
            }

            auto alpha = withAlpha ? idi("$$a") : intLiteral(loc, 255);
            auto gray = round255(idf("$$lf"));
            auto grayBody = colorPack(gray, round255(idf("$$lf")), round255(idf("$$lf")), alpha);

            std::vector<NAst::TLetExpr::TBinding> pBinding;
            pBinding.push_back({
                .Name = "$$p",
                .Value = binary("-", binary("*", f(2.0), idf("$$lf"), floatType), idf("$$q"), floatType),
            });
            auto chromaBody = colorPack(
                round255(hueToRgb(binary("+", idf("$$hf"), f(1.0 / 3.0), floatType))),
                round255(hueToRgb(idf("$$hf"))),
                round255(hueToRgb(binary("-", idf("$$hf"), f(1.0 / 3.0), floatType))),
                withAlpha ? idi("$$a") : intLiteral(loc, 255));
            auto pLet = makeLet(std::move(pBinding), std::move(chromaBody), colorType);

            std::vector<NAst::TLetExpr::TBinding> qBinding;
            qBinding.push_back({
                .Name = "$$q",
                .Value = ifExpr(
                    binary("<", idf("$$lf"), f(0.5), boolType),
                    binary("*", idf("$$lf"), binary("+", f(1.0), idf("$$sf"), floatType), floatType),
                    binary("-", binary("+", idf("$$lf"), idf("$$sf"), floatType), binary("*", idf("$$lf"), idf("$$sf"), floatType), floatType),
                    floatType),
            });
            auto chroma = makeLet(std::move(qBinding), std::move(pLet), colorType);

            auto body = ifExpr(
                binary("==", idf("$$sf"), f(0.0), boolType),
                std::move(grayBody),
                std::move(chroma),
                colorType);
            std::vector<NAst::TLetExpr::TBinding> derivedBindings;
            derivedBindings.push_back({ .Name = "$$hf", .Value = binary("/", idi("$$h"), f(360.0), floatType) });
            derivedBindings.push_back({ .Name = "$$sf", .Value = binary("/", idi("$$s"), f(100.0), floatType) });
            derivedBindings.push_back({ .Name = "$$lf", .Value = binary("/", idi("$$l"), f(100.0), floatType) });
            auto derived = makeLet(std::move(derivedBindings), std::move(body), colorType);
            return makeLet(std::move(bindings), std::move(derived), colorType);
        };
    };

    auto inlineHsv = [integerType, floatType, boolType, colorType, colorPack, intLiteral, floatLiteral, ident, binary, cast, ifExpr](bool withAlpha) {
        return [integerType, floatType, boolType, colorType, colorPack, intLiteral, floatLiteral, ident, binary, cast, ifExpr, withAlpha](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
            auto loc = args[0]->Location;
            auto f = [&](double value) { return floatLiteral(loc, value); };
            auto idf = [&](const std::string& name) { return ident(loc, name, floatType); };
            auto idi = [&](const std::string& name) { return ident(loc, name, integerType); };
            auto makeLet = [&](std::vector<NAst::TLetExpr::TBinding> bindings, NAst::TExprPtr body, NAst::TTypePtr type) {
                auto expr = std::make_shared<NAst::TLetExpr>(loc, std::move(bindings), std::move(body));
                expr->Type = std::move(type);
                return expr;
            };
            auto round255 = [&](NAst::TExprPtr value) -> NAst::TExprPtr {
                return cast(binary("+", binary("*", std::move(value), f(255.0), floatType), f(0.5), floatType), integerType);
            };
            auto eqI = [&](int64_t value) {
                return binary("==", idi("$$i"), intLiteral(loc, value), boolType);
            };
            auto selectChannel = [&](const std::array<std::string, 6>& cases, const std::string& fallback) -> NAst::TExprPtr {
                NAst::TExprPtr selected = idf(fallback);
                for (int64_t i = 4; i >= 0; --i) {
                    selected = ifExpr(eqI(i), idf(cases[static_cast<size_t>(i)]), std::move(selected), floatType);
                }
                return selected;
            };

            auto r = round255(selectChannel({"$$vf", "$$q", "$$p", "$$p", "$$t", "$$vf"}, "$$vf"));
            auto g = round255(selectChannel({"$$t", "$$vf", "$$vf", "$$q", "$$p", "$$p"}, "$$p"));
            auto b = round255(selectChannel({"$$p", "$$p", "$$t", "$$vf", "$$vf", "$$q"}, "$$q"));
            auto alpha = withAlpha ? idi("$$a") : intLiteral(loc, 255);
            auto body = colorPack(std::move(r), std::move(g), std::move(b), std::move(alpha));

            std::vector<NAst::TLetExpr::TBinding> colorBindings;
            colorBindings.push_back({
                .Name = "$$i",
                .Value = ifExpr(
                    binary("==", idi("$$sector"), intLiteral(loc, 6), boolType),
                    intLiteral(loc, 0),
                    idi("$$sector"),
                    integerType),
            });
            colorBindings.push_back({ .Name = "$$p", .Value = binary("*", idf("$$vf"), binary("-", f(1.0), idf("$$sf"), floatType), floatType) });
            colorBindings.push_back({ .Name = "$$q", .Value = binary("*", idf("$$vf"), binary("-", f(1.0), binary("*", idf("$$f"), idf("$$sf"), floatType), floatType), floatType) });
            colorBindings.push_back({ .Name = "$$t", .Value = binary("*", idf("$$vf"), binary("-", f(1.0), binary("*", binary("-", f(1.0), idf("$$f"), floatType), idf("$$sf"), floatType), floatType), floatType) });
            auto colorLet = makeLet(std::move(colorBindings), std::move(body), colorType);

            std::vector<NAst::TLetExpr::TBinding> fBindings;
            fBindings.push_back({ .Name = "$$f", .Value = binary("-", idf("$$hf"), cast(idi("$$sector"), floatType), floatType) });
            auto fLet = makeLet(std::move(fBindings), std::move(colorLet), colorType);

            std::vector<NAst::TLetExpr::TBinding> sectorBindings;
            sectorBindings.push_back({ .Name = "$$sector", .Value = cast(idf("$$hf"), integerType) });
            auto sectorLet = makeLet(std::move(sectorBindings), std::move(fLet), colorType);

            std::vector<NAst::TLetExpr::TBinding> derivedBindings;
            derivedBindings.push_back({ .Name = "$$hf", .Value = binary("/", idi("$$h"), f(60.0), floatType) });
            derivedBindings.push_back({ .Name = "$$sf", .Value = binary("/", idi("$$s"), f(100.0), floatType) });
            derivedBindings.push_back({ .Name = "$$vf", .Value = binary("/", idi("$$v"), f(100.0), floatType) });
            auto derivedLet = makeLet(std::move(derivedBindings), std::move(sectorLet), colorType);

            std::vector<NAst::TLetExpr::TBinding> argBindings;
            argBindings.push_back({ .Name = "$$h", .Value = args[0] });
            argBindings.push_back({ .Name = "$$s", .Value = args[1] });
            argBindings.push_back({ .Name = "$$v", .Value = args[2] });
            if (withAlpha) {
                argBindings.push_back({ .Name = "$$a", .Value = args[3] });
            }
            return makeLet(std::move(argBindings), std::move(derivedLet), colorType);
        };
    };

    auto decomposeRgbTempId = std::make_shared<size_t>(0);
    auto decomposeRgb = [integerType, colorType, voidType, intLiteral, decomposeRgbTempId, ident, binary](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
        const auto colorName = "$$color_decompose_rgb_" + std::to_string((*decomposeRgbTempId)++);

        auto component = [&](int64_t shift) -> NAst::TExprPtr {
            auto loc = args[0]->Location;
            NAst::TExprPtr value = ident(loc, colorName, integerType);
            if (shift != 0) {
                value = binary(">>", std::move(value), intLiteral(loc, shift), integerType);
            }
            return binary("&", std::move(value), intLiteral(loc, 255), integerType);
        };

        auto assignTarget = [&](const NAst::TExprPtr& target, NAst::TExprPtr value) -> NAst::TExprPtr {
            if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(target)) {
                auto id = maybeIdent.Cast();
                return std::make_shared<NAst::TAssignExpr>(target->Location, id->Name, std::move(value));
            }
            if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(target)) {
                auto index = maybeIndex.Cast();
                auto maybeCollection = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection);
                if (maybeCollection) {
                    return std::make_shared<NAst::TArrayAssignExpr>(
                        target->Location,
                        maybeCollection.Cast()->Name,
                        std::vector<NAst::TExprPtr>{index->Index},
                        std::move(value));
                }
            }
            if (auto maybeMultiIndex = NAst::TMaybeNode<NAst::TMultiIndexExpr>(target)) {
                auto index = maybeMultiIndex.Cast();
                auto maybeCollection = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection);
                if (maybeCollection) {
                    return std::make_shared<NAst::TArrayAssignExpr>(
                        target->Location,
                        maybeCollection.Cast()->Name,
                        index->Indices,
                        std::move(value));
                }
            }

            return std::make_shared<NAst::TCallExpr>(
                target->Location,
                std::make_shared<NAst::TIdentExpr>(target->Location, "__unsupported_inline_color_decompose_target"),
                std::vector<NAst::TExprPtr>{});
        };

        auto block = std::make_shared<NAst::TBlockExpr>(args[0]->Location, std::vector<NAst::TExprPtr>{});
        block->Type = voidType;

        auto colorVar = std::make_shared<NAst::TVarStmt>(args[0]->Location, colorName, integerType);
        colorVar->Type = integerType;
        block->Stmts.push_back(colorVar);
        auto colorValue = std::make_shared<NAst::TCastExpr>(args[0]->Location, args[0], integerType);
        colorValue->Type = integerType;
        block->Stmts.push_back(std::make_shared<NAst::TAssignExpr>(args[0]->Location, colorName, colorValue));
        block->Stmts.push_back(assignTarget(args[1], component(16)));
        block->Stmts.push_back(assignTarget(args[2], component(8)));
        block->Stmts.push_back(assignTarget(args[3], component(0)));
        return block;
    };

    auto decomposeColorTempId = std::make_shared<size_t>(0);
    auto makeColorDecomposeBlock =
        [integerType, floatType, boolType, voidType, intLiteral, floatLiteral, ident, binary, cast, ifExpr, decomposeColorTempId]
        (const char* suffix, const auto& emitBody) {
            return [=](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
                auto loc = args[0]->Location;
                const auto prefix = std::string("$$color_decompose_") + suffix + "_" + std::to_string((*decomposeColorTempId)++);
                auto name = [&](const char* local) { return prefix + local; };
                auto idi = [&](const char* local) { return ident(loc, name(local), integerType); };
                auto idf = [&](const char* local) { return ident(loc, name(local), floatType); };
                auto f = [&](double value) { return floatLiteral(loc, value); };
                auto assignLocal = [&](const char* local, NAst::TExprPtr value) -> NAst::TExprPtr {
                    return std::make_shared<NAst::TAssignExpr>(loc, name(local), std::move(value));
                };
                auto roundToInt = [&](NAst::TExprPtr value) -> NAst::TExprPtr {
                    return cast(binary("+", std::move(value), f(0.5), floatType), integerType);
                };
                auto assignTarget = [&](const NAst::TExprPtr& target, NAst::TExprPtr value) -> NAst::TExprPtr {
                    if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(target)) {
                        return std::make_shared<NAst::TAssignExpr>(target->Location, maybeIdent.Cast()->Name, std::move(value));
                    }
                    if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(target)) {
                        auto index = maybeIndex.Cast();
                        if (auto maybeCollection = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection)) {
                            return std::make_shared<NAst::TArrayAssignExpr>(
                                target->Location,
                                maybeCollection.Cast()->Name,
                                std::vector<NAst::TExprPtr>{index->Index},
                                std::move(value));
                        }
                    }
                    if (auto maybeMultiIndex = NAst::TMaybeNode<NAst::TMultiIndexExpr>(target)) {
                        auto index = maybeMultiIndex.Cast();
                        if (auto maybeCollection = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection)) {
                            return std::make_shared<NAst::TArrayAssignExpr>(
                                target->Location,
                                maybeCollection.Cast()->Name,
                                index->Indices,
                                std::move(value));
                        }
                    }

                    return std::make_shared<NAst::TCallExpr>(
                        target->Location,
                        std::make_shared<NAst::TIdentExpr>(target->Location, "__unsupported_inline_color_decompose_target"),
                        std::vector<NAst::TExprPtr>{});
                };

                auto block = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
                block->Type = voidType;
                auto declare = [&](const char* local, NAst::TTypePtr type) {
                    auto var = std::make_shared<NAst::TVarStmt>(loc, name(local), std::move(type));
                    block->Stmts.push_back(var);
                };
                for (const char* local : {"$color", "$r", "$g", "$b"}) {
                    declare(local, integerType);
                }
                for (const char* local : {"$rf", "$gf", "$bf", "$mx", "$mn", "$d", "$hf", "$sf", "$lf", "$kf"}) {
                    declare(local, floatType);
                }

                block->Stmts.push_back(assignLocal("$color", cast(args[0], integerType)));
                auto component = [&](int64_t shift) -> NAst::TExprPtr {
                    NAst::TExprPtr value = idi("$color");
                    if (shift != 0) {
                        value = binary(">>", std::move(value), intLiteral(loc, shift), integerType);
                    }
                    return binary("&", std::move(value), intLiteral(loc, 255), integerType);
                };
                block->Stmts.push_back(assignLocal("$r", component(16)));
                block->Stmts.push_back(assignLocal("$g", component(8)));
                block->Stmts.push_back(assignLocal("$b", component(0)));
                block->Stmts.push_back(assignLocal("$rf", binary("/", idi("$r"), f(255.0), floatType)));
                block->Stmts.push_back(assignLocal("$gf", binary("/", idi("$g"), f(255.0), floatType)));
                block->Stmts.push_back(assignLocal("$bf", binary("/", idi("$b"), f(255.0), floatType)));
                block->Stmts.push_back(assignLocal("$mx", ifExpr(
                    binary(">", idf("$rf"), idf("$gf"), boolType),
                    ifExpr(binary(">", idf("$rf"), idf("$bf"), boolType), idf("$rf"), idf("$bf"), floatType),
                    ifExpr(binary(">", idf("$gf"), idf("$bf"), boolType), idf("$gf"), idf("$bf"), floatType),
                    floatType)));
                block->Stmts.push_back(assignLocal("$mn", ifExpr(
                    binary("<", idf("$rf"), idf("$gf"), boolType),
                    ifExpr(binary("<", idf("$rf"), idf("$bf"), boolType), idf("$rf"), idf("$bf"), floatType),
                    ifExpr(binary("<", idf("$gf"), idf("$bf"), boolType), idf("$gf"), idf("$bf"), floatType),
                    floatType)));

                emitBody(loc, block, idi, idf, f, assignLocal, assignTarget, roundToInt, args);
                return block;
            };
        };

    auto decomposeCmyk = makeColorDecomposeBlock("cmyk",
        [integerType, floatType, boolType, voidType, intLiteral, binary]
        (TLocation loc, const std::shared_ptr<NAst::TBlockExpr>& block,
         const auto& idi, const auto& idf, const auto& f, const auto& assignLocal,
         const auto& assignTarget, const auto& roundToInt, const std::vector<NAst::TExprPtr>& args) {
            block->Stmts.push_back(assignLocal("$kf", binary("-", f(1.0), idf("$mx"), floatType)));
            auto percent = [&](NAst::TExprPtr channel) {
                return roundToInt(binary("*",
                    binary("/", binary("-", binary("-", f(1.0), std::move(channel), floatType), idf("$kf"), floatType),
                        binary("-", f(1.0), idf("$kf"), floatType), floatType),
                    f(100.0), floatType));
            };
            auto black = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            black->Type = voidType;
            black->Stmts.push_back(assignTarget(args[1], intLiteral(loc, 0)));
            black->Stmts.push_back(assignTarget(args[2], intLiteral(loc, 0)));
            black->Stmts.push_back(assignTarget(args[3], intLiteral(loc, 0)));
            black->Stmts.push_back(assignTarget(args[4], intLiteral(loc, 100)));
            auto chroma = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            chroma->Type = voidType;
            chroma->Stmts.push_back(assignTarget(args[1], percent(idf("$rf"))));
            chroma->Stmts.push_back(assignTarget(args[2], percent(idf("$gf"))));
            chroma->Stmts.push_back(assignTarget(args[3], percent(idf("$bf"))));
            chroma->Stmts.push_back(assignTarget(args[4], roundToInt(binary("*", idf("$kf"), f(100.0), floatType))));
            auto stmt = std::make_shared<NAst::TIfExpr>(
                loc,
                binary(">=", idf("$kf"), f(1.0), boolType),
                std::move(black),
                std::move(chroma));
            stmt->Type = voidType;
            block->Stmts.push_back(stmt);
        });

    auto decomposeHsl = makeColorDecomposeBlock("hsl",
        [integerType, floatType, boolType, voidType, binary, ifExpr]
        (TLocation loc, const std::shared_ptr<NAst::TBlockExpr>& block,
         const auto& idi, const auto& idf, const auto& f, const auto& assignLocal,
         const auto& assignTarget, const auto& roundToInt, const std::vector<NAst::TExprPtr>& args) {
            block->Stmts.push_back(assignLocal("$lf", binary("/", binary("+", idf("$mx"), idf("$mn"), floatType), f(2.0), floatType)));
            block->Stmts.push_back(assignLocal("$sf", f(0.0)));
            block->Stmts.push_back(assignLocal("$hf", f(0.0)));
            auto chroma = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            chroma->Type = voidType;
            chroma->Stmts.push_back(assignLocal("$d", binary("-", idf("$mx"), idf("$mn"), floatType)));
            chroma->Stmts.push_back(assignLocal("$sf", ifExpr(
                binary(">", idf("$lf"), f(0.5), boolType),
                binary("/", idf("$d"), binary("-", f(2.0), binary("+", idf("$mx"), idf("$mn"), floatType), floatType), floatType),
                binary("/", idf("$d"), binary("+", idf("$mx"), idf("$mn"), floatType), floatType),
                floatType)));
            chroma->Stmts.push_back(assignLocal("$hf", ifExpr(
                binary("==", idf("$mx"), idf("$rf"), boolType),
                binary("+", binary("/", binary("-", idf("$gf"), idf("$bf"), floatType), idf("$d"), floatType),
                    ifExpr(binary("<", idf("$gf"), idf("$bf"), boolType), f(6.0), f(0.0), floatType), floatType),
                ifExpr(
                    binary("==", idf("$mx"), idf("$gf"), boolType),
                    binary("+", binary("/", binary("-", idf("$bf"), idf("$rf"), floatType), idf("$d"), floatType), f(2.0), floatType),
                    binary("+", binary("/", binary("-", idf("$rf"), idf("$gf"), floatType), idf("$d"), floatType), f(4.0), floatType),
                    floatType),
                floatType)));
            chroma->Stmts.push_back(assignLocal("$hf", binary("/", idf("$hf"), f(6.0), floatType)));
            auto noop = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            noop->Type = voidType;
            auto stmt = std::make_shared<NAst::TIfExpr>(
                loc,
                binary("!=", idf("$mx"), idf("$mn"), boolType),
                std::move(chroma),
                std::move(noop));
            stmt->Type = voidType;
            block->Stmts.push_back(stmt);
            block->Stmts.push_back(assignTarget(args[1], roundToInt(binary("*", idf("$hf"), f(360.0), floatType))));
            block->Stmts.push_back(assignTarget(args[2], roundToInt(binary("*", idf("$sf"), f(100.0), floatType))));
            block->Stmts.push_back(assignTarget(args[3], roundToInt(binary("*", idf("$lf"), f(100.0), floatType))));
        });

    auto decomposeHsv = makeColorDecomposeBlock("hsv",
        [integerType, floatType, boolType, voidType, binary, ifExpr]
        (TLocation loc, const std::shared_ptr<NAst::TBlockExpr>& block,
         const auto& idi, const auto& idf, const auto& f, const auto& assignLocal,
         const auto& assignTarget, const auto& roundToInt, const std::vector<NAst::TExprPtr>& args) {
            block->Stmts.push_back(assignLocal("$d", binary("-", idf("$mx"), idf("$mn"), floatType)));
            block->Stmts.push_back(assignLocal("$sf", ifExpr(
                binary("==", idf("$mx"), f(0.0), boolType),
                f(0.0),
                binary("/", idf("$d"), idf("$mx"), floatType),
                floatType)));
            block->Stmts.push_back(assignLocal("$hf", f(0.0)));
            auto chroma = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            chroma->Type = voidType;
            chroma->Stmts.push_back(assignLocal("$hf", ifExpr(
                binary("==", idf("$mx"), idf("$rf"), boolType),
                binary("+", binary("/", binary("-", idf("$gf"), idf("$bf"), floatType), idf("$d"), floatType),
                    ifExpr(binary("<", idf("$gf"), idf("$bf"), boolType), f(6.0), f(0.0), floatType), floatType),
                ifExpr(
                    binary("==", idf("$mx"), idf("$gf"), boolType),
                    binary("+", binary("/", binary("-", idf("$bf"), idf("$rf"), floatType), idf("$d"), floatType), f(2.0), floatType),
                    binary("+", binary("/", binary("-", idf("$rf"), idf("$gf"), floatType), idf("$d"), floatType), f(4.0), floatType),
                    floatType),
                floatType)));
            chroma->Stmts.push_back(assignLocal("$hf", binary("/", idf("$hf"), f(6.0), floatType)));
            auto noop = std::make_shared<NAst::TBlockExpr>(loc, std::vector<NAst::TExprPtr>{});
            noop->Type = voidType;
            auto stmt = std::make_shared<NAst::TIfExpr>(
                loc,
                binary("!=", idf("$d"), f(0.0), boolType),
                std::move(chroma),
                std::move(noop));
            stmt->Type = voidType;
            block->Stmts.push_back(stmt);
            block->Stmts.push_back(assignTarget(args[1], roundToInt(binary("*", idf("$hf"), f(360.0), floatType))));
            block->Stmts.push_back(assignTarget(args[2], roundToInt(binary("*", idf("$sf"), f(100.0), floatType))));
            block->Stmts.push_back(assignTarget(args[3], roundToInt(binary("*", idf("$mx"), f(100.0), floatType))));
        });

    auto makeOutInt = [&]() -> NAst::TTypePtr {
        auto t = std::make_shared<NAst::TIntegerType>();
        t->Mutable  = true;
        t->Readable = false;
        return std::make_shared<NAst::TReferenceType>(t);
    };

    ExternalTypes_ = {
        {
            .Name = "цвет",
            .Type = colorStorageType,
        },
    };

    ExternalFunctions_ = {

        // ── Color constants ───────────────────────────────────────────────────
        {
            .Name = "прозрачный",
            .MangledName = "color_transparent",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackARGB(0, 0, 0, 0)),
        },
        {
            .Name = "белый",
            .MangledName = "color_white",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 255)),
        },
        {
            .Name = "чёрный",
            .MangledName = "color_black",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 0)),
        },
        {
            .Name = "черный",
            .MangledName = "color_black",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 0)),
        },
        {
            .Name = "серый",
            .MangledName = "color_gray",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(128, 128, 128)),
        },
        {
            .Name = "фиолетовый",
            .MangledName = "color_purple",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(128, 0, 128)),
        },
        {
            .Name = "синий",
            .MangledName = "color_blue",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 255)),
        },
        {
            .Name = "голубой",
            .MangledName = "color_cyan",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 255, 255)),
        },
        {
            .Name = "зелёный",
            .MangledName = "color_green",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 128, 0)),
        },
        {
            .Name = "зеленый",
            .MangledName = "color_green",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 128, 0)),
        },
        {
            .Name = "жёлтый",
            .MangledName = "color_yellow",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 0)),
        },
        {
            .Name = "желтый",
            .MangledName = "color_yellow",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 0)),
        },
        {
            .Name = "оранжевый",
            .MangledName = "color_orange",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 165, 0)),
        },
        {
            .Name = "красный",
            .MangledName = "color_red",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 0, 0)),
        },

        // ── Color construction ────────────────────────────────────────────────
        {
            .Name = "RGB",
            .MangledName = "color_rgb",
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = [colorPack](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
                return colorPack(args[0], args[1], args[2],
                    std::make_shared<NAst::TNumberExpr>(args[0]->Location, int64_t{255}));
            },
        },
        {
            .Name = "RGBA",
            .MangledName = "color_rgba",
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = [colorPack](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
                return colorPack(args[0], args[1], args[2], args[3]);
            },
        },
        {
            .Name = "CMYK",
            .MangledName = "color_cmyk",
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineCmyk(false),
        },
        {
            .Name = "CMYKA",
            .MangledName = "color_cmyka",
            .ArgTypes = { integerType, integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineCmyk(true),
        },
        {
            .Name = "HSL",
            .MangledName = "color_hsl",
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineHsl(false),
        },
        {
            .Name = "HSLA",
            .MangledName = "color_hsla",
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineHsl(true),
        },
        {
            .Name = "HSV",
            .MangledName = "color_hsv",
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineHsv(false),
        },
        {
            .Name = "HSVA",
            .MangledName = "color_hsva",
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
            .Inline = inlineHsv(true),
        },

        // ── Color decomposition ───────────────────────────────────────────────
        {
            .Name = "разложить в RGB",
            .MangledName = "color_decompose_rgb",
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
            .Inline = decomposeRgb,
        },
        {
            .Name = "разложить в CMYK",
            .MangledName = "color_decompose_cmyk",
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
            .Inline = decomposeCmyk,
        },
        {
            .Name = "разложить в HSL",
            .MangledName = "color_decompose_hsl",
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
            .Inline = decomposeHsl,
        },
        {
            .Name = "разложить в HSV",
            .MangledName = "color_decompose_hsv",
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
            .Inline = decomposeHsv,
        },

        // ── Output operator ───────────────────────────────────────────────────
        {
            .Name = "вывод",
            .MangledName = "color_print",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(color_print)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_print(static_cast<int64_t>(args[0]));
                return 0;
            },
            .ArgTypes = { colorType },
            .ReturnType = voidType,
            .IsOp = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
