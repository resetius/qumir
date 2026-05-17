#include "printer.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace NQumir {
namespace NAst {
namespace NCore {

namespace {

class TPrinter {
public:
    TPrinter(std::ostream& out, TPrintOptions options)
        : Out(out)
        , Options(options)
    { }

    void PrintExpr(TExprPtr expr) {
        PrintExpr(std::move(expr), /*allowTypeWrap=*/true, /*level=*/0);
    }

    void PrintType(TTypePtr type) {
        PrintType(std::move(type), /*level=*/0);
    }

private:
    bool FitsOneLine(TExprPtr expr, bool allowTypeWrap, int level) const {
        if (!Options.Pretty) {
            return true;
        }
        auto compactOptions = Options;
        compactOptions.Pretty = false;
        std::ostringstream out;
        TPrinter printer(out, compactOptions);
        printer.PrintExpr(std::move(expr), allowTypeWrap, level);
        return level * Options.IndentStep + out.str().size() <= Options.LineWidth;
    }

    bool ForceMultiline(TExprPtr expr) const {
        return TMaybeNode<TBlockExpr>(expr)
            || TMaybeNode<TSeqExpr>(expr)
            || TMaybeNode<TVarsBlockExpr>(expr)
            || TMaybeNode<TFunDecl>(expr);
    }

    void PrintCompact(TExprPtr expr, bool allowTypeWrap, int level) {
        auto compactOptions = Options;
        compactOptions.Pretty = false;
        TPrinter printer(Out, compactOptions);
        printer.PrintExpr(std::move(expr), allowTypeWrap, level);
    }

    void Separator(int level) {
        if (Options.Pretty) {
            Out << '\n' << std::string(static_cast<size_t>(level * Options.IndentStep), ' ');
        } else {
            Out << ' ';
        }
    }

    void Space() {
        Out << ' ';
    }

    bool ShouldWrapType(const TExprPtr& expr) const {
        if (!expr || !expr->Type) {
            return false;
        }
        if (TMaybeNode<TVarStmt>(expr)) {
            return false;
        }
        if (Options.TypeMode == ETypePrintMode::All) {
            return !TMaybeNode<TFunDecl>(expr)
                && !TMaybeNode<TCastExpr>(expr);
        }
        return TMaybeType<TNamedType>(expr->Type)
            || TMaybeNode<TStructConstructExpr>(expr);
    }

    void PrintExpr(TExprPtr expr, bool allowTypeWrap, int level) {
        if (!expr) {
            Out << "nil";
            return;
        }

        if (allowTypeWrap && ShouldWrapType(expr)) {
            if (Options.Pretty && !ForceMultiline(expr) && FitsOneLine(expr, allowTypeWrap, level)) {
                PrintCompact(std::move(expr), allowTypeWrap, level);
                return;
            }
            Out << "(: ";
            PrintExpr(expr, /*allowTypeWrap=*/false, level + 1);
            Separator(level + 1);
            PrintType(expr->Type, level + 1);
            Out << ')';
            return;
        }

        if (Options.Pretty && !ForceMultiline(expr) && FitsOneLine(expr, allowTypeWrap, level)) {
            PrintCompact(std::move(expr), allowTypeWrap, level);
            return;
        }

        if (auto n = TMaybeNode<TIdentExpr>(expr)) {
            PrintIdentifier(n.Cast()->Name);
        } else if (auto n = TMaybeNode<TAssignExpr>(expr)) {
            Out << "(= ";
            PrintIdentifier(n.Cast()->Name);
            Separator(level + 1);
            PrintExpr(n.Cast()->Value, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TArrayAssignExpr>(expr)) {
            PrintArrayAssign(n.Cast(), level);
        } else if (auto n = TMaybeNode<TNumberExpr>(expr)) {
            PrintNumber(n.Cast());
        } else if (auto n = TMaybeNode<TStringLiteralExpr>(expr)) {
            PrintString(n.Cast()->Value, '"');
        } else if (auto n = TMaybeNode<TUnaryExpr>(expr)) {
            Out << '(' << n.Cast()->Operator.ToString() << ' ';
            PrintExpr(n.Cast()->Operand, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TBinaryExpr>(expr)) {
            Out << '(' << n.Cast()->Operator.ToString() << ' ';
            PrintExpr(n.Cast()->Left, true, level + 1);
            Separator(level + 1);
            PrintExpr(n.Cast()->Right, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TBlockExpr>(expr)) {
            PrintExprList("block", n.Cast()->Stmts, level);
        } else if (auto n = TMaybeNode<TSeqExpr>(expr)) {
            PrintExprList("seq", n.Cast()->Stmts, level);
        } else if (auto n = TMaybeNode<TIfStmt>(expr)) {
            PrintIfLike("cond", n.Cast()->Cond, n.Cast()->Then, n.Cast()->Else, level);
        } else if (auto n = TMaybeNode<TIfExpr>(expr)) {
            PrintIfLike("if", n.Cast()->Cond, n.Cast()->Then, n.Cast()->Else, level);
        } else if (auto n = TMaybeNode<TLetExpr>(expr)) {
            PrintLet(n.Cast(), level);
        } else if (auto n = TMaybeNode<TWhileStmtExpr>(expr)) {
            PrintWhile(n.Cast(), level);
        } else if (auto n = TMaybeNode<TRepeatStmtExpr>(expr)) {
            PrintRepeat(n.Cast(), level);
        } else if (auto n = TMaybeNode<TForStmtExpr>(expr)) {
            PrintFor(n.Cast(), level);
        } else if (auto n = TMaybeNode<TTimesStmtExpr>(expr)) {
            PrintTimes(n.Cast(), level);
        } else if (TMaybeNode<TBreakStmt>(expr)) {
            Out << "break";
        } else if (TMaybeNode<TContinueStmt>(expr)) {
            Out << "continue";
        } else if (auto n = TMaybeNode<TVarStmt>(expr)) {
            PrintVar(n.Cast(), level);
        } else if (auto n = TMaybeNode<TVarsBlockExpr>(expr)) {
            PrintExprList("vars", n.Cast()->Vars, level);
        } else if (auto n = TMaybeNode<TFunDecl>(expr)) {
            PrintFun(n.Cast(), level);
        } else if (auto n = TMaybeNode<TCallExpr>(expr)) {
            PrintCall(n.Cast(), level);
        } else if (auto n = TMaybeNode<TAwaitExpr>(expr)) {
            Out << "(await";
            Separator(level + 1);
            PrintExpr(n.Cast()->Operand, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TInputExpr>(expr)) {
            PrintExprList("input", n.Cast()->Args, level);
        } else if (auto n = TMaybeNode<TOutputExpr>(expr)) {
            PrintOutput(n.Cast(), level);
        } else if (auto n = TMaybeNode<TCastExpr>(expr)) {
            Out << "(cast";
            Separator(level + 1);
            PrintExpr(n.Cast()->Operand, true, level + 1);
            Separator(level + 1);
            PrintType(n.Cast()->Type, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TIndexExpr>(expr)) {
            Out << "(index";
            Separator(level + 1);
            PrintExpr(n.Cast()->Index, true, level + 1);
            Separator(level + 1);
            PrintExpr(n.Cast()->Collection, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TMultiIndexExpr>(expr)) {
            PrintMultiIndex(n.Cast(), level);
        } else if (auto n = TMaybeNode<TSliceExpr>(expr)) {
            PrintSlice(n.Cast(), level);
        } else if (auto n = TMaybeNode<TUseExpr>(expr)) {
            Out << "(use ";
            PrintString(n.Cast()->ModuleName, '"');
            Out << ')';
        } else if (auto n = TMaybeNode<TAssertStmt>(expr)) {
            Out << "(assert";
            Separator(level + 1);
            PrintExpr(n.Cast()->Expr, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TFieldAccessExpr>(expr)) {
            Out << "(field ";
            PrintIdentifier(n.Cast()->FieldName);
            Separator(level + 1);
            PrintExpr(n.Cast()->Object, true, level + 1);
            Out << ')';
        } else if (auto n = TMaybeNode<TStructConstructExpr>(expr)) {
            PrintStructConstruct(n.Cast(), level);
        } else if (auto n = TMaybeNode<TFieldAssignExpr>(expr)) {
            Out << "(field_assign";
            Separator(level + 1);
            PrintExpr(n.Cast()->Object, true, level + 1);
            Separator(level + 1);
            PrintIdentifier(n.Cast()->FieldName);
            Separator(level + 1);
            PrintExpr(n.Cast()->Value, true, level + 1);
            Out << ')';
        } else {
            throw std::runtime_error("unsupported AST node for core print: " + std::string(expr->NodeName()));
        }
    }

    bool HasPrintableTypeAttrs(TTypePtr type) const {
        return type && type->Mutable && !type->Readable;
    }

    // Appends non-default attrs before '>'. Readable is the default and is omitted.
    void PrintTypeAttrs(TTypePtr type) {
        if (!HasPrintableTypeAttrs(type)) {
            return;
        }
        Out << " (";
        if (type->Mutable) {
            Out << "mutable";
        }
        Out << ')';
    }

    // Prints a simple (scalar) type, wrapping in <> with attrs if flags are non-default.
    void PrintScalarType(const char* name, TTypePtr type) {
        if (HasPrintableTypeAttrs(type)) {
            Out << '<' << name;
            PrintTypeAttrs(type);
            Out << '>';
        } else {
            Out << name;
        }
    }

    void PrintType(TTypePtr type, int level) {
        if (!type) {
            Out << "nil";
        } else if (TMaybeType<TIntegerType>(type)) {
            PrintScalarType("i64", type);
        } else if (TMaybeType<TFloatType>(type)) {
            PrintScalarType("f64", type);
        } else if (TMaybeType<TBoolType>(type)) {
            PrintScalarType("bool", type);
        } else if (TMaybeType<TStringType>(type)) {
            PrintScalarType("string", type);
        } else if (TMaybeType<TSymbolType>(type)) {
            PrintScalarType("char", type);
        } else if (TMaybeType<TFileType>(type)) {
            PrintScalarType("file", type);
        } else if (TMaybeType<TVoidType>(type)) {
            PrintScalarType("void", type);
        } else if (auto t = TMaybeType<TFunctionType>(type)) {
            PrintFunctionType(t.Cast(), level);
        } else if (auto t = TMaybeType<TFutureType>(type)) {
            Out << "<future ";
            PrintType(t.Cast()->ResultType, level);
            PrintTypeAttrs(type);
            Out << '>';
        } else if (auto t = TMaybeType<TArrayType>(type)) {
            Out << "<array ";
            PrintType(t.Cast()->ElementType, level);
            Out << ' ' << t.Cast()->Arity << '>';
        } else if (auto t = TMaybeType<TPointerType>(type)) {
            Out << "<ptr ";
            PrintType(t.Cast()->PointeeType, level);
            PrintTypeAttrs(type);
            Out << '>';
        } else if (auto t = TMaybeType<TReferenceType>(type)) {
            Out << "<ref ";
            PrintType(t.Cast()->ReferencedType, level);
            PrintTypeAttrs(type);
            Out << '>';
        } else if (auto t = TMaybeType<TNamedType>(type)) {
            if (Options.ShortNamedTypes.contains(t.Cast()->Name)) {
                PrintIdentifier(t.Cast()->Name);
            } else {
                Out << "<named ";
                PrintIdentifier(t.Cast()->Name);
                Out << ' ';
                PrintType(t.Cast()->UnderlyingType, level);
                PrintTypeAttrs(type);
                Out << '>';
            }
        } else if (auto t = TMaybeType<TStructType>(type)) {
            PrintStructType(t.Cast(), level);
        } else {
            throw std::runtime_error("unsupported type for core print: " + std::string(type->TypeName()));
        }
    }

    void PrintIdentifier(const std::string& value) {
        if (value.find(' ') != std::string::npos) {
            Out << '|' << value << '|';
        } else {
            Out << value;
        }
    }

    void PrintString(const std::string& value, char quote) {
        Out << quote;
        for (char ch : value) {
            switch (ch) {
                case '\n': Out << "\\n"; break;
                case '\t': Out << "\\t"; break;
                case '\\': Out << "\\\\"; break;
                case '"': Out << (quote == '"' ? "\\\"" : "\""); break;
                case '\'': Out << (quote == '\'' ? "\\'" : "'"); break;
                default: Out << ch;
            }
        }
        Out << quote;
    }

    void PrintNumber(const std::shared_ptr<TNumberExpr>& node) {
        if (TMaybeType<TBoolType>(node->Type)) {
            Out << (node->IntValue ? "#t" : "#f");
        } else if (TMaybeType<TSymbolType>(node->Type)) {
            PrintString(std::string(1, static_cast<char>(node->IntValue)), '\'');
        } else if (node->IsFloat) {
            const auto oldPrecision = Out.precision();
            Out << std::setprecision(std::numeric_limits<double>::max_digits10);
            Out << node->FloatValue;
            Out.precision(oldPrecision);
        } else {
            Out << node->IntValue;
        }
    }

    void PrintExprList(std::string_view head, const std::vector<TExprPtr>& items, int level) {
        Out << '(' << head;
        for (const auto& item : items) {
            Separator(level + 1);
            PrintExpr(item, true, level + 1);
        }
        Out << ')';
    }

    void PrintIfLike(std::string_view head, TExprPtr cond, TExprPtr thenBranch, TExprPtr elseBranch, int level) {
        Out << '(' << head;
        Separator(level + 1);
        PrintExpr(cond, true, level + 1);
        Separator(level + 1);
        PrintExpr(thenBranch, true, level + 1);
        if (elseBranch) {
            Separator(level + 1);
            PrintExpr(elseBranch, true, level + 1);
        }
        Out << ')';
    }

    void PrintArrayAssign(const std::shared_ptr<TArrayAssignExpr>& node, int level) {
        Out << "(= ";
        PrintIdentifier(node->Name);
        Separator(level + 1);
        PrintIndexVector(node->Indices, level + 1);
        Separator(level + 1);
        PrintExpr(node->Value, true, level + 1);
        Out << ')';
    }

    void PrintIndexVector(const std::vector<TExprPtr>& indices, int level) {
        Out << '[';
        for (size_t i = 0; i < indices.size(); ++i) {
            if (i != 0) {
                Separator(level + 1);
            }
            PrintExpr(indices[i], true, level + 1);
        }
        Out << ']';
    }

    void PrintLet(const std::shared_ptr<TLetExpr>& node, int level) {
        Out << "(let (";
        for (size_t i = 0; i < node->Bindings.size(); ++i) {
            if (i != 0) {
                Separator(level + 2);
            }
            Out << '(';
            PrintIdentifier(node->Bindings[i].Name);
            Space();
            PrintExpr(node->Bindings[i].Value, true, level + 2);
            Out << ')';
        }
        Out << ')';
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Out << ')';
    }

    void PrintWhile(const std::shared_ptr<TWhileStmtExpr>& node, int level) {
        Out << "(while";
        Separator(level + 1);
        PrintExpr(node->Cond, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Out << ')';
    }

    void PrintRepeat(const std::shared_ptr<TRepeatStmtExpr>& node, int level) {
        Out << "(repeat";
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->Cond, true, level + 1);
        Out << ')';
    }

    void PrintFor(const std::shared_ptr<TForStmtExpr>& node, int level) {
        Out << "(for ";
        PrintIdentifier(node->VarName);
        Separator(level + 1);
        PrintExpr(node->From, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->To, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->Step, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Out << ')';
    }

    void PrintTimes(const std::shared_ptr<TTimesStmtExpr>& node, int level) {
        Out << "(times";
        Separator(level + 1);
        PrintExpr(node->Count, true, level + 1);
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Out << ')';
    }

    void PrintVar(const std::shared_ptr<TVarStmt>& node, int level) {
        Out << "(var ";
        PrintIdentifier(node->Name);
        Space();
        PrintType(node->Type, level);
        for (const auto& [from, to] : node->Bounds) {
            Separator(level + 1);
            Out << '[';
            PrintExpr(from, true, level + 1);
            Space();
            PrintExpr(to, true, level + 1);
            Out << ']';
        }
        Out << ')';
    }

    void PrintFun(const std::shared_ptr<TFunDecl>& node, int level) {
        Out << "(fun ";
        PrintIdentifier(node->Name);
        Space();
        PrintType(node->RetType, level);
        Space();
        Out << '(';
        for (size_t i = 0; i < node->Params.size(); ++i) {
            if (i != 0) {
                Separator(level + 2);
            }
            PrintExpr(node->Params[i], true, level + 2);
        }
        Out << ')';
        Space();
        Out << '(';
        if (node->LastAssert) {
            Out << "(expect_after ";
            PrintExpr(node->LastAssert, true, level + 2);
            Out << ')';
        }
        Out << ')';
        Separator(level + 1);
        PrintExpr(node->Body, true, level + 1);
        Out << ')';
    }

    void PrintCall(const std::shared_ptr<TCallExpr>& node, int level) {
        Out << "(call";
        Separator(level + 1);
        PrintExpr(node->Callee, true, level + 1);
        for (const auto& arg : node->Args) {
            Separator(level + 1);
            PrintExpr(arg, true, level + 1);
        }
        Out << ')';
    }

    void PrintOutput(const std::shared_ptr<TOutputExpr>& node, int level) {
        Out << "(output";
        for (const auto& arg : node->Args) {
            Separator(level + 1);
            if (!arg.Width && !arg.Precision) {
                PrintExpr(arg.Expr, true, level + 1);
            } else {
                Out << "(fmt ";
                PrintExpr(arg.Expr, true, level + 2);
                Space();
                PrintExpr(arg.Width, true, level + 2);
                if (arg.Precision) {
                    Space();
                    PrintExpr(arg.Precision, true, level + 2);
                }
                Out << ')';
            }
        }
        Out << ')';
    }

    void PrintMultiIndex(const std::shared_ptr<TMultiIndexExpr>& node, int level) {
        Out << "(index ";
        PrintIndexVector(node->Indices, level + 1);
        Separator(level + 1);
        PrintExpr(node->Collection, true, level + 1);
        Out << ')';
    }

    void PrintSlice(const std::shared_ptr<TSliceExpr>& node, int level) {
        Out << "(slice [";
        PrintExpr(node->Start, true, level + 1);
        if (node->End) {
            Space();
            PrintExpr(node->End, true, level + 1);
        }
        Out << ']';
        Separator(level + 1);
        PrintExpr(node->Collection, true, level + 1);
        Out << ')';
    }

    void PrintStructConstruct(const std::shared_ptr<TStructConstructExpr>& node, int level) {
        Out << "(struct (";
        auto structType = GetStructType(node->Type);
        for (size_t i = 0; i < node->Fields.size(); ++i) {
            if (i != 0) {
                Separator(level + 2);
            }
            Out << '(';
            if (structType && i < structType->Fields.size()) {
                PrintIdentifier(structType->Fields[i].first);
            } else {
                PrintIdentifier("_" + std::to_string(i));
            }
            Space();
            PrintExpr(node->Fields[i], true, level + 2);
            Out << ')';
        }
        Out << "))";
    }

    std::shared_ptr<TStructType> GetStructType(TTypePtr type) {
        if (auto named = TMaybeType<TNamedType>(type)) {
            return TMaybeType<TStructType>(named.Cast()->UnderlyingType).Cast();
        }
        return TMaybeType<TStructType>(type).Cast();
    }

    void PrintFunctionType(const std::shared_ptr<TFunctionType>& type, int level) {
        Out << "<fun ";
        PrintType(type->ReturnType, level);
        Out << " (";
        for (size_t i = 0; i < type->ParamTypes.size(); ++i) {
            if (i != 0) {
                Space();
            }
            PrintType(type->ParamTypes[i], level);
        }
        Out << ")>";
    }

    void PrintStructType(const std::shared_ptr<TStructType>& type, int level) {
        Out << "<struct";
        for (const auto& [name, fieldType] : type->Fields) {
            Out << " (";
            PrintIdentifier(name);
            Space();
            PrintType(fieldType, level);
            Out << ')';
        }
        Out << '>';
    }

    std::ostream& Out;
    TPrintOptions Options;
};

} // namespace

void PrintAst(std::ostream& out, TExprPtr expr, const TPrintOptions& options) {
    TPrinter(out, options).PrintExpr(std::move(expr));
}

std::string PrintAst(TExprPtr expr, const TPrintOptions& options) {
    std::ostringstream out;
    PrintAst(out, std::move(expr), options);
    return out.str();
}

void PrintType(std::ostream& out, TTypePtr type, const TPrintOptions& options) {
    TPrinter(out, options).PrintType(std::move(type));
}

std::string PrintType(TTypePtr type, const TPrintOptions& options) {
    std::ostringstream out;
    PrintType(out, std::move(type), options);
    return out.str();
}

} // namespace NCore

std::ostream& operator<<(std::ostream& out, const TExprPtr& expr) {
    NCore::PrintAst(out, expr, NCore::TPrintOptions{.TypeMode = NCore::ETypePrintMode::Required});
    return out;
}

} // namespace NAst
} // namespace NQumir
