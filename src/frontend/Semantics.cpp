#include "Semantics.h"

#include <climits>
#include <stdexcept>

namespace {

std::string functionSymbol(const std::string &name) {
    return "fn_" + name;
}

}

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine *diagValue) : diag(diagValue) {}

bool SemanticAnalyzer::hasErrors() const {
    return hasSemanticErrors;
}

void SemanticAnalyzer::diagError(int line, int column, const std::string &message) {
    hasSemanticErrors = true;
    if (diag) {
        diag->error(line, column, message);
    }
}

void SemanticAnalyzer::diagError(const Expr &expr, const std::string &message) {
    diagError(expr.line, expr.column, message);
}

void SemanticAnalyzer::diagError(const Stmt &stmt, const std::string &message) {
    diagError(stmt.line, stmt.column, message);
}

void SemanticAnalyzer::analyze(Program &program, bool requireMain) {
    bool hasMain = false;
    functions.clear();
    globals.clear();
    globalSignatures.clear();

    for (const auto &function : program.functions) {
        FunctionSignature signature;
        signature.returnType = function.returnType;
        for (const auto &parameter : function.parameters) {
            signature.parameterTypes.push_back(parameter.type);
        }
        signature.hasDefinition = !function.isDeclaration();
        signature.isVariadic = function.isVariadic;

        auto found = functions.find(function.name);
        if (found == functions.end()) {
            functions.emplace(function.name, std::move(signature));
        } else {
            if (!sameType(found->second.returnType, function.returnType)) {
                fail("conflicting return type for function: " + function.name);
            }
            if (found->second.parameterTypes.size() != function.parameters.size()) {
                fail("conflicting parameter list for function: " + function.name);
            }
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                if (!sameType(found->second.parameterTypes[i], function.parameters[i].type)) {
                    fail("conflicting parameter type for function: " + function.name);
                }
            }
            if (found->second.hasDefinition && !function.isDeclaration()) {
                fail("duplicate function definition: " + function.name);
            }
            found->second.hasDefinition = found->second.hasDefinition || !function.isDeclaration();
        }

        if (function.name == "main" && !function.isDeclaration()) {
            hasMain = true;
            if (!function.returnType->isInteger()) {
                fail("'main' must return int");
            }
            if (!function.parameters.empty()) {
                fail("'main' must not take parameters");
            }
            if (function.isVariadic) {
                fail("'main' must not be variadic");
            }
        }

    }

    for (const auto &function : program.functions) {
        auto it = functions.find(function.name);
        if (it != functions.end()) {
            globals.emplace(
                function.name,
                VariableSymbol{
                    Type::makeFunction(function.returnType, it->second.parameterTypes, it->second.isVariadic),
                    0,
                    true,
                    functionSymbol(function.name)});
        }
    }

    for (auto &global : program.globals) {
        analyzeGlobal(global);
    }

    // 检查全局 _Static_assert
    for (auto &assertStmt : program.globalStaticAsserts) {
        analyzeExpr(*assertStmt->condition);
        long long condValue = 0;
        if (!evaluateConstantExpr(*assertStmt->condition, condValue)) {
            fail("_Static_assert condition must be a constant expression");
        }
        if (condValue == 0) {
            // 无消息时提供默认诊断信息
            std::string msg = assertStmt->message.empty()
                ? "static assertion failed"
                : assertStmt->message;
            fail("_Static_assert failed: " + msg);
        }
    }

    for (auto &function : program.functions) {
        if (!function.isDeclaration()) {
            try {
                analyzeFunction(function);
            } catch (const std::runtime_error &) {
                // 单个函数分析失败，继续分析其他函数
                hasSemanticErrors = true;
            }
        }
    }

    if (requireMain && !hasMain) {
        fail("program must define an 'int main()' function");
    }
}

void SemanticAnalyzer::analyzeFunction(Function &function) {
    scopes.clear();
    nextStackOffset = 0;
    loopDepth = 0;
    currentReturnType = function.returnType;
    currentFunctionParameters = &function.parameters;
    currentFunctionIsVariadic = function.isVariadic;

    enterScope();
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        auto &parameter = function.parameters[i];
        auto &scope = scopes.back();
        if (parameter.type->isVoid() || parameter.type->isArray() || parameter.type->isFunction()) {
            fail("unsupported parameter type in function " + function.name + ": " + typeName(parameter.type));
        }
        if (parameter.type->isStruct() && !isSupportedByValueStructType(parameter.type)) {
            fail("unsupported by-value struct parameter type in function " + function.name + ": " + typeName(parameter.type));
        }
        if (scope.find(parameter.name) != scope.end()) {
            fail("duplicate parameter name in function " + function.name + ": " + parameter.name);
        }
        // 可变参数和非可变参数函数都使用局部栈空间存储参数
        // 代码生成器会额外将可变参数保存到 shadow space 供 va_start 使用
        nextStackOffset = alignTo(nextStackOffset, parameter.type->alignment());
        nextStackOffset += parameter.type->storageSize();
        parameter.stackOffset = nextStackOffset;
        scope.emplace(parameter.name, VariableSymbol{parameter.type, parameter.stackOffset});
        scopeVarOrder.push_back(parameter.name);
    }
    analyzeBlock(*function.body);

    // -Wunused-parameter：检查未使用的函数参数
    if (warnUnusedParam && diag) {
        for (const auto &param : function.parameters) {
            if (usedVars.back().find(param.name) == usedVars.back().end() &&
                param.name.front() != '_') {
                diag->warning(0, 0,
                    "-Wunused-parameter: unused parameter '" + param.name + "' in function '" + function.name + "'");
            }
        }
    }

    leaveScope();

    function.stackSize = alignTo(nextStackOffset, 16);
}

void SemanticAnalyzer::analyzeGlobal(GlobalVar &global) {
    if (functions.find(global.name) != functions.end()) {
        fail("global variable conflicts with function: " + global.name);
    }
    if (global.type->isVoid() || global.type->isFunction()) {
        fail("global variable cannot have type void: " + global.name);
    }
    if (global.type->isArray()) {
        if (global.type->elementType->isVoid() || global.type->arrayLength <= 0) {
            fail("only positive-length global arrays with non-void elements are supported: " + global.name);
        }
    }

    global.symbolName = "gv_" + global.name;
    global.emitStorage = false;
    global.isBss = false;

    const bool hasInitializer = global.init != nullptr;
    const bool isTentativeDefinition = !global.isExternStorage && !hasInitializer;

    auto found = globalSignatures.find(global.name);
    if (found == globalSignatures.end()) {
        GlobalSignature signature;
        signature.type = global.type;
        signature.hasInitializerDefinition = hasInitializer;
        signature.hasTentativeDefinition = isTentativeDefinition;
        signature.symbolName = global.symbolName;
        globalSignatures.emplace(global.name, std::move(signature));

        if (!global.isExternStorage) {
            global.emitStorage = true;
            global.isBss = isTentativeDefinition;
        }
    } else {
        if (!sameType(found->second.type, global.type)) {
            fail("conflicting global variable type: " + global.name);
        }
        global.symbolName = found->second.symbolName;

        if (hasInitializer) {
            if (found->second.hasInitializerDefinition) {
                fail("duplicate initialized global definition: " + global.name);
            }
            found->second.hasInitializerDefinition = true;
            found->second.hasTentativeDefinition = false;
            global.emitStorage = true;
            global.isBss = false;
        } else if (isTentativeDefinition && !found->second.hasInitializerDefinition && !found->second.hasTentativeDefinition) {
            found->second.hasTentativeDefinition = true;
            global.emitStorage = true;
            global.isBss = true;
        }
    }

    globals[global.name] = VariableSymbol{global.type, 0, true, global.symbolName};

    if (!hasInitializer) {
        return;
    }

    analyzeExpr(*global.init);
    if (global.type->isArray()) {
        validateArrayInitializer(global.name, global.type, *global.init, true);
        return;
    }
    if (global.type->isStruct()) {
        validateStructInitializer(global.name, global.type, *global.init, true);
        return;
    }

    if (global.type->isPointer()) {
        if (!isSupportedGlobalPointerInitializer(global)) {
            fail(
                "global pointer initializers currently support only function names, '&function', '&global', or string literals: " +
                global.name);
        }
        return;
    }

    if (global.init->kind != Expr::Kind::Number || !global.type->isInteger()) {
        fail("global initializers currently support only integer constants or char[] string literals: " + global.name);
    }
}

bool SemanticAnalyzer::isSupportedGlobalPointerInitializer(const GlobalVar &global) const {
    if (global.init->kind == Expr::Kind::String) {
        return canAssign(global.type, global.init->type);
    }

    if (global.init->kind == Expr::Kind::Variable) {
        return canAssign(global.type, global.init->type);
    }

    if (global.init->kind != Expr::Kind::Unary) {
        return false;
    }

    const auto &unary = static_cast<const UnaryExpr &>(*global.init);
    if (unary.op != UnaryOp::AddressOf) {
        return false;
    }
    if (unary.operand->kind != Expr::Kind::Variable) {
        return false;
    }

    const auto &variable = static_cast<const VariableExpr &>(*unary.operand);
    if (!variable.isGlobal || variable.type->isArray()) {
        return false;
    }

    return canAssign(global.type, global.init->type);
}

bool SemanticAnalyzer::isSupportedGlobalPointerArrayInitializer(const GlobalVar &global) const {
    if (!global.type->elementType->isPointer() || global.init->kind != Expr::Kind::InitializerList) {
        return false;
    }
    const auto &list = static_cast<const InitializerListExpr &>(*global.init);
    if (static_cast<int>(list.elements.size()) > global.type->arrayLength) {
        fail("too many elements in global array initializer: " + global.name);
    }
    for (const auto &element : list.elements) {
        if (!canAssign(global.type->elementType, element->type)) {
            return false;
        }
    }
    return true;
}

bool SemanticAnalyzer::isSupportedStaticPointerInitializer(const Expr &expr) const {
    if (expr.kind == Expr::Kind::String) {
        return true;
    }
    if (expr.kind == Expr::Kind::Variable) {
        const auto &variable = static_cast<const VariableExpr &>(expr);
        return variable.isGlobal || variable.type->isFunction();
    }
    if (expr.kind != Expr::Kind::Unary) {
        return false;
    }

    const auto &unary = static_cast<const UnaryExpr &>(expr);
    if (unary.op != UnaryOp::AddressOf || unary.operand->kind != Expr::Kind::Variable) {
        return false;
    }

    const auto &variable = static_cast<const VariableExpr &>(*unary.operand);
    return variable.isGlobal && !variable.type->isArray();
}

bool SemanticAnalyzer::isSupportedPointerArrayElementInitializer(
    const TypePtr &elementType,
    const Expr &expr,
    bool isGlobal) const {
    if (!canAssign(elementType, expr.type)) {
        return false;
    }
    if (!isSupportedStaticPointerInitializer(expr)) {
        return false;
    }
    return true;
}

bool SemanticAnalyzer::isSupportedGlobalIntegerInitializer(const Expr &expr) const {
    if (expr.kind == Expr::Kind::Number) {
        return true;
    }
    if (expr.kind != Expr::Kind::Unary) {
        return false;
    }

    const auto &unary = static_cast<const UnaryExpr &>(expr);
    if ((unary.op != UnaryOp::Plus && unary.op != UnaryOp::Minus) ||
        unary.operand->kind != Expr::Kind::Number) {
        return false;
    }
    return true;
}

bool SemanticAnalyzer::isSupportedStructMemberType(const TypePtr &type) const {
    return type->isInteger() || type->isPointer();
}

bool SemanticAnalyzer::isSupportedByValueStructType(const TypePtr &type) const {
    if (!type->isStruct()) {
        return true;
    }
    for (const auto &member : type->members) {
        if (!isSupportedStructMemberType(member.type)) {
            return false;
        }
    }
    return true;
}

void SemanticAnalyzer::validateStructInitializer(
    const std::string &name,
    const TypePtr &structType,
    Expr &init,
    bool isGlobal) {
    if (init.kind != Expr::Kind::InitializerList) {
        fail(
            std::string(isGlobal ? "global" : "local") +
            " struct initializers currently support only one-level initializer lists: " + name);
    }

    const auto &list = static_cast<const InitializerListExpr &>(init);
    if (list.elements.size() > structType->members.size()) {
        fail("too many elements in " + std::string(isGlobal ? "global" : "local") + " struct initializer: " + name);
    }

    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const StructMember &member = structType->members[i];
        Expr &element = *list.elements[i];

        // 结构体成员：需要嵌套初始化列表，递归验证
        if (member.type->isStruct()) {
            if (member.type->isArray()) {
                fail(
                    std::string(isGlobal ? "global" : "local") +
                    " struct initializers do not yet support array members: " + name);
            }
            if (element.kind != Expr::Kind::InitializerList) {
                fail(
                    std::string(isGlobal ? "global" : "local") +
                    " struct member requires nested initializer list: " + name + "." + member.name);
            }
            validateStructInitializer(name + "." + member.name, member.type, element, isGlobal);
            continue;
        }

        if (member.type->isArray()) {
            fail(
                std::string(isGlobal ? "global" : "local") +
                " struct initializers do not yet support array members: " + name);
        }
        if (element.kind == Expr::Kind::InitializerList) {
            fail(
                std::string(isGlobal ? "global" : "local") +
                " struct initializers do not yet support nested aggregate elements for non-struct members: " + name);
        }
        if (!canAssign(member.type, element.type)) {
            fail(
                "struct initializer element type mismatch for " + name + "." + member.name +
                ": expected " + typeName(member.type) + ", got " + typeName(element.type));
        }
        if (member.type->isPointer() && !isSupportedStaticPointerInitializer(element)) {
            fail(
                std::string(isGlobal ? "global" : "local") +
                " struct pointer member initializers currently support only function names, '&function', '&global', or string literals: " +
                name + "." + member.name);
        }
        if (isGlobal && member.type->isInteger() && !isSupportedGlobalIntegerInitializer(element)) {
            fail("global struct integer member initializers currently support only integer constants: " + name + "." + member.name);
        }
    }
}

void SemanticAnalyzer::validateArrayInitializer(
    const std::string &name,
    const TypePtr &arrayType,
    Expr &init,
    bool isGlobal) {
    if (init.kind == Expr::Kind::String && arrayType->elementType->equals(*Type::makeChar())) {
        const auto &stringExpr = static_cast<const StringExpr &>(init);
        if (static_cast<int>(stringExpr.value.size()) + 1 > arrayType->arrayLength) {
            fail("string literal is too long for " + std::string(isGlobal ? "global" : "local") + " array: " + name);
        }
        return;
    }

    if (init.kind != Expr::Kind::InitializerList) {
        fail(
            std::string(isGlobal ? "global" : "local") +
            " array initializers currently support only char[] string literals or initializer lists: " + name);
    }

    const auto &list = static_cast<const InitializerListExpr &>(init);
    if (static_cast<int>(list.elements.size()) > arrayType->arrayLength) {
        fail("too many elements in " + std::string(isGlobal ? "global" : "local") + " array initializer: " + name);
    }

    for (const auto &element : list.elements) {
        // 多维数组：元素类型为数组且元素为初始化列表时，递归验证
        if (arrayType->elementType->isArray() && element->kind == Expr::Kind::InitializerList) {
            validateArrayInitializer(name, arrayType->elementType, *element, isGlobal);
            continue;
        }
        if (!canAssign(arrayType->elementType, element->type)) {
            fail(
                "array initializer element type mismatch for " + name + ": expected " +
                typeName(arrayType->elementType) + ", got " + typeName(element->type));
        }
        if (arrayType->elementType->isPointer()) {
            if (!isSupportedPointerArrayElementInitializer(arrayType->elementType, *element, isGlobal)) {
                fail(
                    std::string(isGlobal ? "global" : "local") +
                    " pointer array initializers currently support only function names, '&function', '&global', or string literals: " +
                    name);
            }
            continue;
        }
        if (isGlobal && arrayType->elementType->isInteger() && !isSupportedGlobalIntegerInitializer(*element)) {
            fail("global integer array initializers currently support only integer constants: " + name);
        }
    }
}

void SemanticAnalyzer::analyzeBlock(BlockStmt &block) {
    enterScope();
    for (auto &statement : block.statements) {
        analyzeStatement(*statement);
    }
    leaveScope();
}

void SemanticAnalyzer::analyzeStatement(Stmt &stmt) {
    stmt.accept(*this);
}

void SemanticAnalyzer::visitReturnStmt(ReturnStmt &node) {
    if (currentReturnType->isVoid()) {
        if (node.expr) {
            fail("void function must not return a value");
        }
    } else {
        if (!node.expr) {
            fail("non-void function must return a value");
        }
        analyzeExpr(*node.expr);
        if (currentReturnType->isStruct() && !isSupportedByValueStructType(currentReturnType)) {
            fail("unsupported by-value struct return type: " + typeName(currentReturnType));
        }
        if (!canAssign(currentReturnType, node.expr->type)) {
            fail("return type mismatch: expected " + typeName(currentReturnType) + ", got " + typeName(node.expr->type));
        }
        insertImplicitCast(node.expr, currentReturnType);
    }
}

void SemanticAnalyzer::visitExprStmt(ExprStmt &node) {
    analyzeExpr(*node.expr);
}

void SemanticAnalyzer::visitDeclStmt(DeclStmt &node) {
    if (node.isStatic) {
        auto &scope = scopes.back();
        if (scope.find(node.name) != scope.end()) {
            fail("redeclaration of local variable: " + node.name);
        }
        if (node.type->isVoid()) {
            fail("variable cannot have type void: " + node.name);
        }
        if (node.type->isFunction()) {
            fail("variable cannot have function type: " + node.name);
        }
        static int staticLocalCounter = 0;
        node.staticSymbolName = "static_local_" + std::to_string(staticLocalCounter++) + "_" + node.name;
        globals[node.name] = VariableSymbol{node.type, 0, true, node.staticSymbolName};
        scope.emplace(node.name, VariableSymbol{node.type, 0, true, node.staticSymbolName});

        if (node.init) {
            analyzeExpr(*node.init);
            if (node.type->isStruct()) {
                validateStructInitializer(node.name, node.type, *node.init, true);
                return;
            }
            if (node.type->isArray()) {
                validateArrayInitializer(node.name, node.type, *node.init, true);
                return;
            }
            if (!canAssign(node.type, node.init->type)) {
                fail("cannot initialize " + node.name + " of type " + typeName(node.type) + " with " + typeName(node.init->type));
            }
            insertImplicitCast(node.init, node.type);
        }
        return;
    }
    declareVariable(node);
    if (node.init) {
        analyzeExpr(*node.init);
        if (node.type->isStruct()) {
            validateStructInitializer(node.name, node.type, *node.init, false);
            return;
        }
        if (node.type->isArray()) {
            validateArrayInitializer(node.name, node.type, *node.init, false);
            return;
        }
        if (!canAssign(node.type, node.init->type)) {
            fail("cannot initialize " + node.name + " of type " + typeName(node.type) + " with " + typeName(node.init->type));
        }
        insertImplicitCast(node.init, node.type);
    }
}

void SemanticAnalyzer::visitBlockStmt(BlockStmt &node) {
    analyzeBlock(node);
}

void SemanticAnalyzer::visitIfStmt(IfStmt &node) {
    analyzeExpr(*node.condition);
    if (!node.condition->type->isScalar()) {
        fail("if condition must be scalar");
    }
    analyzeStatement(*node.thenBranch);
    if (node.elseBranch) {
        analyzeStatement(*node.elseBranch);
    }
}

void SemanticAnalyzer::visitWhileStmt(WhileStmt &node) {
    analyzeExpr(*node.condition);
    if (!node.condition->type->isScalar()) {
        fail("while condition must be scalar");
    }
    ++loopDepth;
    analyzeStatement(*node.body);
    --loopDepth;
}

void SemanticAnalyzer::visitForStmt(ForStmt &node) {
    enterScope();
    if (node.init) {
        analyzeStatement(*node.init);
    }
    if (node.condition) {
        analyzeExpr(*node.condition);
        if (!node.condition->type->isScalar()) {
            fail("for condition must be scalar");
        }
    }
    if (node.update) {
        analyzeExpr(*node.update);
    }
    ++loopDepth;
    analyzeStatement(*node.body);
    --loopDepth;
    leaveScope();
}

void SemanticAnalyzer::visitDoWhileStmt(DoWhileStmt &node) {
    ++loopDepth;
    analyzeStatement(*node.body);
    analyzeExpr(*node.condition);
    if (!node.condition->type->isScalar()) {
        fail("do-while condition must be scalar");
    }
    --loopDepth;
}

void SemanticAnalyzer::visitSwitchStmt(SwitchStmt &node) {
    analyzeExpr(*node.scrutinee);
    ++switchDepth;
    for (auto &c : node.cases) {
        analyzeExpr(*c.label);
        analyzeStatement(*c.body);
    }
    if (node.defaultBody) {
        analyzeStatement(*node.defaultBody);
    }
    --switchDepth;
}

void SemanticAnalyzer::visitBreakStmt(BreakStmt &) {
    if (loopDepth == 0 && switchDepth == 0) {
        fail("'break' used outside of a loop or switch");
    }
}

void SemanticAnalyzer::visitContinueStmt(ContinueStmt &) {
    if (loopDepth == 0) {
        fail("'continue' used outside of a loop");
    }
}

void SemanticAnalyzer::visitGotoStmt(GotoStmt &) {
    // goto 语句不做前向引用检查
}

void SemanticAnalyzer::visitLabelStmt(LabelStmt &node) {
    analyzeStatement(*node.body);
}

void SemanticAnalyzer::visitStaticAssertStmt(StaticAssertStmt &node) {
    analyzeExpr(*node.condition);
    long long condValue = 0;
    if (!evaluateConstantExpr(*node.condition, condValue)) {
        fail("_Static_assert condition must be a constant expression");
    }
    if (condValue == 0) {
        std::string msg = node.message.empty()
            ? "static assertion failed"
            : node.message;
        fail("_Static_assert failed: " + msg);
    }
}

void SemanticAnalyzer::analyzeExpr(Expr &expr) {
    expr.accept(*this);
}

void SemanticAnalyzer::visitNumberExpr(NumberExpr &node) {
    node.type = Type::makeInt();
    node.isLValue = false;
}

void SemanticAnalyzer::visitFloatLiteralExpr(FloatLiteralExpr &node) {
    node.type = Type::makeDouble();
    node.isLValue = false;
}

void SemanticAnalyzer::visitStringExpr(StringExpr &node) {
    node.type = Type::makeArray(Type::makeChar(), static_cast<int>(node.value.size()) + 1);
    node.isLValue = false;
}

void SemanticAnalyzer::visitVariableExpr(VariableExpr &node) {
    const VariableSymbol symbol = resolveVariable(node.name, node.line, node.column);
    node.stackOffset = symbol.stackOffset;
    node.type = symbol.type;
    node.isGlobal = symbol.isGlobal;
    node.symbolName = symbol.symbolName;
    node.isLValue = !symbol.type->isFunction();

    if (!symbol.isGlobal && !usedVars.empty()) {
        usedVars.back().insert(node.name);
    }

    if (!symbol.isGlobal && !symbol.type->isFunction() && !initializedVars.empty()) {
        bool isInitialized = false;
        for (const auto &scope : initializedVars) {
            if (scope.find(node.name) != scope.end()) {
                isInitialized = true;
                break;
            }
        }
        if (!isInitialized && currentFunctionParameters) {
            for (const auto &param : *currentFunctionParameters) {
                if (param.name == node.name) {
                    isInitialized = true;
                    break;
                }
            }
        }
        if (!isInitialized) {
            diag->warning(node.line, node.column, "use of uninitialized variable: " + node.name);
        }
    }
}

void SemanticAnalyzer::visitUnaryExpr(UnaryExpr &node) {
    if (node.operand) {
        analyzeExpr(*node.operand);
    }
    switch (node.op) {
    case UnaryOp::Plus:
    case UnaryOp::Minus:
        if (!node.operand->type->isInteger() && !node.operand->type->isFloatingPoint()) {
            fail("unary +/- requires int or float operand");
        }
        node.type = node.operand->type->isFloatingPoint()
            ? node.operand->type
            : promoteIntegerType(node.operand->type);
        node.isLValue = false;
        return;
    case UnaryOp::LogicalNot:
        if (!node.operand->type->isScalar()) {
            fail("logical ! requires scalar operand");
        }
        node.type = Type::makeInt();
        node.isLValue = false;
        return;
    case UnaryOp::AddressOf:
        if (!node.operand->isLValue && !node.operand->type->isFunction()) {
            fail("address-of requires an lvalue");
        }
        node.type = Type::makePointer(node.operand->type);
        node.isLValue = false;
        return;
    case UnaryOp::Dereference: {
        TypePtr operandType = decayType(node.operand->type);
        if (!operandType->isPointer()) {
            fail("dereference requires pointer operand");
        }
        node.type = operandType->elementType;
        node.isLValue = !node.type->isFunction();
        return;
    }
    case UnaryOp::BitwiseNot:
        if (!node.operand->type->isInteger()) {
            fail("bitwise ~ requires int operand");
        }
        node.type = promoteIntegerType(node.operand->type);
        node.isLValue = false;
        return;
    case UnaryOp::PreIncrement:
    case UnaryOp::PreDecrement:
    case UnaryOp::PostIncrement:
    case UnaryOp::PostDecrement:
        if (!node.operand->isLValue) {
            fail("increment/decrement requires an lvalue");
        }
        if (!node.operand->type->isScalar()) {
            fail("increment/decrement requires scalar operand");
        }
        node.type = node.operand->type;
        node.isLValue = false;
        return;
    case UnaryOp::Sizeof:
        node.type = Type::makeULong();
        node.isLValue = false;
        return;
    case UnaryOp::Alignof:
        if (!node.sizeofType) {
            fail("_Alignof requires a type argument");
        }
        node.type = Type::makeInt();
        node.isLValue = false;
        return;
    }
}

void SemanticAnalyzer::visitBinaryExpr(BinaryExpr &node) {
    analyzeExpr(*node.left);
    analyzeExpr(*node.right);
    TypePtr leftType = decayType(node.left->type);
    TypePtr rightType = decayType(node.right->type);

    switch (node.op) {
    case BinaryOp::Add:
        if (leftType->isPointer() && rightType->isInteger()) {
            node.type = leftType;
        } else if (leftType->isInteger() && rightType->isPointer()) {
            node.type = rightType;
        } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
            node.type = usualArithmeticConversion(leftType, rightType);
            insertImplicitCast(node.left, node.type);
            insertImplicitCast(node.right, node.type);
        } else {
            fail("invalid operands to '+'");
        }
        node.isLValue = false;
        return;
    case BinaryOp::Subtract:
        if (leftType->isPointer() && rightType->isPointer()) {
            node.type = Type::makeLongLong();
        } else if (leftType->isPointer() && rightType->isInteger()) {
            node.type = leftType;
        } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
            node.type = usualArithmeticConversion(leftType, rightType);
            insertImplicitCast(node.left, node.type);
            insertImplicitCast(node.right, node.type);
        } else {
            fail("invalid operands to '-'");
        }
        node.isLValue = false;
        return;
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
        if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
            node.type = usualArithmeticConversion(leftType, rightType);
            insertImplicitCast(node.left, node.type);
            insertImplicitCast(node.right, node.type);
            // 编译期除零检测
            if (node.op == BinaryOp::Divide && diag) {
                long long rhsVal = 0;
                if (evaluateConstantExpr(*node.right, rhsVal) && rhsVal == 0) {
                    diag->error(node.line, node.column, "division by zero");
                }
            }
        } else {
            fail("arithmetic operator requires operands of the same type");
        }
        node.isLValue = false;
        return;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
        if (sameType(leftType, rightType)) {
        } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
            // 有符号/无符号比较警告
            if (diag && leftType->isInteger() && rightType->isInteger() &&
                leftType->isUnsigned != rightType->isUnsigned) {
                diag->warning(node.line, node.column,
                    "comparison of " + typeName(leftType) + " and " + typeName(rightType) +
                    ": signed/unsigned comparison may behave unexpectedly");
            }
            TypePtr convType = usualArithmeticConversion(leftType, rightType);
            insertImplicitCast(node.left, convType);
            insertImplicitCast(node.right, convType);
        } else {
            fail("incompatible operands to equality operator");
        }
        node.type = Type::makeInt();
        node.isLValue = false;
        return;
    case BinaryOp::LogicalAnd:
    case BinaryOp::LogicalOr:
        if (!leftType->isScalar() || !rightType->isScalar()) {
            fail("logical operator requires scalar operands");
        }
        node.type = Type::makeInt();
        node.isLValue = false;
        return;
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
        if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
            // 有符号/无符号比较警告
            if (diag && leftType->isInteger() && rightType->isInteger() &&
                leftType->isUnsigned != rightType->isUnsigned) {
                diag->warning(node.line, node.column,
                    "comparison of " + typeName(leftType) + " and " + typeName(rightType) +
                    ": signed/unsigned comparison may behave unexpectedly");
            }
            TypePtr convType = usualArithmeticConversion(leftType, rightType);
            insertImplicitCast(node.left, convType);
            insertImplicitCast(node.right, convType);
        } else {
            fail("comparison operator requires scalar operands");
        }
        node.type = Type::makeInt();
        node.isLValue = false;
        return;
    case BinaryOp::Modulo:
        if (!leftType->isInteger() || !rightType->isInteger()) {
            fail("bitwise operator requires int operands");
        }
        node.type = commonIntegerType(leftType, rightType);
        // 编译期取模零检测
        if (diag) {
            long long rhsVal = 0;
            if (evaluateConstantExpr(*node.right, rhsVal) && rhsVal == 0) {
                diag->error(node.line, node.column, "division by zero");
            }
        }
        node.isLValue = false;
        return;
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
    case BinaryOp::BitwiseAnd:
    case BinaryOp::BitwiseXor:
    case BinaryOp::BitwiseOr:
        if (!leftType->isInteger() || !rightType->isInteger()) {
            fail("bitwise operator requires int operands");
        }
        node.type = commonIntegerType(leftType, rightType);
        node.isLValue = false;
        return;
    case BinaryOp::Comma:
        node.type = node.right->type;
        node.isLValue = node.right->isLValue;
        return;
    }
}

void SemanticAnalyzer::visitInitializerListExpr(InitializerListExpr &node) {
    for (auto &element : node.elements) {
        analyzeExpr(*element);
    }
    node.type = Type::makeVoid();
    node.isLValue = false;
}

void SemanticAnalyzer::visitAssignExpr(AssignExpr &node) {
    analyzeExpr(*node.target);
    analyzeExpr(*node.value);
    if (!node.target->isLValue) {
        fail("left-hand side of assignment must be an lvalue");
    }
    if (node.target->type->isConst) {
        fail("cannot assign to const variable");
    }
    if (node.target->type->isArray() || node.target->type->isVoid()) {
        fail("left-hand side of assignment is not assignable");
    }
    if (!canAssign(node.target->type, node.value->type)) {
        fail("assignment type mismatch: cannot assign " + typeName(node.value->type) + " to " + typeName(node.target->type));
    }
    insertImplicitCast(node.value, node.target->type);
    if (node.target->kind == Expr::Kind::Variable) {
        const auto &varName = static_cast<const VariableExpr &>(*node.target).name;
        if (!initializedVars.empty()) {
            initializedVars.back().insert(varName);
        }
    }
    node.type = node.target->type;
    node.isLValue = false;
}

static int editDistance(const std::string &a, const std::string &b);

void SemanticAnalyzer::visitCallExpr(CallExpr &node) {
    if (node.callee->kind == Expr::Kind::Variable) {
        const auto &name = static_cast<const VariableExpr &>(*node.callee).name;
        bool found = functions.find(name) != functions.end() || globals.find(name) != globals.end();
        if (!found) {
            for (const auto &scope : scopes) {
                if (scope.find(name) != scope.end()) { found = true; break; }
            }
        }
        if (!found) {
            // 查找相似的函数名
            std::string suggestion;
            int bestDist = 3;  // 最大编辑距离
            for (const auto &fn : functions) {
                int dist = editDistance(name, fn.first);
                if (dist < bestDist) {
                    bestDist = dist;
                    suggestion = fn.first;
                }
            }
            std::string message = "implicit declaration of function '" + name + "' is invalid in C99";
            if (!suggestion.empty()) {
                message += "; did you mean '" + suggestion + "'?";
            }
            failAt(node.callee->line, node.callee->column, message);
        }
    }
    analyzeExpr(*node.callee);
    TypePtr calleeType = decayType(node.callee->type);
    TypePtr functionType;
    if (calleeType->isFunction()) {
        functionType = calleeType;
    } else if (calleeType->isPointer() && calleeType->elementType->isFunction()) {
        functionType = calleeType->elementType;
    } else {
        fail("call target must be a function or function pointer");
    }
    if (functionType->isVariadic) {
        if (node.arguments.size() < functionType->parameterTypes.size()) {
            fail(
                "wrong number of arguments in variadic function call: expected at least " +
                std::to_string(functionType->parameterTypes.size()) + ", got " + std::to_string(node.arguments.size()));
        }
    } else {
        if (node.arguments.size() != functionType->parameterTypes.size()) {
            fail(
                "wrong number of arguments in function call: expected " +
                std::to_string(functionType->parameterTypes.size()) + ", got " + std::to_string(node.arguments.size()));
        }
    }
    node.parameterTypes = functionType->parameterTypes;
    for (std::size_t i = 0; i < node.arguments.size(); ++i) {
        analyzeExpr(*node.arguments[i]);
        if (i < functionType->parameterTypes.size()) {
            if (!isEquivalentArgumentType(functionType->parameterTypes[i], node.arguments[i]->type)) {
                fail(
                    "argument type mismatch in function call: expected " +
                    typeName(functionType->parameterTypes[i]) + ", got " + typeName(node.arguments[i]->type));
            }
        }
    }
    node.type = functionType->elementType;
    node.isLValue = false;
}

void SemanticAnalyzer::visitIndexExpr(IndexExpr &node) {
    analyzeExpr(*node.base);
    analyzeExpr(*node.index);
    if (!node.index->type->isInteger()) {
        fail("array subscript must be int");
    }
    TypePtr baseType = decayType(node.base->type);
    if (!baseType->isPointer()) {
        fail("subscripted value must be pointer or array");
    }
    node.type = baseType->elementType;
    node.isLValue = true;
}

void SemanticAnalyzer::visitMemberAccessExpr(MemberAccessExpr &node) {
    analyzeExpr(*node.base);
    if (!node.base->type->isStruct()) {
        fail("member access requires a struct value");
    }
    const StructMember *resolved = node.base->type->findMember(node.memberName);
    int resolvedOffset = 0;
    if (resolved) {
        resolvedOffset = resolved->offset;
    } else {
        int outOffset = 0;
        resolved = node.base->type->findMemberRecursive(node.memberName, 0, outOffset);
        if (!resolved) {
            fail("unknown struct member: " + node.memberName);
        }
        resolvedOffset = outOffset;
    }
    node.memberOffset = resolvedOffset;
    node.bitWidth = resolved->bitWidth;
    node.bitOffset = resolved->bitOffset;
    node.type = resolved->type;
    node.isLValue = true;
}

void SemanticAnalyzer::visitTernaryExpr(TernaryExpr &node) {
    analyzeExpr(*node.condition);
    if (!node.condition->type->isScalar()) {
        fail("ternary condition must be scalar");
    }
    analyzeExpr(*node.thenExpr);
    analyzeExpr(*node.elseExpr);
    TypePtr thenType = decayType(node.thenExpr->type);
    TypePtr elseType = decayType(node.elseExpr->type);
    if (thenType->isScalar() && elseType->isScalar()) {
        node.type = usualArithmeticConversion(thenType, elseType);
        insertImplicitCast(node.thenExpr, node.type);
        insertImplicitCast(node.elseExpr, node.type);
    } else {
        node.type = thenType;
    }
    node.isLValue = false;
}

void SemanticAnalyzer::visitCastExpr(CastExpr &node) {
    analyzeExpr(*node.operand);
    node.type = node.targetType;
    node.isLValue = false;
}

void SemanticAnalyzer::visitBuiltinVaStartExpr(BuiltinVaStartExpr &node) {
    analyzeExpr(*node.ap);
    if (!node.ap->type->isPointer()) {
        fail("__builtin_va_start: first argument must be a pointer");
    }
    if (currentFunctionParameters) {
        for (int i = static_cast<int>(currentFunctionParameters->size()) - 1; i >= 0; --i) {
            if ((*currentFunctionParameters)[i].name == node.lastParamName) {
                node.paramIndex = i;
                break;
            }
        }
        if (node.paramIndex < 0) {
            fail("__builtin_va_start: unknown parameter name: " + node.lastParamName);
        }
    }
    node.type = Type::makeVoid();
    node.isLValue = false;
}

void SemanticAnalyzer::visitBuiltinVaArgExpr(BuiltinVaArgExpr &node) {
    analyzeExpr(*node.ap);
    if (!node.ap->type->isPointer()) {
        fail("__builtin_va_arg: first argument must be a pointer");
    }
    node.type = node.argType;
    node.isLValue = false;
}

void SemanticAnalyzer::visitBuiltinVaEndExpr(BuiltinVaEndExpr &node) {
    analyzeExpr(*node.ap);
    if (!node.ap->type->isPointer()) {
        fail("__builtin_va_end: argument must be a pointer");
    }
    node.type = Type::makeVoid();
    node.isLValue = false;
}

void SemanticAnalyzer::visitGenericExpr(GenericExpr &node) {
    analyzeExpr(*node.controllingExpr);
    TypePtr ctrlType = decayType(node.controllingExpr->type);

    for (auto &assoc : node.associations) {
        analyzeExpr(*assoc.expr);
    }

    for (auto &assoc : node.associations) {
        if (!assoc.type) {
            continue;
        }
        TypePtr assocType = decayType(assoc.type);
        if (sameType(ctrlType, assocType)) {
            node.type = assoc.expr->type;
            node.isLValue = assoc.expr->isLValue;
            node.selectedExpr = assoc.expr.get();
            return;
        }
    }

    for (auto &assoc : node.associations) {
        if (!assoc.type) {
            node.type = assoc.expr->type;
            node.isLValue = assoc.expr->isLValue;
            node.selectedExpr = assoc.expr.get();
            return;
        }
    }

    fail("_Generic: no matching association for type " + typeName(ctrlType));
}

void SemanticAnalyzer::visitCompoundLiteralExpr(CompoundLiteralExpr &node) {
    for (auto &element : node.init->elements) {
        analyzeExpr(*element);
    }
    node.type = node.compoundType;
    node.isLValue = true;
    int clSize = node.compoundType->valueSize();
    if (clSize == 0 && node.compoundType->isArray() && node.init) {
        clSize = static_cast<int>(node.init->elements.size()) * node.compoundType->elementType->valueSize();
    }
    nextStackOffset = alignTo(nextStackOffset, node.compoundType->alignment());
    nextStackOffset += clSize;
    node.stackOffset = nextStackOffset;
}

void SemanticAnalyzer::visitStmtExpr(StmtExpr &node) {
    for (auto &stmt : node.statements) {
        analyzeStatement(*stmt);
    }
    if (node.result) {
        analyzeExpr(*node.result);
        node.type = node.result->type;
        node.isLValue = node.result->isLValue;
    } else {
        node.type = Type::makeVoid();
        node.isLValue = false;
    }
}

void SemanticAnalyzer::enterScope() {
    scopes.emplace_back();
    initializedVars.emplace_back();
    usedVars.emplace_back();
    scopeVarOrder.clear();
}

void SemanticAnalyzer::leaveScope() {
    // 检查未使用的局部变量
    if (diag && !scopes.back().empty()) {
        for (const auto &name : scopeVarOrder) {
            if (usedVars.back().find(name) == usedVars.back().end() &&
                name.front() != '_') {
                auto it = scopes.back().find(name);
                if (it != scopes.back().end() && !it->second.isGlobal) {
                    diag->warning(0, 0, "unused variable '" + name + "'");
                }
            }
        }
    }
    scopes.pop_back();
    initializedVars.pop_back();
    usedVars.pop_back();
    scopeVarOrder.clear();
}

void SemanticAnalyzer::declareVariable(DeclStmt &decl) {
    auto &scope = scopes.back();
    if (scope.find(decl.name) != scope.end()) {
        fail("redeclaration of local variable: " + decl.name);
    }
    // -Wshadow：检查是否遮蔽了外层作用域的变量
    if (warnShadow && diag && decl.line > 0) {
        for (int i = 0; i < static_cast<int>(scopes.size()) - 1; ++i) {
            if (scopes[i].find(decl.name) != scopes[i].end()) {
                diag->warning(decl.line, decl.column,
                    "-Wshadow: declaration of '" + decl.name + "' shadows outer scope variable");
                break;
            }
        }
        if (globals.find(decl.name) != globals.end()) {
            diag->warning(decl.line, decl.column,
                "-Wshadow: declaration of '" + decl.name + "' shadows global variable");
        }
    }
    if (decl.type->isVoid()) {
        fail("variable cannot have type void: " + decl.name);
    }
    if (decl.type->isFunction()) {
        fail("variable cannot have function type: " + decl.name);
    }
    if (decl.type->isArray()) {
        if (decl.type->elementType->isVoid()) {
            fail("local arrays with void elements are not supported: " + decl.name);
        }
        if (decl.type->isVla) {
            // VLA：分析大小表达式
            if (decl.vlaSizeExpr) {
                analyzeExpr(*decl.vlaSizeExpr);
                if (!decl.vlaSizeExpr->type->isInteger()) {
                    fail("VLA size must be an integer expression: " + decl.name);
                }
            }
            // VLA 的栈空间在代码生成时动态分配
            // 这里只分配一个指针大小的槽位来存储 VLA 基地址
            nextStackOffset = alignTo(nextStackOffset, 8);
            nextStackOffset += 8;
            decl.stackOffset = nextStackOffset;
            scope.emplace(decl.name, VariableSymbol{decl.type, decl.stackOffset});
            scopeVarOrder.push_back(decl.name);
            // 数组被视为已初始化（即使元素未初始化）
            initializedVars.back().insert(decl.name);
            return;
        }
        if (decl.type->arrayLength <= 0) {
            fail("only positive-length local arrays with non-void elements are supported: " + decl.name);
        }
    }

    nextStackOffset = alignTo(nextStackOffset, decl.type->alignment());
    nextStackOffset += decl.type->storageSize();
    decl.stackOffset = nextStackOffset;
    scope.emplace(decl.name, VariableSymbol{decl.type, decl.stackOffset});
    scopeVarOrder.push_back(decl.name);

    // 如果有初始化器，标记为已初始化
    if (decl.init) {
        initializedVars.back().insert(decl.name);
    }
}

// 计算两个字符串的编辑距离（Levenshtein distance）
static int editDistance(const std::string &a, const std::string &b) {
    const int m = static_cast<int>(a.size());
    const int n = static_cast<int>(b.size());

    // 使用两个一维行代替二维矩阵，减少堆分配
    std::vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;

    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            if (a[i - 1] == b[j - 1]) {
                curr[j] = prev[j - 1];
            } else {
                curr[j] = 1 + std::min({prev[j], curr[j - 1], prev[j - 1]});
            }
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// 查找最相似的名称
static std::string findSimilarName(const std::string &name,
                                    const std::vector<std::unordered_map<std::string, VariableSymbol>> &scopes,
                                    const std::unordered_map<std::string, VariableSymbol> &globals,
                                    const std::unordered_map<std::string, FunctionSignature> &functions) {
    std::string bestMatch;
    int bestDistance = 3; // 最大允许编辑距离

    // 检查局部变量（所有作用域）
    for (const auto &scope : scopes) {
        for (const auto &entry : scope) {
            int dist = editDistance(name, entry.first);
            if (dist < bestDistance) {
                bestDistance = dist;
                bestMatch = entry.first;
            }
        }
    }

    // 检查全局变量
    for (const auto &entry : globals) {
        int dist = editDistance(name, entry.first);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestMatch = entry.first;
        }
    }

    // 检查函数
    for (const auto &entry : functions) {
        int dist = editDistance(name, entry.first);
        if (dist < bestDistance) {
            bestDistance = dist;
            bestMatch = entry.first;
        }
    }

    return bestMatch;
}

VariableSymbol SemanticAnalyzer::resolveVariable(const std::string &name, int line, int column) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }

    const auto global = globals.find(name);
    if (global != globals.end()) {
        return global->second;
    }

    const auto function = functions.find(name);
    if (function != functions.end()) {
        return VariableSymbol{
            Type::makeFunction(function->second.returnType, function->second.parameterTypes, function->second.isVariadic),
            0,
            true,
            functionSymbol(name)};
    }

    // 错误级联抑制：同一名称只报告一次
    if (reportedUndeclared.count(name)) {
        // 返回一个占位类型，避免后续崩溃
        return VariableSymbol{Type::makeInt(), 0, false, ""};
    }
    reportedUndeclared.insert(name);

    // 查找相似的名称
    std::string suggestion = findSimilarName(name, scopes, globals, functions);
    std::string message = "use of undeclared variable: " + name;
    if (!suggestion.empty()) {
        message += "; did you mean '" + suggestion + "'?";
    }

    if (line > 0) {
        failAt(line, column, message);
    } else {
        fail(message);
    }
}

bool SemanticAnalyzer::canAssign(const TypePtr &target, const TypePtr &value) const {
    TypePtr decayedValue = decayType(value);
    if (target->isInteger() && decayedValue->isInteger()) {
        return true;
    }
    if (target->isFloatingPoint() && decayedValue->isFloatingPoint()) {
        return true;
    }
    // 允许 int <-> float 隐式转换
    if ((target->isInteger() && decayedValue->isFloatingPoint()) ||
        (target->isFloatingPoint() && decayedValue->isInteger())) {
        return true;
    }
    if (target->isStruct() || decayedValue->isStruct()) {
        return target->isStruct() &&
            decayedValue->isStruct() &&
            sameType(target, decayedValue) &&
            isSupportedByValueStructType(target) &&
            isSupportedByValueStructType(decayedValue);
    }
    // void* 与任何指针类型之间可隐式转换
    if (target->isPointer() && decayedValue->isPointer()) {
        if (target->elementType->kind == TypeKind::Void || decayedValue->elementType->kind == TypeKind::Void) {
            return true;
        }
    }
    return sameType(target, decayedValue);
}

bool SemanticAnalyzer::sameType(const TypePtr &left, const TypePtr &right) const {
    return left->equals(*right);
}

bool SemanticAnalyzer::isEquivalentArgumentType(const TypePtr &param, const TypePtr &arg) const {
    TypePtr decayedArg = decayType(arg);
    if (param->isInteger() && decayedArg->isInteger()) {
        return true;
    }
    if (param->isFloatingPoint() && decayedArg->isFloatingPoint()) {
        return true;
    }
    // 允许 int <-> float 隐式转换
    if ((param->isInteger() && decayedArg->isFloatingPoint()) ||
        (param->isFloatingPoint() && decayedArg->isInteger())) {
        return true;
    }
    if (param->isStruct() || decayedArg->isStruct()) {
        return param->isStruct() &&
            decayedArg->isStruct() &&
            sameType(param, decayedArg) &&
            isSupportedByValueStructType(param) &&
            isSupportedByValueStructType(decayedArg);
    }
    // void* 与任何指针类型之间可隐式转换
    if (param->isPointer() && decayedArg->isPointer()) {
        if (param->elementType->kind == TypeKind::Void || decayedArg->elementType->kind == TypeKind::Void) {
            return true;
        }
    }
    return sameType(param, decayedArg);
}

TypePtr SemanticAnalyzer::decayType(const TypePtr &type) const {
    return type->decay();
}

TypePtr SemanticAnalyzer::promoteIntegerType(const TypePtr &type) const {
    if (!type->isInteger()) {
        return type;
    }
    if (type->kind == TypeKind::Char || type->kind == TypeKind::Short) {
        return Type::makeInt();
    }
    return std::make_shared<Type>(*type);
}

TypePtr SemanticAnalyzer::commonIntegerType(const TypePtr &left, const TypePtr &right) const {
    TypePtr promotedLeft = promoteIntegerType(left);
    TypePtr promotedRight = promoteIntegerType(right);
    return integerRank(promotedLeft) >= integerRank(promotedRight)
        ? promotedLeft
        : promotedRight;
}

TypePtr SemanticAnalyzer::commonFloatType(const TypePtr &left, const TypePtr &right) const {
    // double 优先级高于 float
    if (left->kind == TypeKind::Double || right->kind == TypeKind::Double) {
        return Type::makeDouble();
    }
    return Type::makeFloat();
}

TypePtr SemanticAnalyzer::usualArithmeticConversion(const TypePtr &left, const TypePtr &right) const {
    // 两个浮点：取更高精度
    if (left->isFloatingPoint() && right->isFloatingPoint()) {
        return commonFloatType(left, right);
    }
    // 一个浮点一个整数：提升到浮点
    if (left->isFloatingPoint() && right->isInteger()) {
        return left;
    }
    if (left->isInteger() && right->isFloatingPoint()) {
        return right;
    }
    // 两个整数：取更高 rank
    return commonIntegerType(left, right);
}

void SemanticAnalyzer::insertImplicitCast(std::unique_ptr<Expr> &expr, const TypePtr &targetType) {
    if (sameType(expr->type, targetType)) {
        return;
    }
    // 检测窄化转换并发出警告
    if (diag && expr->type && targetType) {
        const int srcSize = expr->type->valueSize();
        const int dstSize = targetType->valueSize();
        // 整数窄化：目标比源小
        if (expr->type->isInteger() && targetType->isInteger() && dstSize < srcSize) {
            diag->warning(expr->line, expr->column,
                "implicit conversion from " + typeName(expr->type) + " to " + typeName(targetType) +
                " (" + std::to_string(srcSize) + " bytes -> " + std::to_string(dstSize) + " bytes)");
        }
        // 浮点窄化：double -> float
        if (expr->type->kind == TypeKind::Double && targetType->kind == TypeKind::Float) {
            diag->warning(expr->line, expr->column,
                "implicit conversion from double to float (precision loss)");
        }
    }
    auto cast = std::make_unique<CastExpr>(targetType, std::move(expr));
    cast->type = targetType;
    cast->isLValue = false;
    expr = std::move(cast);
}

int SemanticAnalyzer::integerRank(const TypePtr &type) const {
    switch (type->kind) {
    case TypeKind::Char:
        return 1;
    case TypeKind::Short:
        return 2;
    case TypeKind::Int:
        return 3;
    case TypeKind::Long:
        return 4;
    case TypeKind::LongLong:
        return 5;
    default:
        return 0;
    }
}

std::string SemanticAnalyzer::typeName(const TypePtr &type) const {
    switch (type->kind) {
    case TypeKind::Struct:
        return "struct " + type->structName;
    case TypeKind::Union:
        return "union " + type->structName;
    case TypeKind::Char:
        return "char";
    case TypeKind::Short:
        return "short";
    case TypeKind::Int:
        return "int";
    case TypeKind::Long:
        return "long";
    case TypeKind::LongLong:
        return "long long";
    case TypeKind::Float:
        return "float";
    case TypeKind::Double:
        return "double";
    case TypeKind::Void:
        return "void";
    case TypeKind::Function: {
        std::string result = typeName(type->elementType) + " (";
        for (std::size_t i = 0; i < type->parameterTypes.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += typeName(type->parameterTypes[i]);
        }
        result += ")";
        return result;
    }
    case TypeKind::Pointer:
        return typeName(type->elementType) + "*";
    case TypeKind::Array:
        return typeName(type->elementType) + "[" + std::to_string(type->arrayLength) + "]";
    }
    return "unknown";
}

[[noreturn]] void SemanticAnalyzer::fail(const std::string &message) const {
    if (diag) {
        diag->error(0, 0, message);
    }
    throw std::runtime_error("Semantic error: " + message);
}

[[noreturn]] void SemanticAnalyzer::failAt(int line, int column, const std::string &message) const {
    if (diag) {
        diag->error(line, column, message);
    }
    throw std::runtime_error("Semantic error: " + message);
}

bool SemanticAnalyzer::evaluateConstantExpr(const Expr &expr, long long &result) const {
    switch (expr.kind) {
    case Expr::Kind::Number:
        result = static_cast<const NumberExpr &>(expr).value;
        return true;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        switch (unary.op) {
        case UnaryOp::Sizeof:
            if (unary.sizeofType) {
                result = unary.sizeofType->valueSize();
                return true;
            }
            if (unary.operand && unary.operand->type) {
                result = unary.operand->type->valueSize();
                return true;
            }
            return false;
        case UnaryOp::Alignof:
            if (unary.sizeofType) {
                result = unary.sizeofType->alignment();
                return true;
            }
            return false;
        case UnaryOp::Plus:
            if (unary.operand) {
                return evaluateConstantExpr(*unary.operand, result);
            }
            return false;
        case UnaryOp::Minus:
            if (unary.operand) {
                if (evaluateConstantExpr(*unary.operand, result)) {
                    result = -result;
                    return true;
                }
            }
            return false;
        case UnaryOp::LogicalNot:
            if (unary.operand) {
                if (evaluateConstantExpr(*unary.operand, result)) {
                    result = !result;
                    return true;
                }
            }
            return false;
        case UnaryOp::BitwiseNot:
            if (unary.operand) {
                if (evaluateConstantExpr(*unary.operand, result)) {
                    result = ~result;
                    return true;
                }
            }
            return false;
        default:
            return false;
        }
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        return evaluateConstantExpr(*cast.operand, result);
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        long long leftVal = 0, rightVal = 0;
        if (!evaluateConstantExpr(*binary.left, leftVal)) {
            return false;
        }
        if (!evaluateConstantExpr(*binary.right, rightVal)) {
            return false;
        }
        switch (binary.op) {
        case BinaryOp::Add: {
            long long wide = leftVal + rightVal;
            if ((wide > INT32_MAX || wide < INT32_MIN) && diag) {
                diag->warning(binary.line, binary.column, "signed integer overflow in constant expression");
            }
            result = wide;
            return true;
        }
        case BinaryOp::Subtract: {
            long long wide = leftVal - rightVal;
            if ((wide > INT32_MAX || wide < INT32_MIN) && diag) {
                diag->warning(binary.line, binary.column, "signed integer overflow in constant expression");
            }
            result = wide;
            return true;
        }
        case BinaryOp::Multiply: {
            long long wide = leftVal * rightVal;
            if ((wide > INT32_MAX || wide < INT32_MIN) && diag) {
                diag->warning(binary.line, binary.column, "signed integer overflow in constant expression");
            }
            result = wide;
            return true;
        }
        case BinaryOp::Divide:
            if (rightVal == 0) {
                if (diag) diag->error(binary.line, binary.column, "division by zero in constant expression");
                return false;
            }
            if (leftVal == INT_MIN && rightVal == -1) {
                if (diag) diag->error(binary.line, binary.column, "signed integer overflow in constant expression (INT_MIN / -1)");
                return false;
            }
            result = leftVal / rightVal;
            return true;
        case BinaryOp::Modulo:
            if (rightVal == 0) {
                if (diag) diag->error(binary.line, binary.column, "division by zero in constant expression");
                return false;
            }
            if (leftVal == INT_MIN && rightVal == -1) {
                if (diag) diag->error(binary.line, binary.column, "signed integer overflow in constant expression (INT_MIN % -1)");
                return false;
            }
            result = leftVal % rightVal;
            return true;
        case BinaryOp::Equal: result = leftVal == rightVal; return true;
        case BinaryOp::NotEqual: result = leftVal != rightVal; return true;
        case BinaryOp::Less: result = leftVal < rightVal; return true;
        case BinaryOp::LessEqual: result = leftVal <= rightVal; return true;
        case BinaryOp::Greater: result = leftVal > rightVal; return true;
        case BinaryOp::GreaterEqual: result = leftVal >= rightVal; return true;
        case BinaryOp::LogicalAnd: result = leftVal && rightVal; return true;
        case BinaryOp::LogicalOr: result = leftVal || rightVal; return true;
        case BinaryOp::BitwiseAnd: result = leftVal & rightVal; return true;
        case BinaryOp::BitwiseXor: result = leftVal ^ rightVal; return true;
        case BinaryOp::BitwiseOr: result = leftVal | rightVal; return true;
        case BinaryOp::ShiftLeft:
            if (rightVal < 0 || rightVal >= 32) {
                if (diag) diag->warning(binary.line, binary.column, "shift amount out of range in constant expression");
            }
            result = leftVal << rightVal;
            return true;
        case BinaryOp::ShiftRight:
            if (rightVal < 0 || rightVal >= 32) {
                if (diag) diag->warning(binary.line, binary.column, "shift amount out of range in constant expression");
            }
            result = leftVal >> rightVal;
            return true;
        default: return false;
        }
    }
    default:
        return false;
    }
}

int SemanticAnalyzer::alignTo(int value, int alignment) {
    return (value + alignment - 1) / alignment * alignment;
}
