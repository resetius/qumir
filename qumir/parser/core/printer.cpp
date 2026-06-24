#include "printer.h"

#include <iomanip>
#include <limits>
#include <map>
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
        if (
            TMaybeNode<TFunDecl>(expr) ||
            TMaybeNode<TCastExpr>(expr) ||
            TMaybeNode<TBitcastExpr>(expr))
        {
            return false;
        }
        if (TMaybeNode<TBlockExpr>(expr) && TMaybeType<TVoidType>(expr->Type)) {
            return false; // void blocks are statements — don't annotate type
        }
        return true;
    }
    return TMaybeType<TNamedType>(expr->Type)
        || TMaybeNode<TStructConstructExpr>(expr)
        || IsNonDefaultIntegerLiteral(expr);
}

bool TPrinter::IsNonDefaultIntegerLiteral(const TExprPtr& expr) const {
    auto number = TMaybeNode<TNumberExpr>(expr);
    if (!number || number.Cast()->IsFloat()) {
        return false;
    }
    auto integerType = TMaybeType<TIntegerType>(expr->Type);
    return integerType && integerType.Cast()->Kind != TIntegerType::I64;
}

bool TPrinter::HasPrintableTypeAttrs(TTypePtr type) const {
    return type && (type->Template || !type->Readable || !type->Mutable);
}

void TPrinter::PrintTypeAttrs(TTypePtr type) {
    if (!HasPrintableTypeAttrs(type)) {
        return;
    }
    *Out << " (";
    bool hasPrevious = false;
    auto printAttribute = [&](std::string_view attribute) {
        if (hasPrevious) {
            *Out << ' ';
        }
        *Out << attribute;
        hasPrevious = true;
    };
    if (type->Template) {
        printAttribute("template");
    }
    if (type->Readable && !type->Mutable) {
        printAttribute("readonly");
    } else if (!type->Readable && type->Mutable) {
        printAttribute("writeonly");
    } else if (!type->Readable && !type->Mutable) {
        throw std::runtime_error("type cannot be both unreadable and immutable");
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
        *Out << "<named ";
        PrintIdentifier(t.Cast()->Name);
        if (!Options.ShortNamedTypes.contains(t.Cast()->Name) && t.Cast()->UnderlyingType) {
            *Out << ' ';
            PrintType(t.Cast()->UnderlyingType, level);
        }
        PrintTypeAttrs(type);
        *Out << '>';
    } else if (auto t = TMaybeType<TStructType>(type)) {
        PrintStructType(t.Cast(), level);
    } else {
        throw std::runtime_error("unsupported type for core print: " + std::string(type->TypeName()));
    }
}

void TPrinter::PrintIdentifier(const std::string& value) {
    const bool needsQuoting = value.find(' ') != std::string::npos
                           || value.find('.') != std::string::npos;
    if (needsQuoting) {
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
    } else if (node.IsFloat()) {
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
    if (node.Init) {
        *Out << " =";
        Space();
        PrintExpr(node.Init, true, level + 1);
    } else {
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
    }
    *Out << ')';
}

void TPrinter::PrintFun(TFunDecl& node, int level) {
    *Out << "(fun ";
    PrintIdentifier(node.Name);
    *Out << " (";
    for (size_t i = 0; i < node.Params.size(); ++i) {
        if (i != 0) {
            Separator(level + 2);
        }
        PrintExpr(node.Params[i], true, level + 2);
    }
    *Out << ')';
    if (!TMaybeType<TVoidType>(node.RetType)) {
        *Out << " ->";
        Space();
        PrintType(node.RetType, level);
    }
    if (node.LastAssert) {
        Space();
        *Out << "(attrs";
        Separator(level + 2);
        *Out << "(expect_after";
        Space();
        PrintExpr(node.LastAssert, true, level + 2);
        *Out << ')';
        *Out << ')';
    }
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
    PrintExpr(node.Collection, true, level + 1);
    Separator(level + 1);
    PrintIndexVector(node.Indices, level + 1);
    *Out << ')';
}

void TPrinter::PrintSlice(TSliceExpr& node, int level) {
    *Out << "(slice ";
    PrintExpr(node.Collection, true, level + 1);
    Separator(level + 1);
    *Out << '[';
    PrintExpr(node.Start, true, level + 1);
    if (node.End) {
        Space();
        PrintExpr(node.End, true, level + 1);
    }
    *Out << "])";
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

namespace {

using TNamedTypeMap = std::map<std::string, std::shared_ptr<TNamedType>>;

void CollectFromType(const TTypePtr& type, TNamedTypeMap& out) {
    if (!type) return;
    if (auto t = TMaybeType<TNamedType>(type)) {
        auto named = t.Cast();
        if (named->UnderlyingType && !out.contains(named->Name)) {
            out[named->Name] = named;
            CollectFromType(named->UnderlyingType, out);
        }
        return;
    }
    if (auto t = TMaybeType<TFunctionType>(type)) {
        for (const auto& p : t.Cast()->ParamTypes) CollectFromType(p, out);
        CollectFromType(t.Cast()->ReturnType, out);
    } else if (auto t = TMaybeType<TFutureType>(type)) {
        CollectFromType(t.Cast()->ResultType, out);
    } else if (auto t = TMaybeType<TArrayType>(type)) {
        CollectFromType(t.Cast()->ElementType, out);
    } else if (auto t = TMaybeType<TPointerType>(type)) {
        CollectFromType(t.Cast()->PointeeType, out);
    } else if (auto t = TMaybeType<TReferenceType>(type)) {
        CollectFromType(t.Cast()->ReferencedType, out);
    } else if (auto t = TMaybeType<TStructType>(type)) {
        for (const auto& [name, fieldType] : t.Cast()->Fields) {
            CollectFromType(fieldType, out);
        }
    }
}

void CollectTypesFromExpr(const TExprPtr& expr, TNamedTypeMap& out) {
    if (!expr) return;
    CollectFromType(expr->Type, out);
    if (auto fd = TMaybeNode<TFunDecl>(expr)) {
        CollectFromType(fd.Cast()->RetType, out);
        for (const auto& p : fd.Cast()->Params) {
            CollectFromType(p->Type, out);
        }
    }
    for (const auto& child : expr->Children()) {
        CollectTypesFromExpr(child, out);
    }
}

std::string_view CleanupExitKindName(ECleanupExitKind kind) {
    switch (kind) {
        case ECleanupExitKind::Break:
            return "break";
        case ECleanupExitKind::Continue:
            return "continue";
        case ECleanupExitKind::Return:
            return "return";
    }
    throw std::runtime_error("unknown cleanup exit kind");
}

} // anonymous namespace

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
        if (Frame.Level == 0) {
            TNamedTypeMap collected;
            for (const auto& stmt : node.Stmts) {
                CollectTypesFromExpr(stmt, collected);
            }
            for (const auto& [name, _] : collected) {
                Printer.Options.ShortNamedTypes.insert(name);
            }
            *Out << "(block";
            // Pass 1: use statements
            for (const auto& stmt : node.Stmts) {
                if (!TMaybeNode<TUseExpr>(stmt)) continue;
                Printer.Separator(1);
                Printer.PrintExpr(stmt, true, 1);
            }
            // Pass 2: locally declared types (not imported from modules)
            for (const auto& [name, namedType] : collected) {
                if (namedType->Reference.has_value()) continue;
                Printer.Separator(1);
                *Out << "(type ";
                Printer.PrintIdentifier(name);
                Printer.Space();
                Printer.PrintType(namedType->UnderlyingType, 1);
                *Out << ')';
            }
            // Pass 3: everything else
            for (const auto& stmt : node.Stmts) {
                if (TMaybeNode<TTypeDeclStmt>(stmt)) continue;
                if (TMaybeNode<TUseExpr>(stmt)) continue;
                Printer.Separator(1);
                Printer.PrintExpr(stmt, true, 1);
            }
            *Out << ')';
        } else {
            Printer.PrintExprList("block", node.Stmts, Frame.Level);
        }
    }

    void Visit(TTypeDeclStmt& node) override {
        auto named = TMaybeType<TNamedType>(node.Type);
        *Out << "(type ";
        Printer.PrintIdentifier(named.Cast()->Name);
        Printer.Space();
        Printer.PrintType(named.Cast()->UnderlyingType, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TIfExpr& node) override {
        Printer.PrintIfLike("if", node.Cond, node.Then, node.Else, Frame.Level);
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

    void Visit(TReturnExpr& node) override {
        if (!node.Value) {
            *Out << "(return)";
            return;
        }
        *Out << "(return";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Value, true, Frame.Level + 1);
        *Out << ')';
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

    void Visit(TBitcastExpr& node) override {
        *Out << "(bitcast";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Operand, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintType(node.Type, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TIndexExpr& node) override {
        *Out << "(index";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Collection, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Index, true, Frame.Level + 1);
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
        *Out << "(field";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Object, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintIdentifier(node.FieldName);
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

    void Visit(TRetainExpr& node) override {
        PrintLifetimeUnary("retain", node.Value);
    }

    void Visit(TOwnLiteralExpr& node) override {
        PrintLifetimeUnary("own-literal", node.Value);
    }

    void Visit(TMoveExpr& node) override {
        PrintLifetimeUnary("move", node.Value);
    }

    void Visit(TBorrowExpr& node) override {
        PrintLifetimeUnary("borrow", node.Value);
    }

    void Visit(TDestroyExpr& node) override {
        *Out << "(destroy";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Value, true, Frame.Level + 1);
        if (node.Aux) {
            Printer.Separator(Frame.Level + 1);
            Printer.PrintExpr(node.Aux, true, Frame.Level + 1);
        }
        *Out << ')';
    }

    void Visit(TReplaceExpr& node) override {
        *Out << "(replace";
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Target, true, Frame.Level + 1);
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(node.Value, true, Frame.Level + 1);
        *Out << ')';
    }

    void Visit(TCleanupExitExpr& node) override {
        *Out << "(cleanup-exit";
        Printer.Separator(Frame.Level + 1);
        *Out << '(' << CleanupExitKindName(node.Kind);
        if (node.Value) {
            Printer.Separator(Frame.Level + 2);
            Printer.PrintExpr(node.Value, true, Frame.Level + 2);
        }
        *Out << ')';
        PrintCleanups(node.Cleanups);
        *Out << ')';
    }

    void Visit(TGlobalCleanupExpr& node) override {
        *Out << "(cleanup-global";
        PrintCleanups(node.Cleanups);
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

    void PrintLifetimeUnary(std::string_view head, const TExprPtr& value) {
        *Out << '(' << head;
        Printer.Separator(Frame.Level + 1);
        Printer.PrintExpr(value, true, Frame.Level + 1);
        *Out << ')';
    }

    void PrintCleanups(const std::vector<TExprPtr>& cleanups) {
        for (const auto& cleanup : cleanups) {
            Printer.Separator(Frame.Level + 1);
            Printer.PrintExpr(cleanup, true, Frame.Level + 1);
        }
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
