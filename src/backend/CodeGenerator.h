#pragma once

#include "Ast.h"
#include "Target.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class CodeGenerator {
public:
    explicit CodeGenerator(TargetKind targetValue);
    std::string generate(const Program &program);
    std::string generate(const Program &program, bool emitEntryPointValue);

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
    void emitStatement(const Stmt &stmt);
    void emitExpr(const Expr &expr);
    void emitCallExpr(const CallExpr &call);
    void emitWindowsCallExpr(const CallExpr &call);
    void emitSystemVCallExpr(const CallExpr &call);
    void emitAddress(const Expr &expr);
    std::string globalAddressInitializer(const Expr &expr);
    bool supportsCurrentByValueStruct(const Type &type) const;
    bool supportsCurrentStructInitializer(const Type &type) const;
    bool isRegisterPassedStruct(const Type &type) const;
    int alignStackSize(int size) const;
    std::vector<WindowsAbiArgument> buildWindowsAbiArguments(const std::vector<TypePtr> &parameterTypes, bool includeHiddenReturnPointer) const;
    int findHiddenReturnPointerLocalOffset(const Function &function) const;
    int findLargeStructCallResultLocalOffset(const Function &function) const;
    int currentFunctionFrameSize(const Function &function) const;
    void emitCopyBytes(const std::string &destAddressExpr, const std::string &srcAddressExpr, int size);
    void emitCopyStructValue(const Type &type, const std::string &destAddressExpr, const std::string &srcAddressExpr);
    void emitLoadStructValueToRax(const Expr &expr);
    void emitStoreStructValueFromRax(const Type &type, const std::string &destAddressExpr);
    void emitGlobalStructInitializer(const GlobalVar &global, const InitializerListExpr *list);
    void emitZeroFillBytes(std::ostringstream &line, int count, bool &first) const;
    void emitGlobalStructMemberValue(std::ostringstream &line, const Type &type, const Expr &expr, bool &first);
    void emitLocalStructInitializer(const DeclStmt &decl, const InitializerListExpr &list);
    void emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr);
    void emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list);
    void emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex);
    void emitStoreToLocalSlot(const Type &type, int addressOffset);
    void emitLoad(const Type &type);
    void emitStore(const Type &type);
    std::string dataDirectiveForSize(int size) const;
    long long evaluateStaticIntegerInitializer(const Expr &expr) const;
    int pointeeSize(const Type &type) const;
    const RegisterSet &argumentRegister(int index) const;
    bool usesWindowsAbi() const;
    int argumentRegisterCount() const;
    int stackArgumentOffset(int index) const;
    static std::string functionSymbol(const std::string &name);
    std::string stringLabel(const std::string &value);
    static std::string globalSymbol(const std::string &name);
    void emitLine(const std::string &text);
    void emitDataLine(std::string text);
    void emitBssLine(std::string text);
    void emitRdataLine(std::string text);
    std::string makeLabel(const std::string &prefix);

    std::ostringstream out;
    std::vector<std::string> dataLines;
    std::vector<std::string> bssLines;
    std::vector<std::string> rdataLines;
    std::unordered_map<std::string, std::string> stringLabels;
    const Program *currentProgram = nullptr;
    std::string currentReturnLabel;
    std::vector<std::string> loopContinueLabels;
    std::vector<std::string> loopBreakLabels;
    const TargetSpec &target;
    bool emitProgramEntryPoint = true;
    int activeHiddenReturnPointerOffset = 0;
    int activeLargeStructCallResultOffset = 0;
    int labelCounter = 0;
};
