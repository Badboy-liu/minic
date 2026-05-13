#pragma once
#include "IRModule.h"
#include "frontend/Ast.h"
#include "frontend/ExprVisitor.h"
#include "frontend/StmtVisitor.h"
#include <unordered_map>
#include <vector>

namespace ir {

class IRLowering : public ExprVisitor, public StmtVisitor {
public:
    IRLowering();

    // 将整个 Program 降低到 IRModule
    std::unique_ptr<IRModule> lowerProgram(const Program &program);

private:
    std::unique_ptr<IRModule> module;
    IRFunction *currentFunc = nullptr;
    IRBasicBlock *currentBB = nullptr;

    // 当前求值结果（visit 后设置）
    IRValue *lastValue = nullptr;

    // 变量名 → alloca 地址映射
    std::vector<std::unordered_map<std::string, IRValue *>> scopeStack;

    // 标签名 → 基本块映射（goto/label）
    std::unordered_map<std::string, IRBasicBlock *> labelMap;

    // break/continue 目标栈
    std::vector<IRBasicBlock *> breakTargets;
    std::vector<IRBasicBlock *> continueTargets;

    // switch 默认块
    std::vector<IRBasicBlock *> switchDefaultTargets;

    // 全局变量名 → IRGlobalVariable 映射
    std::unordered_map<std::string, IRGlobalVariable *> globalMap;

    // 字符串常量池
    std::unordered_map<std::string, IRGlobalVariable *> stringPool;

    // 函数映射
    std::unordered_map<std::string, IRFunction *> functionMap;

    // 工具方法
    IRType convertType(const TypePtr &type);
    IRValue *emitAlloca(IRType ty, const std::string &name = "");
    void emitStore(IRValue *val, IRValue *ptr);
    IRValue *emitLoad(IRValue *ptr, const std::string &name = "");
    IRValue *emitGEP(IRValue *base, IRValue *idx, const std::string &name = "");
    std::unique_ptr<IRInstruction> emitBinaryOp(BinaryOp op, IRValue *lhs, IRValue *rhs, const TypePtr &astType);
    IRValue *emitCast(IRValue *val, const TypePtr &fromType, const TypePtr &toType);
    IRBasicBlock *createBlock(const std::string &name);
    void setInsertBlock(IRBasicBlock *bb);
    void emitBr(IRBasicBlock *target);
    void emitCondBr(IRValue *cond, IRBasicBlock *trueBB, IRBasicBlock *falseBB);
    void emitRet(IRValue *val = nullptr);

    // 查找变量
    IRValue *lookupVar(const std::string &name);
    void pushScope();
    void popScope();

    // 全局变量和函数处理
    void lowerGlobalVar(const GlobalVar &gv);
    void lowerFunction(const Function &func);

    // 临时变量名生成
    int tempCounter = 0;
    std::string nextTemp(const std::string &prefix = "tmp");

    // === ExprVisitor ===
    void visitNumberExpr(NumberExpr &node) override;
    void visitFloatLiteralExpr(FloatLiteralExpr &node) override;
    void visitStringExpr(StringExpr &node) override;
    void visitVariableExpr(VariableExpr &node) override;
    void visitUnaryExpr(UnaryExpr &node) override;
    void visitBinaryExpr(BinaryExpr &node) override;
    void visitInitializerListExpr(InitializerListExpr &node) override;
    void visitAssignExpr(AssignExpr &node) override;
    void visitCallExpr(CallExpr &node) override;
    void visitIndexExpr(IndexExpr &node) override;
    void visitMemberAccessExpr(MemberAccessExpr &node) override;
    void visitTernaryExpr(TernaryExpr &node) override;
    void visitCastExpr(CastExpr &node) override;
    void visitBuiltinVaStartExpr(BuiltinVaStartExpr &node) override;
    void visitBuiltinVaArgExpr(BuiltinVaArgExpr &node) override;
    void visitBuiltinVaEndExpr(BuiltinVaEndExpr &node) override;
    void visitGenericExpr(GenericExpr &node) override;
    void visitCompoundLiteralExpr(CompoundLiteralExpr &node) override;
    void visitStmtExpr(StmtExpr &node) override;

    // === StmtVisitor ===
    void visitReturnStmt(ReturnStmt &node) override;
    void visitExprStmt(ExprStmt &node) override;
    void visitDeclStmt(DeclStmt &node) override;
    void visitBlockStmt(BlockStmt &node) override;
    void visitIfStmt(IfStmt &node) override;
    void visitWhileStmt(WhileStmt &node) override;
    void visitForStmt(ForStmt &node) override;
    void visitBreakStmt(BreakStmt &node) override;
    void visitContinueStmt(ContinueStmt &node) override;
    void visitDoWhileStmt(DoWhileStmt &node) override;
    void visitSwitchStmt(SwitchStmt &node) override;
    void visitGotoStmt(GotoStmt &node) override;
    void visitLabelStmt(LabelStmt &node) override;
    void visitStaticAssertStmt(StaticAssertStmt &node) override;
};

} // namespace ir
