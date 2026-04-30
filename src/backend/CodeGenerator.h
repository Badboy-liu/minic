#pragma once

#include "Ast.h"
#include "Target.h"

#include <cstdint>
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

    void emitPrologue();
    void emitEntryPoint();
    void emitGlobals();
    void emitStringLiterals();
    void emitFunction(const Function &function);
    void emitTargetExternPrelude();
    void emitTargetEntryBody();
    void emitStatement(const Stmt &stmt);
    void emitExpr(const Expr &expr);
    void emitZeroComparison(const Type &type, const std::string &jumpIfZeroLabel, bool jumpWhenZero);
    void emitCallExpr(const CallExpr &call);
    void emitWindowsCallExpr(const CallExpr &call);
    void emitSystemVCallExpr(const CallExpr &call);
    void emitAddress(const Expr &expr);
    std::string globalAddressInitializer(const Expr &expr);
    void emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr);
    void emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list);
    void emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex);
    void emitStoreToLocalSlot(const Type &type, int addressOffset);
    void emitBoolNormalize(const Type &type);
    void emitLoad(const Type &type);
    void emitStore(const Type &type);
    void emitValueConversion(const Type &sourceType, const Type &targetType);
    void emitConvertFloatingValue(const Type &sourceType, const Type &targetType);
    void emitFloatBinaryOp(const BinaryExpr &binary, const Type &type);
    void emitPushFloatValue(const Type &type);
    void emitPopFloatValue(const Type &type, const char *targetRegister);
    std::string dataDirectiveForSize(int size) const;
    long long evaluateStaticIntegerInitializer(const Expr &expr) const;
    int pointeeSize(const Type &type) const;
    bool useUnsignedIntegerOps(const Type &type) const;
    bool useUnsignedComparison(const BinaryExpr &binary) const;
    const RegisterSet &argumentRegister(int index) const;
    bool usesWindowsAbi() const;
    int argumentRegisterCount() const;
    int stackArgumentOffset(int index) const;
    static std::string functionSymbol(const std::string &name);
    std::string stringLabel(const std::string &value);
    std::string floatLiteralLabel(double value, bool isFloat32);
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
    std::unordered_map<std::uint64_t, std::string> float64Labels;
    std::unordered_map<std::uint32_t, std::string> float32Labels;
    const Program *currentProgram = nullptr;
    std::string currentReturnLabel;
    TypePtr currentFunctionReturnType;
    std::vector<std::string> loopContinueLabels;
    std::vector<std::string> loopBreakLabels;
    const TargetSpec &target;
    bool emitProgramEntryPoint = true;
    int labelCounter = 0;
};
