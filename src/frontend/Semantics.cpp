#include "Semantics.h"

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
        globals.emplace(
            function.name,
            VariableSymbol{
                Type::makeFunction(function.returnType, functions.at(function.name).parameterTypes, functions.at(function.name).isVariadic),
                0,
                true,
                functionSymbol(function.name)});
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
    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        auto &returnStmt = static_cast<ReturnStmt &>(stmt);
        if (currentReturnType->isVoid()) {
            if (returnStmt.expr) {
                fail("void function must not return a value");
            }
        } else {
            if (!returnStmt.expr) {
                fail("non-void function must return a value");
            }
            analyzeExpr(*returnStmt.expr);
            if (currentReturnType->isStruct() && !isSupportedByValueStructType(currentReturnType)) {
                fail("unsupported by-value struct return type: " + typeName(currentReturnType));
            }
            if (!canAssign(currentReturnType, returnStmt.expr->type)) {
                fail("return type mismatch: expected " + typeName(currentReturnType) + ", got " + typeName(returnStmt.expr->type));
            }
            insertImplicitCast(returnStmt.expr, currentReturnType);
        }
        break;
    }
    case Stmt::Kind::Expr:
        analyzeExpr(*static_cast<ExprStmt &>(stmt).expr);
        break;
    case Stmt::Kind::Decl: {
        auto &decl = static_cast<DeclStmt &>(stmt);
        if (decl.isStatic) {
            // static 局部变量：存储在 .data/.bss 节，不占栈空间
            auto &scope = scopes.back();
            if (scope.find(decl.name) != scope.end()) {
                fail("redeclaration of local variable: " + decl.name);
            }
            if (decl.type->isVoid()) {
                fail("variable cannot have type void: " + decl.name);
            }
            if (decl.type->isFunction()) {
                fail("variable cannot have function type: " + decl.name);
            }
            // 生成唯一符号名
            static int staticLocalCounter = 0;
            decl.staticSymbolName = "static_local_" + std::to_string(staticLocalCounter++) + "_" + decl.name;
            // 注册为全局符号，以便 resolveVariable 能找到
            globals[decl.name] = VariableSymbol{decl.type, 0, true, decl.staticSymbolName};
            scope.emplace(decl.name, VariableSymbol{decl.type, 0, true, decl.staticSymbolName});

            if (decl.init) {
                analyzeExpr(*decl.init);
                if (decl.type->isStruct()) {
                    validateStructInitializer(decl.name, decl.type, *decl.init, true);
                    break;
                }
                if (decl.type->isArray()) {
                    validateArrayInitializer(decl.name, decl.type, *decl.init, true);
                    break;
                }
                if (!canAssign(decl.type, decl.init->type)) {
                    fail("cannot initialize " + decl.name + " of type " + typeName(decl.type) + " with " + typeName(decl.init->type));
                }
                insertImplicitCast(decl.init, decl.type);
            }
            break;
        }
        declareVariable(decl);
        if (decl.init) {
            analyzeExpr(*decl.init);
            if (decl.type->isStruct()) {
                validateStructInitializer(decl.name, decl.type, *decl.init, false);
                break;
            }
            if (decl.type->isArray()) {
                validateArrayInitializer(decl.name, decl.type, *decl.init, false);
                break;
            }
            if (!canAssign(decl.type, decl.init->type)) {
                fail("cannot initialize " + decl.name + " of type " + typeName(decl.type) + " with " + typeName(decl.init->type));
            }
            insertImplicitCast(decl.init, decl.type);
        }
        break;
    }
    case Stmt::Kind::Block:
        analyzeBlock(static_cast<BlockStmt &>(stmt));
        break;
    case Stmt::Kind::If: {
        auto &ifStmt = static_cast<IfStmt &>(stmt);
        analyzeExpr(*ifStmt.condition);
        if (!ifStmt.condition->type->isScalar()) {
            fail("if condition must be scalar");
        }
        analyzeStatement(*ifStmt.thenBranch);
        if (ifStmt.elseBranch) {
            analyzeStatement(*ifStmt.elseBranch);
        }
        break;
    }
    case Stmt::Kind::While: {
        auto &whileStmt = static_cast<WhileStmt &>(stmt);
        analyzeExpr(*whileStmt.condition);
        if (!whileStmt.condition->type->isScalar()) {
            fail("while condition must be scalar");
        }
        ++loopDepth;
        analyzeStatement(*whileStmt.body);
        --loopDepth;
        break;
    }
    case Stmt::Kind::For: {
        auto &forStmt = static_cast<ForStmt &>(stmt);
        enterScope();
        if (forStmt.init) {
            analyzeStatement(*forStmt.init);
        }
        if (forStmt.condition) {
            analyzeExpr(*forStmt.condition);
            if (!forStmt.condition->type->isScalar()) {
                fail("for condition must be scalar");
            }
        }
        if (forStmt.update) {
            analyzeExpr(*forStmt.update);
        }
        ++loopDepth;
        analyzeStatement(*forStmt.body);
        --loopDepth;
        leaveScope();
        break;
    }
    case Stmt::Kind::DoWhile: {
        auto &doWhileStmt = static_cast<DoWhileStmt &>(stmt);
        ++loopDepth;
        analyzeStatement(*doWhileStmt.body);
        analyzeExpr(*doWhileStmt.condition);
        if (!doWhileStmt.condition->type->isScalar()) {
            fail("do-while condition must be scalar");
        }
        --loopDepth;
        break;
    }
    case Stmt::Kind::Switch: {
        auto &sw = static_cast<SwitchStmt &>(stmt);
        analyzeExpr(*sw.scrutinee);
        ++switchDepth;
        for (auto &c : sw.cases) {
            analyzeExpr(*c.label);
            analyzeStatement(*c.body);
        }
        if (sw.defaultBody) {
            analyzeStatement(*sw.defaultBody);
        }
        --switchDepth;
        break;
    }
    case Stmt::Kind::Break:
        if (loopDepth == 0 && switchDepth == 0) {
            fail("'break' used outside of a loop or switch");
        }
        break;
    case Stmt::Kind::Continue:
        if (loopDepth == 0) {
            fail("'continue' used outside of a loop");
        }
        break;
    case Stmt::Kind::Goto:
        // goto 语句不做前向引用检查
        break;
    case Stmt::Kind::Label: {
        auto &labelStmt = static_cast<LabelStmt &>(stmt);
        analyzeStatement(*labelStmt.body);
        break;
    }
    case Stmt::Kind::StaticAssert: {
        auto &assertStmt = static_cast<StaticAssertStmt &>(stmt);
        analyzeExpr(*assertStmt.condition);
        // 条件必须是编译时常量
        long long condValue = 0;
        if (!evaluateConstantExpr(*assertStmt.condition, condValue)) {
            fail("_Static_assert condition must be a constant expression");
        }
        if (condValue == 0) {
            // 无消息时提供默认诊断信息
            std::string msg = assertStmt.message.empty()
                ? "static assertion failed"
                : assertStmt.message;
            fail("_Static_assert failed: " + msg);
        }
        break;
    }
    }
}

void SemanticAnalyzer::analyzeExpr(Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
        expr.type = Type::makeInt();
        expr.isLValue = false;
        return;
    case Expr::Kind::FloatLiteral:
        expr.type = Type::makeDouble();
        expr.isLValue = false;
        return;
    case Expr::Kind::String: {
        auto &stringExpr = static_cast<StringExpr &>(expr);
        expr.type = Type::makeArray(Type::makeChar(), static_cast<int>(stringExpr.value.size()) + 1);
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Variable: {
        auto &variable = static_cast<VariableExpr &>(expr);
        const VariableSymbol symbol = resolveVariable(variable.name, expr.line, expr.column);
        variable.stackOffset = symbol.stackOffset;
        variable.type = symbol.type;
        variable.isGlobal = symbol.isGlobal;
        variable.symbolName = symbol.symbolName;
        variable.isLValue = !symbol.type->isFunction();
        expr.type = symbol.type;
        expr.isLValue = !symbol.type->isFunction();

        // 标记变量为已使用
        if (!symbol.isGlobal && !usedVars.empty()) {
            usedVars.back().insert(variable.name);
        }

        // 检查局部变量是否已初始化（全局变量和参数被视为已初始化）
        if (!symbol.isGlobal && !symbol.type->isFunction() && !initializedVars.empty()) {
            bool isInitialized = false;
            for (const auto &scope : initializedVars) {
                if (scope.find(variable.name) != scope.end()) {
                    isInitialized = true;
                    break;
                }
            }
            // 检查是否是函数参数
            if (!isInitialized && currentFunctionParameters) {
                for (const auto &param : *currentFunctionParameters) {
                    if (param.name == variable.name) {
                        isInitialized = true;
                        break;
                    }
                }
            }
            if (!isInitialized) {
                diag->warning(expr.line, expr.column, "use of uninitialized variable: " + variable.name);
            }
        }
        return;
    }
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(expr);
        if (unary.operand) {
            analyzeExpr(*unary.operand);
        }
        switch (unary.op) {
        case UnaryOp::Plus:
        case UnaryOp::Minus:
            if (!unary.operand->type->isInteger() && !unary.operand->type->isFloatingPoint()) {
                fail("unary +/- requires int or float operand");
            }
            expr.type = unary.operand->type->isFloatingPoint()
                ? unary.operand->type
                : promoteIntegerType(unary.operand->type);
            expr.isLValue = false;
            return;
        case UnaryOp::LogicalNot:
            if (!unary.operand->type->isScalar()) {
                fail("logical ! requires scalar operand");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        case UnaryOp::AddressOf:
            if (!unary.operand->isLValue && !unary.operand->type->isFunction()) {
                fail("address-of requires an lvalue");
            }
            expr.type = Type::makePointer(unary.operand->type);
            expr.isLValue = false;
            return;
        case UnaryOp::Dereference: {
            TypePtr operandType = decayType(unary.operand->type);
            if (!operandType->isPointer()) {
                fail("dereference requires pointer operand");
            }
            expr.type = operandType->elementType;
            expr.isLValue = !expr.type->isFunction();
            return;
        }
        case UnaryOp::BitwiseNot:
            if (!unary.operand->type->isInteger()) {
                fail("bitwise ~ requires int operand");
            }
            expr.type = promoteIntegerType(unary.operand->type);
            expr.isLValue = false;
            return;
        case UnaryOp::PreIncrement:
        case UnaryOp::PreDecrement:
        case UnaryOp::PostIncrement:
        case UnaryOp::PostDecrement:
            if (!unary.operand->isLValue) {
                fail("increment/decrement requires an lvalue");
            }
            if (!unary.operand->type->isScalar()) {
                fail("increment/decrement requires scalar operand");
            }
            expr.type = unary.operand->type;
            expr.isLValue = false;
            return;
        case UnaryOp::Sizeof:
            expr.type = Type::makeULong();
            expr.isLValue = false;
            return;
        case UnaryOp::Alignof:
            if (!unary.sizeofType) {
                fail("_Alignof requires a type argument");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        }
        return;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(expr);
        analyzeExpr(*binary.left);
        analyzeExpr(*binary.right);
        TypePtr leftType = decayType(binary.left->type);
        TypePtr rightType = decayType(binary.right->type);

        switch (binary.op) {
        case BinaryOp::Add:
            if (leftType->isPointer() && rightType->isInteger()) {
                expr.type = leftType;
            } else if (leftType->isInteger() && rightType->isPointer()) {
                expr.type = rightType;
            } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
                expr.type = usualArithmeticConversion(leftType, rightType);
                insertImplicitCast(binary.left, expr.type);
                insertImplicitCast(binary.right, expr.type);
            } else {
                fail("invalid operands to '+'");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Subtract:
            if (leftType->isPointer() && rightType->isPointer()) {
                // 指针减法：结果为 ptrdiff_t (long long)
                expr.type = Type::makeLongLong();
            } else if (leftType->isPointer() && rightType->isInteger()) {
                expr.type = leftType;
            } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
                expr.type = usualArithmeticConversion(leftType, rightType);
                insertImplicitCast(binary.left, expr.type);
                insertImplicitCast(binary.right, expr.type);
            } else {
                fail("invalid operands to '-'");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
            if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
                expr.type = usualArithmeticConversion(leftType, rightType);
                insertImplicitCast(binary.left, expr.type);
                insertImplicitCast(binary.right, expr.type);
            } else {
                fail("arithmetic operator requires operands of the same type");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
            if (sameType(leftType, rightType)) {
                // 类型相同，无需转换
            } else if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
                // 混合 int/float 比较：应用常用算术转换
                TypePtr convType = usualArithmeticConversion(leftType, rightType);
                insertImplicitCast(binary.left, convType);
                insertImplicitCast(binary.right, convType);
            } else {
                fail("incompatible operands to equality operator");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr:
            if (!leftType->isScalar() || !rightType->isScalar()) {
                fail("logical operator requires scalar operands");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        case BinaryOp::Less:
        case BinaryOp::LessEqual:
        case BinaryOp::Greater:
        case BinaryOp::GreaterEqual:
            if (leftType->isScalar() && rightType->isScalar() && !(leftType->isPointer() || rightType->isPointer())) {
                TypePtr convType = usualArithmeticConversion(leftType, rightType);
                insertImplicitCast(binary.left, convType);
                insertImplicitCast(binary.right, convType);
            } else {
                fail("comparison operator requires scalar operands");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        case BinaryOp::Modulo:
        case BinaryOp::ShiftLeft:
        case BinaryOp::ShiftRight:
        case BinaryOp::BitwiseAnd:
        case BinaryOp::BitwiseXor:
        case BinaryOp::BitwiseOr:
            if (!leftType->isInteger() || !rightType->isInteger()) {
                fail("bitwise operator requires int operands");
            }
            expr.type = commonIntegerType(leftType, rightType);
            expr.isLValue = false;
            return;
        case BinaryOp::Comma:
            // 逗号运算符：结果类型为右侧类型，isLValue 为右侧 isLValue
            expr.type = binary.right->type;
            expr.isLValue = binary.right->isLValue;
            return;
        }
        return;
    }
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<InitializerListExpr &>(expr);
        for (auto &element : list.elements) {
            analyzeExpr(*element);
        }
        expr.type = Type::makeVoid();
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<AssignExpr &>(expr);
        analyzeExpr(*assign.target);
        analyzeExpr(*assign.value);
        if (!assign.target->isLValue) {
            fail("left-hand side of assignment must be an lvalue");
        }
        if (assign.target->type->isConst) {
            fail("cannot assign to const variable");
        }
        if (assign.target->type->isArray() || assign.target->type->isVoid()) {
            fail("left-hand side of assignment is not assignable");
        }
        if (!canAssign(assign.target->type, assign.value->type)) {
            fail("assignment type mismatch: cannot assign " + typeName(assign.value->type) + " to " + typeName(assign.target->type));
        }
        // 赋值时插入隐式类型转换
        insertImplicitCast(assign.value, assign.target->type);
        // 标记变量为已初始化
        if (assign.target->kind == Expr::Kind::Variable) {
            const auto &varName = static_cast<const VariableExpr &>(*assign.target).name;
            if (!initializedVars.empty()) {
                initializedVars.back().insert(varName);
            }
        }
        expr.type = assign.target->type;
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<CallExpr &>(expr);
        // 检测未声明函数调用（C99 起不允许隐式函数声明）
        if (call.callee->kind == Expr::Kind::Variable) {
            const auto &name = static_cast<const VariableExpr &>(*call.callee).name;
            bool found = functions.find(name) != functions.end() || globals.find(name) != globals.end();
            if (!found) {
                for (const auto &scope : scopes) {
                    if (scope.find(name) != scope.end()) { found = true; break; }
                }
            }
            if (!found) {
                failAt(call.callee->line, call.callee->column,
                    "implicit declaration of function '" + name + "' is invalid in C99");
            }
        }
        analyzeExpr(*call.callee);
        TypePtr calleeType = decayType(call.callee->type);
        TypePtr functionType;
        if (calleeType->isFunction()) {
            functionType = calleeType;
        } else if (calleeType->isPointer() && calleeType->elementType->isFunction()) {
            functionType = calleeType->elementType;
        } else {
            fail("call target must be a function or function pointer");
        }
        if (functionType->isVariadic) {
            // 可变参数函数：参数数量 >= 声明的参数数量
            if (call.arguments.size() < functionType->parameterTypes.size()) {
                fail(
                    "wrong number of arguments in variadic function call: expected at least " +
                    std::to_string(functionType->parameterTypes.size()) + ", got " + std::to_string(call.arguments.size()));
            }
        } else {
            if (call.arguments.size() != functionType->parameterTypes.size()) {
                fail(
                    "wrong number of arguments in function call: expected " +
                    std::to_string(functionType->parameterTypes.size()) + ", got " + std::to_string(call.arguments.size()));
            }
        }
        call.parameterTypes = functionType->parameterTypes;
        for (std::size_t i = 0; i < call.arguments.size(); ++i) {
            analyzeExpr(*call.arguments[i]);
            if (i < functionType->parameterTypes.size()) {
                if (!isEquivalentArgumentType(functionType->parameterTypes[i], call.arguments[i]->type)) {
                    fail(
                        "argument type mismatch in function call: expected " +
                        typeName(functionType->parameterTypes[i]) + ", got " + typeName(call.arguments[i]->type));
                }
            }
        }
        expr.type = functionType->elementType;
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Index: {
        auto &index = static_cast<IndexExpr &>(expr);
        analyzeExpr(*index.base);
        analyzeExpr(*index.index);
        if (!index.index->type->isInteger()) {
            fail("array subscript must be int");
        }
        TypePtr baseType = decayType(index.base->type);
        if (!baseType->isPointer()) {
            fail("subscripted value must be pointer or array");
        }
        expr.type = baseType->elementType;
        expr.isLValue = true;
        return;
    }
    case Expr::Kind::MemberAccess: {
        auto &member = static_cast<MemberAccessExpr &>(expr);
        analyzeExpr(*member.base);
        if (!member.base->type->isStruct()) {
            fail("member access requires a struct value");
        }
        // 先尝试直接查找
        const StructMember *resolved = member.base->type->findMember(member.memberName);
        int resolvedOffset = 0;
        if (resolved) {
            resolvedOffset = resolved->offset;
        } else {
            // 直接查找失败，递归搜索匿名成员
            int outOffset = 0;
            resolved = member.base->type->findMemberRecursive(member.memberName, 0, outOffset);
            if (!resolved) {
                fail("unknown struct member: " + member.memberName);
            }
            resolvedOffset = outOffset;
        }
        member.memberOffset = resolvedOffset;
        member.bitWidth = resolved->bitWidth;
        member.bitOffset = resolved->bitOffset;
        expr.type = resolved->type;
        expr.isLValue = true;
        return;
    }
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<TernaryExpr &>(expr);
        analyzeExpr(*ternary.condition);
        if (!ternary.condition->type->isScalar()) {
            fail("ternary condition must be scalar");
        }
        analyzeExpr(*ternary.thenExpr);
        analyzeExpr(*ternary.elseExpr);
        TypePtr thenType = decayType(ternary.thenExpr->type);
        TypePtr elseType = decayType(ternary.elseExpr->type);
        if (thenType->isScalar() && elseType->isScalar()) {
            expr.type = usualArithmeticConversion(thenType, elseType);
            insertImplicitCast(ternary.thenExpr, expr.type);
            insertImplicitCast(ternary.elseExpr, expr.type);
        } else {
            expr.type = thenType;
        }
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Cast: {
        auto &cast = static_cast<CastExpr &>(expr);
        analyzeExpr(*cast.operand);
        // 类型转换：允许标量类型之间和指针之间的转换
        expr.type = cast.targetType;
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::BuiltinVaStart: {
        auto &vaStart = static_cast<BuiltinVaStartExpr &>(expr);
        analyzeExpr(*vaStart.ap);
        if (!vaStart.ap->type->isPointer()) {
            fail("__builtin_va_start: first argument must be a pointer");
        }
        // 查找最后一个命名参数的索引
        if (currentFunctionParameters) {
            for (int i = static_cast<int>(currentFunctionParameters->size()) - 1; i >= 0; --i) {
                if ((*currentFunctionParameters)[i].name == vaStart.lastParamName) {
                    vaStart.paramIndex = i;
                    break;
                }
            }
            if (vaStart.paramIndex < 0) {
                fail("__builtin_va_start: unknown parameter name: " + vaStart.lastParamName);
            }
        }
        expr.type = Type::makeVoid();
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::BuiltinVaArg: {
        auto &vaArg = static_cast<BuiltinVaArgExpr &>(expr);
        analyzeExpr(*vaArg.ap);
        if (!vaArg.ap->type->isPointer()) {
            fail("__builtin_va_arg: first argument must be a pointer");
        }
        expr.type = vaArg.argType;
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::BuiltinVaEnd: {
        auto &vaEnd = static_cast<BuiltinVaEndExpr &>(expr);
        analyzeExpr(*vaEnd.ap);
        if (!vaEnd.ap->type->isPointer()) {
            fail("__builtin_va_end: argument must be a pointer");
        }
        expr.type = Type::makeVoid();
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Generic: {
        auto &generic = static_cast<GenericExpr &>(expr);
        analyzeExpr(*generic.controllingExpr);
        // 对控制表达式类型做 decay（数组→指针，函数→指针）
        TypePtr ctrlType = decayType(generic.controllingExpr->type);

        // 分析所有 association 表达式
        for (auto &assoc : generic.associations) {
            analyzeExpr(*assoc.expr);
        }

        // 查找匹配的 association（对 association 类型也做 decay）
        for (auto &assoc : generic.associations) {
            if (!assoc.type) {
                continue;
            }
            TypePtr assocType = decayType(assoc.type);
            if (sameType(ctrlType, assocType)) {
                expr.type = assoc.expr->type;
                expr.isLValue = assoc.expr->isLValue;
                generic.selectedExpr = assoc.expr.get();
                return;
            }
        }

        // 使用 default
        for (auto &assoc : generic.associations) {
            if (!assoc.type) {
                expr.type = assoc.expr->type;
                expr.isLValue = assoc.expr->isLValue;
                generic.selectedExpr = assoc.expr.get();
                return;
            }
        }

        fail("_Generic: no matching association for type " + typeName(ctrlType));
    }
    case Expr::Kind::CompoundLiteral: {
        auto &compound = static_cast<CompoundLiteralExpr &>(expr);
        // 分析初始化列表中的元素
        for (auto &element : compound.init->elements) {
            analyzeExpr(*element);
        }
        // 复合字面量的类型就是指定的类型
        expr.type = compound.compoundType;
        // 复合字面量是左值（可以取地址）
        expr.isLValue = true;
        // 为复合字面量分配栈空间
        int clSize = compound.compoundType->valueSize();
        if (clSize == 0 && compound.compoundType->isArray() && compound.init) {
            clSize = static_cast<int>(compound.init->elements.size()) * compound.compoundType->elementType->valueSize();
        }
        nextStackOffset = alignTo(nextStackOffset, compound.compoundType->alignment());
        nextStackOffset += clSize;
        compound.stackOffset = nextStackOffset;
        return;
    }
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
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));

    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (a[i - 1] == b[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    return dp[m][n];
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
        case BinaryOp::Add: result = leftVal + rightVal; return true;
        case BinaryOp::Subtract: result = leftVal - rightVal; return true;
        case BinaryOp::Multiply: result = leftVal * rightVal; return true;
        case BinaryOp::Divide:
            if (rightVal == 0) return false;
            result = leftVal / rightVal;
            return true;
        case BinaryOp::Modulo:
            if (rightVal == 0) return false;
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
        case BinaryOp::ShiftLeft: result = leftVal << rightVal; return true;
        case BinaryOp::ShiftRight: result = leftVal >> rightVal; return true;
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
