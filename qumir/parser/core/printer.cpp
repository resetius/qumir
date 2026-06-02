#include "printer.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace NQumir {
namespace NAst {
namespace NCore {

TPrinter::TPrinter(std::ostream& out, TPrintOptions options)
    : Out(&out)
    , Options(options)
{}

void TPrinter::PrintExpr(TExprPtr expr) {
    PrintExpr(std::move(expr), /*allowTypeWrap=*/true, /*level=*/0);
}

void TPrinter::PrintType(TTypePtr type) {
    PrintType(std::move(type), /*level=*/0);
}

bool TPrinter::FitsOneLine(TExprPtr expr, bool allowTypeWrap, int level) {
    if (!Options.Pretty) {
        return true;
    }
    std::ostringstream buf;
    auto* savedOut = Out;
    Out = &buf;
    Options.Pretty = false;
    PrintExpr(std::move(expr), allowTypeWrap, level);
    Out = savedOut;
    Options.Pretty = true;
    return level * Options.IndentStep + buf.str().size() <= Options.LineWidth;
}

bool TPrinter::ForceMultiline(const TExprPtr& expr) const {
    return expr->NodeName() == TBlockExpr::NodeId
        || expr->NodeName() == TSeqExpr::NodeId
        || expr->NodeName() == TVarsBlockExpr::NodeId
        || expr->NodeName() == TFunDecl::NodeId;
}

void TPrinter::PrintCompact(TExprPtr expr, bool allowTypeWrap, int level) {
    auto options = Options;
    Options.Pretty = false;
    PrintExpr(std::move(expr), allowTypeWrap, level);
    Options = options;
}

void TPrinter::Separator(int level) {
    if (Options.Pretty) {
        *Out << '\n' << std::string(static_cast<size_t>(level * Options.IndentStep), ' ');
    } else {
        *Out << ' ';
    }
}

void TPrinter::Space() {
    *Out << ' ';
}

bool TPrinter::ShouldWrapType(const TExprPtr& expr) const {
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
        || TMaybeNode<TStructConstructExpr>(expr)
        || IsNonDefaultIntegerLiteral(expr);
}

bool TPrinter::IsNonDefaultIntegerLiteral(const TExprPtr& expr) const {
    auto number = TMaybeNode<TNumberExpr>(expr);
    if (!number || number.Cast()->IsFloat) {
        return false;
    }
    auto integerType = TMaybeType<TIntegerType>(expr->Type);
    return integerType && integerType.Cast()->Kind != TIntegerType::I64;
}

bool TPrinter::HasPrintableTypeAttrs(TTypePtr type) const {
    return type && type->Mutable && !type->Readable;
}

void TPrinter::PrintTypeAttrs(TTypePtr type) {
    if (!HasPrintableTypeAttrs(type)) {
        return;
    }
    *Out << " (";
    if (type->Mutable) {
        *Out << "mutable";
    }
    *Out << ')';
}

void TPrinter::PrintScalarType(std::string_view name, TTypePtr type) {
    if (HasPrintableTypeAttrs(type)) {
        *Out << '<' << name;
        PrintTypeAttrs(type);
        *Out << '>';
    } else {
        *Out << name;
    }
}

void TPrinter::PrintType(TTypePtr type, int level) {
    if (!type) {
        *Out << "nil";
    } else if (auto t = TMaybeType<TIntegerType>(type)) {
        PrintScalarType(t.Cast()->ToString(), type);
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
        *Out << "<future ";
        PrintType(t.Cast()->ResultType, level);
        PrintTypeAttrs(type);
        *Out << '>';
    } else if (auto t = TMaybeType<TArrayType>(type)) {
        *Out << "<array ";
        PrintType(t.Cast()->ElementType, level);
        *Out << ' ' << t.Cast()->Arity << '>';
    } else if (auto t = TMaybeType<TPointerType>(type)) {
        *Out << "<ptr ";
        PrintType(t.Cast()->PointeeType, level);
        PrintTypeAttrs(type);
        *Out << '>';
    } else if (auto t = TMaybeType<TReferenceType>(type)) {
        *Out << "<ref ";
        PrintType(t.Cast()->ReferencedType, level);
        PrintTypeAttrs(type);
        *Out << '>';
    } else if (auto t = TMaybeType<TNamedType>(type)) {
        if (Options.ShortNamedTypes.contains(t.Cast()->Name)) {
            PrintIdentifier(t.Cast()->Name);
        } else {
            *Out << "<named ";
            PrintIdentifier(t.Cast()->Name);
            *Out << ' ';
            PrintType(t.Cast()->UnderlyingType, level);
            PrintTypeAttrs(type);
            *Out << '>';
        }
    } else if (auto t = TMaybeType<TStructType>(type)) {
        PrintStructType(t.Cast(), level);
    } else {
        throw std::runtime_error("unsupported type for core print: " + std::string(type->TypeName()));
    }
}

void TPrinter::PrintIdentifier(const std::string& value) {
    if (value.find(' ') != std::string::npos) {
        *Out << '|' << value << '|';
    } else {
        *Out << value;
    }
}

void TPrinter::PrintString(const std::string& value, char quote) {
    *Out << quote;
    for (char ch : value) {
        switch (ch) {
            case '\n': *Out << "\\n"; break;
            case '\t': *Out << "\\t"; break;
            case '\\': *Out << "\\\\"; break;
            case '"': *Out << (quote == '"' ? "\\\"" : "\""); break;
            case '\'': *Out << (quote == '\'' ? "\\'" : "'"); break;
            default: *Out << ch;
        }
    }
    *Out << quote;
}

void TPrinter::PrintNumber(TNumberExpr& node) {
    if (TMaybeType<TBoolType>(node.Type)) {
        *Out << (node.IntValue ? "#t" : "#f");
    } else if (TMaybeType<TSymbolType>(node.Type)) {
        PrintString(std::string(1, static_cast<char>(node.IntValue)), '\'');
    } else if (node.IsFloat) {
        const auto oldPrecision = Out->precision();
        *Out << std::setprecision(std::numeric_limits<double>::max_digits10);
        *Out << node.FloatValue;
        Out->precision(oldPrecision);
    } else {
        *Out << node.IntValue;
    }
}

void TPrinter::PrintExprList(std::string_view head, const std::vector<TExprPtr>& items, int level) {
    *Out << '(' << head;
    for (const auto& item : items) {
        Separator(level + 1);
        PrintExpr(item, true, level + 1);
    }
    *Out << ')';
}

void TPrinter::PrintIfLike(std::string_view head, TExprPtr cond, TExprPtr thenBranch, TExprPtr elseBranch, int level) {
    *Out << '(' << head;
    Separator(level + 1);
    PrintExpr(cond, true, level + 1);
    Separator(level + 1);
    PrintExpr(thenBranch, true, level + 1);
    if (elseBranch) {
        Separator(level + 1);
        PrintExpr(elseBranch, true, level + 1);
    }
    *Out << ')';
}

void TPrinter::PrintArrayAssign(TArrayAssignExpr& node, int level) {
    *Out << "(= ";
    PrintIdentifier(node.Name);
    Separator(level + 1);
    PrintIndexVector(node.Indices, level + 1);
    Separator(level + 1);
    PrintExpr(node.Value, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintIndexVector(const std::vector<TExprPtr>& indices, int level) {
    *Out << '[';
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i != 0) {
            Separator(level + 1);
        }
        PrintExpr(indices[i], true, level + 1);
    }
    *Out << ']';
}

void TPrinter::PrintLet(TLetExpr& node, int level) {
    *Out << "(let (";
    for (size_t i = 0; i < node.Bindings.size(); ++i) {
        if (i != 0) {
            Separator(level + 2);
        }
        *Out << '(';
        PrintIdentifier(node.Bindings[i].Name);
        Space();
        PrintExpr(node.Bindings[i].Value, true, level + 2);
        *Out << ')';
    }
    *Out << ')';
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintWhile(TWhileStmtExpr& node, int level) {
    *Out << "(while";
    Separator(level + 1);
    PrintExpr(node.Cond, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintRepeat(TRepeatStmtExpr& node, int level) {
    *Out << "(repeat";
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.Cond, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintFor(TForStmtExpr& node, int level) {
    *Out << "(for ";
    PrintIdentifier(node.VarName);
    Separator(level + 1);
    PrintExpr(node.From, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.To, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.Step, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintTimes(TTimesStmtExpr& node, int level) {
    *Out << "(times";
    Separator(level + 1);
    PrintExpr(node.Count, true, level + 1);
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintVar(TVarStmt& node, int level) {
    *Out << "(var ";
    PrintIdentifier(node.Name);
    Space();
    PrintType(node.Type, level);
    for (const auto& [from, to] : node.Bounds) {
        Separator(level + 1);
        *Out << '[';
        PrintExpr(from, true, level + 1);
        Space();
        PrintExpr(to, true, level + 1);
        *Out << ']';
    }
    *Out << ')';
}

void TPrinter::PrintFun(TFunDecl& node, int level) {
    *Out << "(fun ";
    PrintIdentifier(node.Name);
    Space();
    PrintType(node.RetType, level);
    Space();
    *Out << '(';
    for (size_t i = 0; i < node.Params.size(); ++i) {
        if (i != 0) {
            Separator(level + 2);
        }
        PrintExpr(node.Params[i], true, level + 2);
    }
    *Out << ')';
    Space();
    *Out << '(';
    if (node.LastAssert) {
        *Out << "(expect_after ";
        PrintExpr(node.LastAssert, true, level + 2);
        *Out << ')';
    }
    *Out << ')';
    Separator(level + 1);
    PrintExpr(node.Body, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintCall(TCallExpr& node, int level) {
    *Out << "(call";
    Separator(level + 1);
    PrintExpr(node.Callee, true, level + 1);
    for (const auto& arg : node.Args) {
        Separator(level + 1);
        PrintExpr(arg, true, level + 1);
    }
    *Out << ')';
}

void TPrinter::PrintOutput(TOutputExpr& node, int level) {
    *Out << "(output";
    for (const auto& arg : node.Args) {
        Separator(level + 1);
        if (!arg.Width && !arg.Precision) {
            PrintExpr(arg.Expr, true, level + 1);
        } else {
            *Out << "(fmt ";
            PrintExpr(arg.Expr, true, level + 2);
            Space();
            PrintExpr(arg.Width, true, level + 2);
            if (arg.Precision) {
                Space();
                PrintExpr(arg.Precision, true, level + 2);
            }
            *Out << ')';
        }
    }
    *Out << ')';
}

void TPrinter::PrintMultiIndex(TMultiIndexExpr& node, int level) {
    *Out << "(index ";
    PrintIndexVector(node.Indices, level + 1);
    Separator(level + 1);
    PrintExpr(node.Collection, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintSlice(TSliceExpr& node, int level) {
    *Out << "(slice [";
    PrintExpr(node.Start, true, level + 1);
    if (node.End) {
        Space();
        PrintExpr(node.End, true, level + 1);
    }
    *Out << ']';
    Separator(level + 1);
    PrintExpr(node.Collection, true, level + 1);
    *Out << ')';
}

void TPrinter::PrintStructConstruct(TStructConstructExpr& node, int level) {
    *Out << "(struct (";
    auto structType = GetStructType(node.Type);
    for (size_t i = 0; i < node.Fields.size(); ++i) {
        if (i != 0) {
            Separator(level + 2);
        }
        *Out << '(';
        if (structType && i < structType->Fields.size()) {
            PrintIdentifier(structType->Fields[i].first);
        } else {
            PrintIdentifier("_" + std::to_string(i));
        }
        Space();
        PrintExpr(node.Fields[i], true, level + 2);
        *Out << ')';
    }
    *Out << "))";
}

std::shared_ptr<TStructType> TPrinter::GetStructType(TTypePtr type) {
    if (auto named = TMaybeType<TNamedType>(type)) {
        return TMaybeType<TStructType>(named.Cast()->UnderlyingType).Cast();
    }
    return TMaybeType<TStructType>(type).Cast();
}

void TPrinter::PrintFunctionType(const std::shared_ptr<TFunctionType>& type, int level) {
    *Out << "<fun ";
    PrintType(type->ReturnType, level);
    *Out << " (";
    for (size_t i = 0; i < type->ParamTypes.size(); ++i) {
        if (i != 0) {
            Space();
        }
        PrintType(type->ParamTypes[i], level);
    }
    *Out << ")>";
}

void TPrinter::PrintStructType(const std::shared_ptr<TStructType>& type, int level) {
    *Out << "<struct";
    for (const auto& [name, fieldType] : type->Fields) {
        *Out << " (";
        PrintIdentifier(name);
        Space();
        PrintType(fieldType, level);
        *Out << ')';
    }
    *Out << '>';
}

namespace NImpl {

struct TPrintExpr : public IVisitor {
    TPrintExpr(std::ostream* out, TPrinter& printer, TPrintFrame frame)
        : Out(out)
        , Printer(printer)
        , Frame(frame)
    {}

    void Visit(TIdentExpr& node) override {
        Printer.PrintIdentifier(node.Name);
    }

    void Visit(TAssignExpr& node) override {
        *Out << "(= ";
        Printer.PrintIdentifier(node.Name);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Value, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TArrayAssignExpr& node) override {
        Printer.PrintArrayAssign(node, Frame.Level);
    }

    void Visit(TNumberExpr& node) override {
        Printer.PrintNumber(node);
    }

    void Visit(TStringLiteralExpr& node) override {
        Printer.PrintString(node.Value, '"');
    }

    void Visit(TUnaryExpr& node) override {
        *Out << '(' << node.Operator.ToString() << ' ';
        Printer.PrintExpr(node.Operand, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TBinaryExpr& node) override {
        *Out << '(' << node.Operator.ToString() << ' ';
        Printer.PrintExpr(node.Left, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Right, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TBlockExpr& node) override {
        Printer.PrintExprList("block", node.Stmts, Frame.Level);
    }

    void Visit(TSeqExpr& node) override {
        Printer.PrintExprList("seq", node.Stmts, Frame.Level);
    }

    void Visit(TIfStmt& node) override {
        Printer.PrintIfLike("cond", node.Cond, node.Then, node.Else, Frame.Level);
    }

    void Visit(TIfExpr& node) override {
        Printer.PrintIfLike("if", node.Cond, node.Then, node.Else, Frame.Level);
    }

    void Visit(TLetExpr& node) override {
        Printer.PrintLet(node, Frame.Level);
    }

    void Visit(TWhileStmtExpr& node) override {
        Printer.PrintWhile(node, Frame.Level);
    }

    void Visit(TRepeatStmtExpr& node) override {
        Printer.PrintRepeat(node, Frame.Level);
    }

    void Visit(TForStmtExpr& node) override {
        Printer.PrintFor(node, Frame.Level);
    }

    void Visit(TTimesStmtExpr& node) override {
        Printer.PrintTimes(node, Frame.Level);
    }

    void Visit(TBreakStmt& /*node*/) override {
        *Out << "break";
    }

    void Visit(TContinueStmt& /*node*/) override {
        *Out << "continue";
    }

    void Visit(TVarStmt& node) override {
        Printer.PrintVar(node, Frame.Level);
    }

    void Visit(TVarsBlockExpr& node) override {
        Printer.PrintExprList("vars", node.Vars, Frame.Level);
    }

    void Visit(TFunDecl& node) override {
        Printer.PrintFun(node, Frame.Level);
    }

    void Visit(TCallExpr& node) override {
        Printer.PrintCall(node, Frame.Level);
    }

    void Visit(TAwaitExpr& node) override {
        *Out << "(await";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Operand, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TInputExpr& node) override {
        Printer.PrintExprList("input", node.Args, Frame.Level);
    }

    void Visit(TOutputExpr& node) override {
        Printer.PrintOutput(node, Frame.Level);
    }

    void Visit(TCastExpr& node) override {
        *Out << "(cast";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Operand, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintType(node.Type, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TIndexExpr& node) override {
        *Out << "(index";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Index, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Collection, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TMultiIndexExpr& node) override {
        Printer.PrintMultiIndex(node, Frame.Level);
    }

    void Visit(TSliceExpr& node) override {
        Printer.PrintSlice(node, Frame.Level);
    }

    void Visit(TUseExpr& node) override {
        *Out << "(use ";
        Printer.PrintString(node.ModuleName, '"');
        *Out << ')';
    }

    void Visit(TAssertStmt& node) override {
        *Out << "(assert";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Expr, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TFieldAccessExpr& node) override {
        *Out << "(field ";
        Printer.PrintIdentifier(node.FieldName);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Object, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TStructConstructExpr& node) override {
        Printer.PrintStructConstruct(node, Frame.Level);
    }

    void Visit(TFieldAssignExpr& node) override {
        *Out << "(field_assign";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Object, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintIdentifier(node.FieldName);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Value, true, Frame.Level + 1);
        *Out << ')';
    }

    void VisitOtherwise(TExpr& node) override {
        auto it = Printer.Options.NodePrinters.find(node.NodeName());
        if (it != Printer.Options.NodePrinters.end()) {
            it->second(node, Printer, Frame);
            return;
        }
        throw std::runtime_error("unsupported AST node for core print: " + std::string(node.NodeName()));
    }

    std::ostream* Out;
    TPrinter& Printer;
    TPrintFrame Frame;
};

} // namespace NImpl

void TPrinter::PrintExpr(TExprPtr expr, bool allowTypeWrap, int level) {
    if (!expr) {
        *Out << "nil";
        return;
    }

    if (allowTypeWrap && ShouldWrapType(expr)) {
        if (Options.Pretty && !ForceMultiline(expr) && FitsOneLine(expr, allowTypeWrap, level)) {
            PrintCompact(std::move(expr), allowTypeWrap, level);
            return;
        }
        *Out << "(: ";
        PrintExpr(expr, /*allowTypeWrap=*/false, level + 1);
        Separator(level + 1);
        PrintType(expr->Type, level + 1);
        *Out << ')';
        return;
    }

    if (Options.Pretty && !ForceMultiline(expr) && FitsOneLine(expr, allowTypeWrap, level)) {
        PrintCompact(std::move(expr), allowTypeWrap, level);
        return;
    }

    NImpl::TPrintExpr visitor(Out, *this, TPrintFrame{allowTypeWrap, level});
    expr->Accept(visitor);
}

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
