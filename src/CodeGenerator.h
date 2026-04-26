#pragma once

#include "Ast.h"

#include <sstream>
#include <string>
#include <vector>

class CodeGenerator {
public:
    std::string generate(const Program &program);

private:
    void emitFunction(const Function &function);
    void emitStatement(const Stmt &stmt);
    void emitExpr(const Expr &expr);
    void emitAddress(const Expr &expr);
    void emitLoad(const Type &type);
    void emitStore(const Type &type);
    int pointeeSize(const Type &type) const;
    static std::string argumentRegister32(int index);
    static std::string argumentRegister64(int index);
    static std::string functionSymbol(const std::string &name);
    void emitLine(const std::string &text);
    std::string makeLabel(const std::string &prefix);

    std::ostringstream out;
    std::string currentReturnLabel;
    std::vector<std::string> loopContinueLabels;
    std::vector<std::string> loopBreakLabels;
    int labelCounter = 0;
};
