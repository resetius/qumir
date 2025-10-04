#pragma once

#include <string>
#include <memory>
#include <vector>
#include <sstream>

#include <qumir/location.h>

#include "type.h"
#include "operator.h"

namespace NQumir {
namespace NAst {

struct TExpr;
using TExprPtr = std::shared_ptr<TExpr>;

template<typename T>
struct TMaybeNode {
    TMaybeNode(TExprPtr node);

    std::shared_ptr<T> Cast() {
        return std::static_pointer_cast<T>(Node);
    }

    TExprPtr Node;
    operator bool() const { return Node != nullptr; }
};

struct TExpr {
    TLocation Loc;
    TTypePtr Type = nullptr;

    TExpr() = default;
    TExpr(TLocation loc)
        : Loc(std::move(loc))
    { }
    TExpr(TLocation loc, TTypePtr type)
        : Loc(std::move(loc))
        , Type(std::move(type))
    { }
    virtual ~TExpr() = default;
    virtual std::vector<TExprPtr> Children() const {
        return {};
    }
    virtual const std::string_view NodeName() const = 0;
    virtual const std::string ToString() const {
        return std::string(NodeName());
    }
};

template<typename T>
inline TMaybeNode<T>::TMaybeNode(TExprPtr node)
    : Node(node && std::string_view(T::NodeId) == node->NodeName()
        ? std::move(node)
        : nullptr)
{ }

struct TIdentExpr : TExpr {
    static constexpr const char* NodeId = "Ident";

    std::string Name;
    explicit TIdentExpr(TLocation loc, std::string n)
        : TExpr(std::move(loc))
        , Name(std::move(n))
    { }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return "$" + Name;
    }
};

struct TAssignExpr : TExpr {
    static constexpr const char* NodeId = "Assign";

    std::string Name;
    TExprPtr Value;
    TAssignExpr(TLocation loc, std::string n, TExprPtr v)
        : TExpr(std::move(loc))
        , Name(std::move(n))
        , Value(std::move(v))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Value };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return std::string("$") + Name + " =";
    }
};

struct TNumberExpr : TExpr {
    static constexpr const char* NodeId = "Number";

    union {
        int64_t IntValue;
        double FloatValue;
    };

    bool IsFloat = false;
    explicit TNumberExpr(TLocation loc, bool v)
        : TExpr(std::move(loc)), IntValue(v)
    { }
    explicit TNumberExpr(TLocation loc, int64_t v)
        : TExpr(std::move(loc)), IntValue(v)
    { }
    explicit TNumberExpr(TLocation loc, double v)
        : TExpr(std::move(loc)), FloatValue(v), IsFloat(true)
    { }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return IsFloat ? std::to_string(FloatValue) : std::to_string(IntValue);
    }
};

struct TUnaryExpr : TExpr {
    static constexpr const char* NodeId = "Unary";

    TOperator Operator;
    TExprPtr Operand;
    TUnaryExpr(TLocation loc, TOperator o, TExprPtr e)
        : TExpr(std::move(loc))
        , Operator(o)
        , Operand(std::move(e))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Operand };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return Operator.ToString();
    }
};

struct TBinaryExpr : TExpr {
    static constexpr const char* NodeId = "Binary";

    TOperator Operator;
    TExprPtr Left;
    TExprPtr Right;
    TBinaryExpr(TLocation loc, TOperator o, TExprPtr l, TExprPtr r)
        : TExpr(std::move(loc))
        , Operator(o)
        , Left(std::move(l))
        , Right(std::move(r))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Left, Right };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return Operator.ToString();
    }
};

struct TBlockExpr : TExpr {
    static constexpr const char* NodeId = "Block";

    std::vector<TExprPtr> Stmts;
    int32_t Scope = -1; // filled in by name resolver, 0 - root scope, -1 - unscoped

    explicit TBlockExpr(TLocation loc, std::vector<TExprPtr> s)
        : TExpr(std::move(loc))
        , Stmts(std::move(s))
    { }

    std::vector<TExprPtr> Children() const override {
        std::vector<TExprPtr> result;
        result.reserve(Stmts.size());
        for (const auto& stmt : Stmts) {
            result.push_back(stmt);
        }
        return result;
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

struct TIfExpr : TExpr {
    static constexpr const char* NodeId = "If";

    TExprPtr Cond;
    TExprPtr Then;
    TExprPtr Else;
    TIfExpr(TLocation loc, TExprPtr c, TExprPtr t, TExprPtr e)
        : TExpr(std::move(loc))
        , Cond(std::move(c))
        , Then(std::move(t))
        , Else(std::move(e))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Cond, Then, Else };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

struct TLoopStmtExpr : TExpr {
    static constexpr const char* NodeId = "Loop";

    TExprPtr Init; // like for (i = 0 /* init */; i < 10; i = i + 1)
    TExprPtr PerCond; // like for (i = 0; i < 10 /* per-iteration condition */; i = i + 1)
    TExprPtr PostCond; // like do ... while (post-condition)
    TExprPtr PreBody; // executed before each iteration, like for (...; ...; pre-body) ...
    TExprPtr Body; // main loop body

    TLoopStmtExpr(TLocation loc, TExprPtr i, TExprPtr p, TExprPtr b, TExprPtr pb, TExprPtr pc)
        : TExpr(std::move(loc))
        , Init(std::move(i))
        , PerCond(std::move(p))
        , PostCond(std::move(pc))
        , PreBody(std::move(pb))
        , Body(std::move(b))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Init, PerCond, PostCond, PreBody, Body };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

struct TBreakStmt : TExpr {
    static constexpr const char* NodeId = "Break";

    explicit TBreakStmt(TLocation loc)
        : TExpr(std::move(loc))
    { }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

struct TContinueStmt : TExpr {
    static constexpr const char* NodeId = "Continue";

    explicit TContinueStmt(TLocation loc)
        : TExpr(std::move(loc))
    { }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

struct TVarStmt : TExpr {
    static constexpr const char* NodeId = "Var";

    std::string Name;

    TVarStmt(TLocation loc, std::string name, NAst::TTypePtr type)
        : TExpr(std::move(loc), std::move(type))
        , Name(std::move(name))
    { }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        return std::string(NodeName()) + " $" + Name;
    }
};

using TParam = std::shared_ptr<TVarStmt>;

struct TFunDecl : TExpr {
    static constexpr const char* NodeId = "FunDecl";

    std::string Name;
    std::vector<TParam> Params;
    std::shared_ptr<TBlockExpr> Body;
    NAst::TTypePtr RetType; // ret type different from TExpr::Type which is the function value type
    TFunDecl(TLocation loc, std::string name, std::vector<TParam> args, std::shared_ptr<TBlockExpr> body, NAst::TTypePtr type)
        : TExpr(std::move(loc))
        , Name(std::move(name))
        , Params(std::move(args))
        , Body(std::move(body))
        , RetType(std::move(type))
    { }

    std::vector<TExprPtr> Children() const override {
        return { Body };
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }

    const std::string ToString() const override {
        auto s = std::string(NodeName()) + " $" + Name;
        s += " (";
        for (size_t i = 0; i < Params.size(); ++i) {
            s += "$" + Params[i]->Name;
            if (!Type && Params[i]->Type) {
                std::ostringstream str; str << *Params[i]->Type;
                s += ": " + str.str();
            }
            if (i < Params.size() - 1) {
                s += ", ";
            }
        }
        s += ")";
        if (!Type && RetType) {
            std::ostringstream str; str << *RetType;
            s += " -> " + str.str();
        }
        return s;
    }
};

struct TCallExpr : TExpr {
    static constexpr const char* NodeId = "Call";

    TExprPtr Callee; // should evaluate to a function
    std::vector<TExprPtr> Args;
    TCallExpr(TLocation loc, TExprPtr c, std::vector<TExprPtr> a)
        : TExpr(std::move(loc))
        , Callee(std::move(c))
        , Args(std::move(a))
    { }

    std::vector<TExprPtr> Children() const override {
        std::vector<TExprPtr> result;
        result.reserve(1 + Args.size());
        result.push_back(Callee);
        for (const auto& arg : Args) {
            result.push_back(arg);
        }
        return result;
    }

    const std::string_view NodeName() const override {
        return NodeId;
    }
};

std::ostream& operator<<(std::ostream& os, const TExpr& expr);

} // namespace NAst
} // namespace NQumir