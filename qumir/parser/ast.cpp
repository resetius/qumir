#include "ast.h"

namespace NQumir {
namespace NAst {

void TIdentExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TArrayAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TNumberExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TStringLiteralExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TUnaryExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBinaryExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBlockExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TIfExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TWhileStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TRepeatStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TForStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TTimesStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBreakStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TContinueStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TReturnExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TVarStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TVarsBlockExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFunDecl::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCallExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAwaitExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TInputExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TOutputExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCastExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBitcastExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TIndexExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TMultiIndexExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TSliceExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TUseExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAssertStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TTypeDeclStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFieldAccessExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TStructConstructExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFieldAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TRetainExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TOwnLiteralExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TMoveExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBorrowExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TDestroyExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TReplaceExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCleanupExitExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TGlobalCleanupExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }

} // namespace NAst
} // namespace NQumir
