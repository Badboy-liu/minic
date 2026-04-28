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

    void emitPrologue();
    void emitEntryPoint();
    void emitGlobals();
    void emitStringLiterals();
    void emitFunction(const Function &function);
    void emitStatement(const Stmt &stmt);
    void emitExpr(const Expr &expr);
    void emitAddress(const Expr &expr);
    void emitLoad(const Type &type);
    void emitStore(const Type &type);
    int pointeeSize(const Type &type) const;
    const RegisterSet &argumentRegister(int index) const;
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
    TargetKind target;
    bool emitProgramEntryPoint = true;
    int labelCounter = 0;
};
