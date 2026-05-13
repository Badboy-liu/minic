#pragma once

struct NumberExpr;
struct FloatLiteralExpr;
struct StringExpr;
struct VariableExpr;
struct UnaryExpr;
struct BinaryExpr;
struct InitializerListExpr;
struct AssignExpr;
struct CallExpr;
struct IndexExpr;
struct MemberAccessExpr;
struct TernaryExpr;
struct CastExpr;
struct BuiltinVaStartExpr;
struct BuiltinVaArgExpr;
struct BuiltinVaEndExpr;
struct GenericExpr;
struct CompoundLiteralExpr;
struct StmtExpr;

class ExprVisitor {
public:
    virtual ~ExprVisitor() = default;
    virtual void visitNumberExpr(NumberExpr &node) = 0;
    virtual void visitFloatLiteralExpr(FloatLiteralExpr &node) = 0;
    virtual void visitStringExpr(StringExpr &node) = 0;
    virtual void visitVariableExpr(VariableExpr &node) = 0;
    virtual void visitUnaryExpr(UnaryExpr &node) = 0;
    virtual void visitBinaryExpr(BinaryExpr &node) = 0;
    virtual void visitInitializerListExpr(InitializerListExpr &node) = 0;
    virtual void visitAssignExpr(AssignExpr &node) = 0;
    virtual void visitCallExpr(CallExpr &node) = 0;
    virtual void visitIndexExpr(IndexExpr &node) = 0;
    virtual void visitMemberAccessExpr(MemberAccessExpr &node) = 0;
    virtual void visitTernaryExpr(TernaryExpr &node) = 0;
    virtual void visitCastExpr(CastExpr &node) = 0;
    virtual void visitBuiltinVaStartExpr(BuiltinVaStartExpr &node) = 0;
    virtual void visitBuiltinVaArgExpr(BuiltinVaArgExpr &node) = 0;
    virtual void visitBuiltinVaEndExpr(BuiltinVaEndExpr &node) = 0;
    virtual void visitGenericExpr(GenericExpr &node) = 0;
    virtual void visitCompoundLiteralExpr(CompoundLiteralExpr &node) = 0;
    virtual void visitStmtExpr(StmtExpr &node) = 0;
};
