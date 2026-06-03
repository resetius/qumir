#include <gtest/gtest.h>

#include <qumir/location.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/core/parser.h>
#include <qumir/parser/core/printer.h>
#include <sstream>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NAst::NCore;

namespace {

// Custom node: (tag "label" <child>)
struct TTagExpr : TExpr {
    static constexpr const char* NodeId = "Tag";

    std::string Label;
    TExprPtr Child;

    TTagExpr(std::string label, TExprPtr child)
        : Label(std::move(label))
        , Child(std::move(child))
    {}

    const std::string_view NodeName() const override { return NodeId; }

    std::vector<TExprPtr> Children() const override { return {Child}; }

    void Accept(IVisitor& visitor) override { visitor.VisitOtherwise(*this); }
};

// Custom node: (pair <left> <right>)
struct TPairExpr : TExpr {
    static constexpr const char* NodeId = "Pair";

    TExprPtr Left;
    TExprPtr Right;

    TPairExpr(TExprPtr left, TExprPtr right)
        : Left(std::move(left))
        , Right(std::move(right))
    {}

    const std::string_view NodeName() const override { return NodeId; }

    std::vector<TExprPtr> Children() const override { return {Left, Right}; }

    void Accept(IVisitor& visitor) override { visitor.VisitOtherwise(*this); }
};

TPrintOptions MakeOptions(bool pretty = false) {
    TPrintOptions opts;
    opts.Pretty = pretty;
    opts.NodePrinters[TTagExpr::NodeId] = [](TExpr& node, TPrinter& p, TPrintFrame frame) {
        auto& tag = static_cast<TTagExpr&>(node);
        p.GetOut() << "(tag \"" << tag.Label << "\"";
        p.Separator(frame.Level + 1);
        p.PrintExpr(tag.Child, frame.AllowTypeWrap, frame.Level + 1);
        p.GetOut() << ')';
    };
    opts.NodePrinters[TPairExpr::NodeId] = [](TExpr& node, TPrinter& p, TPrintFrame frame) {
        auto& pair = static_cast<TPairExpr&>(node);
        p.GetOut() << "(pair";
        p.Separator(frame.Level + 1);
        p.PrintExpr(pair.Left, frame.AllowTypeWrap, frame.Level + 1);
        p.Separator(frame.Level + 1);
        p.PrintExpr(pair.Right, frame.AllowTypeWrap, frame.Level + 1);
        p.GetOut() << ')';
    };
    return opts;
}

TExprPtr MakeIdent(std::string name) {
    return std::make_shared<TIdentExpr>(TLocation{}, std::move(name));
}

} // namespace

TEST(PrinterCustomNodes, TagCompact) {
    auto expr = std::make_shared<TTagExpr>("hello", MakeIdent("x"));
    EXPECT_EQ(PrintAst(expr, MakeOptions()), "(tag \"hello\" x)");
}

TEST(PrinterCustomNodes, PairCompact) {
    auto expr = std::make_shared<TPairExpr>(MakeIdent("a"), MakeIdent("b"));
    EXPECT_EQ(PrintAst(expr, MakeOptions()), "(pair a b)");
}

TEST(PrinterCustomNodes, NestedCustomNodes) {
    auto inner = std::make_shared<TPairExpr>(MakeIdent("x"), MakeIdent("y"));
    auto outer = std::make_shared<TTagExpr>("wrap", inner);
    EXPECT_EQ(PrintAst(outer, MakeOptions()), "(tag \"wrap\" (pair x y))");
}

TEST(PrinterCustomNodes, UnknownNodeThrows) {
    struct TUnknown : TExpr {
        const std::string_view NodeName() const override { return "Unknown"; }
        void Accept(IVisitor& v) override { v.VisitOtherwise(*this); }
    };
    auto expr = std::make_shared<TUnknown>();
    TPrintOptions opts;
    EXPECT_THROW(PrintAst(expr, opts), std::runtime_error);
}

namespace {

TParser MakeParser() {
    TParser p;
    p.NodeParsers["tag"] = [](IParseHandle& h, TLocation loc) -> TAstTask {
        auto tok = h.Next();
        if (tok.Type != TToken::String) {
            co_return IParseHandle::MakeError(tok, "expected string label for tag");
        }
        auto child = co_await h.Expr();
        co_await h.Take(')');
        co_return std::make_shared<TTagExpr>(tok.Name, std::move(child));
    };
    p.NodeParsers["pair"] = [](IParseHandle& h, TLocation loc) -> TAstTask {
        auto left = co_await h.Expr();
        auto right = co_await h.Expr();
        co_await h.Take(')');
        co_return std::make_shared<TPairExpr>(std::move(left), std::move(right));
    };
    return p;
}

TExprPtr Parse(const std::string& src) {
    std::istringstream in(src);
    TTokenStream ts(in);
    auto result = MakeParser().Parse(ts);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().ToString());
    return result.value_or(nullptr);
}

} // namespace

TEST(ParserCustomNodes, TagSimple) {
    auto expr = Parse(R"((tag "hello" x))");
    ASSERT_NE(expr, nullptr);
    auto* tag = dynamic_cast<TTagExpr*>(expr.get());
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->Label, "hello");
    auto* child = dynamic_cast<TIdentExpr*>(tag->Child.get());
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->Name, "x");
}

TEST(ParserCustomNodes, PairSimple) {
    auto expr = Parse("(pair a b)");
    ASSERT_NE(expr, nullptr);
    auto* pair = dynamic_cast<TPairExpr*>(expr.get());
    ASSERT_NE(pair, nullptr);
    EXPECT_EQ(dynamic_cast<TIdentExpr*>(pair->Left.get())->Name, "a");
    EXPECT_EQ(dynamic_cast<TIdentExpr*>(pair->Right.get())->Name, "b");
}

TEST(ParserCustomNodes, NestedCustomNodes) {
    auto expr = Parse(R"((tag "wrap" (pair x y)))");
    ASSERT_NE(expr, nullptr);
    auto* tag = dynamic_cast<TTagExpr*>(expr.get());
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->Label, "wrap");
    auto* pair = dynamic_cast<TPairExpr*>(tag->Child.get());
    ASSERT_NE(pair, nullptr);
}

TEST(ParserCustomNodes, UnknownFormFallsBackToBinaryOp) {
    auto expr = Parse("(+ a b)");
    ASSERT_NE(expr, nullptr);
    EXPECT_NE(dynamic_cast<TBinaryExpr*>(expr.get()), nullptr);
}

TEST(ParserCustomNodes, UnknownFormErrors) {
    std::istringstream in("(mystery a b c)");
    TTokenStream ts(in);
    auto result = MakeParser().Parse(ts);
    EXPECT_FALSE(result.has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
