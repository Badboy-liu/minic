#include "Ast.h"
#include "ExprVisitor.h"
#include "StmtVisitor.h"

// Expr accept 方法
void NumberExpr::accept(ExprVisitor &visitor) { visitor.visitNumberExpr(*this); }
void FloatLiteralExpr::accept(ExprVisitor &visitor) { visitor.visitFloatLiteralExpr(*this); }
void StringExpr::accept(ExprVisitor &visitor) { visitor.visitStringExpr(*this); }
void VariableExpr::accept(ExprVisitor &visitor) { visitor.visitVariableExpr(*this); }
void UnaryExpr::accept(ExprVisitor &visitor) { visitor.visitUnaryExpr(*this); }
void BinaryExpr::accept(ExprVisitor &visitor) { visitor.visitBinaryExpr(*this); }
void InitializerListExpr::accept(ExprVisitor &visitor) { visitor.visitInitializerListExpr(*this); }
void AssignExpr::accept(ExprVisitor &visitor) { visitor.visitAssignExpr(*this); }
void CallExpr::accept(ExprVisitor &visitor) { visitor.visitCallExpr(*this); }
void IndexExpr::accept(ExprVisitor &visitor) { visitor.visitIndexExpr(*this); }
void MemberAccessExpr::accept(ExprVisitor &visitor) { visitor.visitMemberAccessExpr(*this); }
void TernaryExpr::accept(ExprVisitor &visitor) { visitor.visitTernaryExpr(*this); }
void CastExpr::accept(ExprVisitor &visitor) { visitor.visitCastExpr(*this); }
void BuiltinVaStartExpr::accept(ExprVisitor &visitor) { visitor.visitBuiltinVaStartExpr(*this); }
void BuiltinVaArgExpr::accept(ExprVisitor &visitor) { visitor.visitBuiltinVaArgExpr(*this); }
void BuiltinVaEndExpr::accept(ExprVisitor &visitor) { visitor.visitBuiltinVaEndExpr(*this); }
void GenericExpr::accept(ExprVisitor &visitor) { visitor.visitGenericExpr(*this); }
void CompoundLiteralExpr::accept(ExprVisitor &visitor) { visitor.visitCompoundLiteralExpr(*this); }
void StmtExpr::accept(ExprVisitor &visitor) { visitor.visitStmtExpr(*this); }

// Stmt accept 方法
void ReturnStmt::accept(StmtVisitor &visitor) { visitor.visitReturnStmt(*this); }
void ExprStmt::accept(StmtVisitor &visitor) { visitor.visitExprStmt(*this); }
void DeclStmt::accept(StmtVisitor &visitor) { visitor.visitDeclStmt(*this); }
void BlockStmt::accept(StmtVisitor &visitor) { visitor.visitBlockStmt(*this); }
void IfStmt::accept(StmtVisitor &visitor) { visitor.visitIfStmt(*this); }
void WhileStmt::accept(StmtVisitor &visitor) { visitor.visitWhileStmt(*this); }
void ForStmt::accept(StmtVisitor &visitor) { visitor.visitForStmt(*this); }
void BreakStmt::accept(StmtVisitor &visitor) { visitor.visitBreakStmt(*this); }
void ContinueStmt::accept(StmtVisitor &visitor) { visitor.visitContinueStmt(*this); }
void DoWhileStmt::accept(StmtVisitor &visitor) { visitor.visitDoWhileStmt(*this); }
void SwitchStmt::accept(StmtVisitor &visitor) { visitor.visitSwitchStmt(*this); }
void GotoStmt::accept(StmtVisitor &visitor) { visitor.visitGotoStmt(*this); }
void LabelStmt::accept(StmtVisitor &visitor) { visitor.visitLabelStmt(*this); }
void StaticAssertStmt::accept(StmtVisitor &visitor) { visitor.visitStaticAssertStmt(*this); }
