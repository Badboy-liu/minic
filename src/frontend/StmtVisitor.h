#pragma once

struct ReturnStmt;
struct ExprStmt;
struct DeclStmt;
struct BlockStmt;
struct IfStmt;
struct WhileStmt;
struct ForStmt;
struct BreakStmt;
struct ContinueStmt;
struct DoWhileStmt;
struct SwitchStmt;
struct GotoStmt;
struct LabelStmt;
struct StaticAssertStmt;

class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;
    virtual void visitReturnStmt(ReturnStmt &node) = 0;
    virtual void visitExprStmt(ExprStmt &node) = 0;
    virtual void visitDeclStmt(DeclStmt &node) = 0;
    virtual void visitBlockStmt(BlockStmt &node) = 0;
    virtual void visitIfStmt(IfStmt &node) = 0;
    virtual void visitWhileStmt(WhileStmt &node) = 0;
    virtual void visitForStmt(ForStmt &node) = 0;
    virtual void visitBreakStmt(BreakStmt &node) = 0;
    virtual void visitContinueStmt(ContinueStmt &node) = 0;
    virtual void visitDoWhileStmt(DoWhileStmt &node) = 0;
    virtual void visitSwitchStmt(SwitchStmt &node) = 0;
    virtual void visitGotoStmt(GotoStmt &node) = 0;
    virtual void visitLabelStmt(LabelStmt &node) = 0;
    virtual void visitStaticAssertStmt(StaticAssertStmt &node) = 0;
};
