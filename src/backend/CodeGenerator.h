#pragma once

#include "Ast.h"
#include "ExprVisitor.h"
#include "StmtVisitor.h"
#include "Target.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class CodeGenerator : public ExprVisitor, public StmtVisitor {
public:
    explicit CodeGenerator(TargetKind targetValue);
    std::string generate(const Program &program);
    std::string generate(const Program &program, bool emitEntryPointValue);
    void setSourceFileName(const std::string &name) { sourceFileName = name; }

private:
    struct RegisterSet {
        const char *r8;
        const char *r16;
        const char *r32;
        const char *r64;
    };
    struct WindowsAbiArgument {
        TypePtr type;
        bool inRegister = false;
        int registerIndex = -1;
        bool isFloatRegister = false;
        int homeOffset = 0;
        int stackSize = 0;
        bool hiddenReturnPointer = false;
    };

    void emitPrologue();
    void emitEntryPoint();
    void emitGlobals();
    void emitStringLiterals();
    void emitFunction(const Function &function);
    void emitTargetExternPrelude();
    void emitTargetEntryBody();
    void emitStatement(Stmt &stmt);
    bool emitSwitchJumpTable(const SwitchStmt &sw, const std::string &endLabel);
    void emitExpr(Expr &expr);
    void emitCallExpr(CallExpr &call);
    void emitWindowsCallExpr(CallExpr &call);
    void emitSystemVCallExpr(CallExpr &call);
    bool canTailCall() const;
    void emitTailCall(CallExpr &call);
    void emitAddress(Expr &expr);
    std::string globalAddressInitializer(const Expr &expr);

    // ExprVisitor
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

    // StmtVisitor
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
    bool supportsCurrentByValueStruct(const Type &type) const;
    bool supportsCurrentStructInitializer(const Type &type) const;
    bool isRegisterPassedStruct(const Type &type) const;
    bool isSystemVRegisterStruct(const Type &type) const;
    int systemVStructRegisterCount(const Type &type) const;
    bool usesHiddenSystemVReturnPointer(const Function &function) const;
    int alignStackSize(int size) const;
    std::vector<WindowsAbiArgument> buildWindowsAbiArguments(const std::vector<TypePtr> &parameterTypes, bool includeHiddenReturnPointer) const;
    int findHiddenReturnPointerLocalOffset(const Function &function) const;
    int findLargeStructCallResultLocalOffset(const Function &function) const;
    int currentFunctionFrameSize(const Function &function) const;
    void emitCopyBytes(const std::string &destAddressExpr, const std::string &srcAddressExpr, int size);
    void emitCopyStructValue(const Type &type, const std::string &destAddressExpr, const std::string &srcAddressExpr);
    void emitLoadStructValueToRax(Expr &expr);
    void emitStoreStructValueFromRax(const Type &type, const std::string &destAddressExpr);
    void emitGlobalStructInitializer(const GlobalVar &global, const InitializerListExpr *list);
    void emitZeroFillBytes(std::ostringstream &line, int count, bool &first) const;
    void emitGlobalStructScalars(std::vector<std::pair<std::string, std::string>> &values, const Type &type, const Expr &expr);
    void emitLocalStructInitializer(const DeclStmt &decl, const InitializerListExpr &list);
    void emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr);
    void emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list);
    void emitNestedArrayValues(const Type &arrayType, int baseOffset, const InitializerListExpr &list);
    void emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex);
    void emitStoreToLocalSlot(const Type &type, int addressOffset);
    void emitLoad(const Type &type);
    void emitStore(const Type &type);
    void emitFloatToBool();
    std::string floatLiteralLabel(double value);
    std::string dataDirectiveForSize(int size) const;
    long long evaluateStaticIntegerInitializer(const Expr &expr) const;
    int pointeeSize(const Type &type) const;
    const RegisterSet &argumentRegister(int index) const;
    bool usesWindowsAbi() const;
    int argumentRegisterCount() const;
    int floatArgumentRegisterCount() const;
    int stackArgumentOffset(int index) const;
    static std::string functionSymbol(const std::string &name);
    std::string stringLabel(const std::string &value);
    static std::string globalSymbol(const std::string &name);
    void emitLine(const std::string &text);
    std::string applyPeepholeRegAlloc(const std::string &assembly);
    void emitDataLine(std::string text);
    void emitBssLine(std::string text);
    void emitRdataLine(std::string text);
    std::string makeLabel(const std::string &prefix);
    static std::string formatStackAddress(int offset);
    void collectStaticLocals(const BlockStmt &block);
    void emitStaticLocals();

    // DWARF 调试信息
    struct DebugLineEntry {
        std::string label;  // 代码标签
        int line;           // 源代码行号
    };
    struct DebugFuncInfo {
        std::string name;
        std::string startLabel;
        std::string endLabel;
        int startLine;
    };
    std::vector<DebugLineEntry> debugLineEntries;
    std::vector<DebugFuncInfo> debugFuncInfos;
    std::vector<std::string> debugStrTable;
    void emitDwarfSections();
    void emitDebugLineSection();
    void emitDebugInfoSection();
    void emitDebugAbbrevSection();
    void emitDebugStrSection();
    static std::string encodeULEB128(int value);
    static std::string encodeSLEB128(int value);

    // PE 异常处理表（.pdata/.xdata）
    struct FunctionUnwindInfo {
        std::string startLabel;
        std::string endLabel;
        std::string xdataLabel;
        int frameSize;
        int prologueSize;  // 前言字节数
        bool hasVla;
    };
    std::vector<FunctionUnwindInfo> functionUnwindInfos;
    void emitPdataSection();
    void emitXdataSection();
    static int computePrologueSize(int frameSize);

    std::ostringstream out;
    std::vector<std::string> dataLines;
    std::vector<std::string> bssLines;
    std::vector<std::string> rdataLines;
    std::unordered_map<std::string, std::string> stringLabels;
    std::unordered_map<double, std::string> floatLabels;
    const Program *currentProgram = nullptr;
    std::string currentReturnLabel;
    std::string currentFunctionName;
    std::vector<std::string> loopContinueLabels;
    std::vector<std::string> loopBreakLabels;
    const TargetSpec &target;
    bool emitProgramEntryPoint = true;
    int activeHiddenReturnPointerOffset = 0;
    int activeLargeStructCallResultOffset = 0;
    int labelCounter = 0;
    bool functionHasVla = false;
    std::string sourceFileName;

    struct StaticLocalVar {
        TypePtr type;
        std::string symbolName;
        const Expr *init;  // 可能为 nullptr
    };
    std::vector<StaticLocalVar> staticLocalVars;
};
