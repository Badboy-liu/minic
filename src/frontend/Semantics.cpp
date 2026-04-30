#include "Semantics.h"

#include <stdexcept>

namespace {

std::string functionSymbol(const std::string &name) {
    return "fn_" + name;
}

}

void SemanticAnalyzer::analyze(Program &program) {
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
        }

    }

    for (const auto &function : program.functions) {
        globals.emplace(
            function.name,
            VariableSymbol{
                Type::makeFunction(function.returnType, functions.at(function.name).parameterTypes),
                0,
                true,
                functionSymbol(function.name)});
    }

    for (auto &global : program.globals) {
        analyzeGlobal(global);
    }

    for (auto &function : program.functions) {
        if (!function.isDeclaration()) {
            analyzeFunction(function);
        }
    }

    if (!hasMain) {
        fail("program must define an 'int main()' function");
    }
}

void SemanticAnalyzer::analyzeFunction(Function &function) {
    scopes.clear();
    nextStackOffset = 0;
    loopDepth = 0;
    currentReturnType = function.returnType;

    enterScope();
    for (auto &parameter : function.parameters) {
        auto &scope = scopes.back();
        if (parameter.type->isVoid() || parameter.type->isArray() || parameter.type->isFunction()) {
            fail("unsupported parameter type in function " + function.name + ": " + typeName(parameter.type));
        }
        if (scope.find(parameter.name) != scope.end()) {
            fail("duplicate parameter name in function " + function.name + ": " + parameter.name);
        }
        nextStackOffset += parameter.type->storageSize();
        parameter.stackOffset = nextStackOffset;
        scope.emplace(parameter.name, VariableSymbol{parameter.type, parameter.stackOffset});
    }
    analyzeBlock(*function.body);
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
            if (!canAssign(currentReturnType, returnStmt.expr->type)) {
                fail("return type mismatch: expected " + typeName(currentReturnType) + ", got " + typeName(returnStmt.expr->type));
            }
        }
        break;
    }
    case Stmt::Kind::Expr:
        analyzeExpr(*static_cast<ExprStmt &>(stmt).expr);
        break;
    case Stmt::Kind::Decl: {
        auto &decl = static_cast<DeclStmt &>(stmt);
        declareVariable(decl);
        if (decl.init) {
            analyzeExpr(*decl.init);
            if (decl.type->isArray()) {
                validateArrayInitializer(decl.name, decl.type, *decl.init, false);
                break;
            }
            if (!canAssign(decl.type, decl.init->type)) {
                fail("cannot initialize " + decl.name + " of type " + typeName(decl.type) + " with " + typeName(decl.init->type));
            }
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
    case Stmt::Kind::Break:
        if (loopDepth == 0) {
            fail("'break' used outside of a loop");
        }
        break;
    case Stmt::Kind::Continue:
        if (loopDepth == 0) {
            fail("'continue' used outside of a loop");
        }
        break;
    }
}

void SemanticAnalyzer::analyzeExpr(Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
        expr.type = Type::makeInt();
        expr.isLValue = false;
        return;
    case Expr::Kind::FloatNumber:
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
        const VariableSymbol symbol = resolveVariable(variable.name);
        variable.stackOffset = symbol.stackOffset;
        variable.type = symbol.type;
        variable.isGlobal = symbol.isGlobal;
        variable.symbolName = symbol.symbolName;
        variable.isLValue = !symbol.type->isFunction();
        expr.type = symbol.type;
        expr.isLValue = !symbol.type->isFunction();
        return;
    }
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(expr);
        analyzeExpr(*unary.operand);
        switch (unary.op) {
        case UnaryOp::Plus:
        case UnaryOp::Minus:
            if (!unary.operand->type->isArithmetic()) {
                fail("unary +/- requires arithmetic operand");
            }
            expr.type = unary.operand->type->isFloating()
                ? decayType(unary.operand->type)
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
            if (operandType->elementType->isVoid()) {
                fail("cannot dereference void*");
            }
            expr.type = operandType->elementType;
            expr.isLValue = !expr.type->isFunction();
            return;
        }
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
            if ((leftType->isFloating() && rightType->isFloating()) ||
                (leftType->isInteger() && rightType->isInteger()) ||
                (leftType->isFloating() && rightType->isInteger()) ||
                (leftType->isInteger() && rightType->isFloating())) {
                expr.type = commonArithmeticType(leftType, rightType);
            } else if (leftType->isPointer() && rightType->isInteger()) {
                expr.type = leftType;
            } else if (leftType->isInteger() && rightType->isPointer()) {
                expr.type = rightType;
            } else {
                fail("invalid operands to '+'");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Subtract:
            if ((leftType->isFloating() && rightType->isFloating()) ||
                (leftType->isInteger() && rightType->isInteger()) ||
                (leftType->isFloating() && rightType->isInteger()) ||
                (leftType->isInteger() && rightType->isFloating())) {
                expr.type = commonArithmeticType(leftType, rightType);
            } else if (leftType->isPointer() && rightType->isInteger()) {
                expr.type = leftType;
            } else {
                fail("invalid operands to '-'");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
            if (!((leftType->isFloating() && rightType->isFloating()) ||
                (leftType->isInteger() && rightType->isInteger()) ||
                (leftType->isFloating() && rightType->isInteger()) ||
                (leftType->isInteger() && rightType->isFloating()))) {
                fail("arithmetic operator requires arithmetic operands");
            }
            expr.type = commonArithmeticType(leftType, rightType);
            expr.isLValue = false;
            return;
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
            if (!(sameType(leftType, rightType) ||
                (leftType->isFloating() && rightType->isFloating()) ||
                (leftType->isInteger() && rightType->isInteger()) ||
                (leftType->isFloating() && rightType->isInteger()) ||
                (leftType->isInteger() && rightType->isFloating()) ||
                (leftType->isScalar() && rightType->isScalar()))) {
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
            if (!((leftType->isFloating() && rightType->isFloating()) ||
                (leftType->isInteger() && rightType->isInteger()) ||
                (leftType->isFloating() && rightType->isInteger()) ||
                (leftType->isInteger() && rightType->isFloating()))) {
                fail("comparison operator requires arithmetic operands");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
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
        if (assign.target->type->isArray() || assign.target->type->isVoid()) {
            fail("left-hand side of assignment is not assignable");
        }
        if (!canAssign(assign.target->type, assign.value->type)) {
            fail("assignment type mismatch: cannot assign " + typeName(assign.value->type) + " to " + typeName(assign.target->type));
        }
        expr.type = assign.target->type;
        expr.isLValue = false;
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<CallExpr &>(expr);
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
        if (call.arguments.size() != functionType->parameterTypes.size()) {
            fail(
                "wrong number of arguments in function call: expected " +
                std::to_string(functionType->parameterTypes.size()) + ", got " + std::to_string(call.arguments.size()));
        }
        call.parameterTypes = functionType->parameterTypes;
        for (std::size_t i = 0; i < call.arguments.size(); ++i) {
            analyzeExpr(*call.arguments[i]);
            if (!isEquivalentArgumentType(functionType->parameterTypes[i], call.arguments[i]->type)) {
                fail(
                    "argument type mismatch in function call: expected " +
                    typeName(functionType->parameterTypes[i]) + ", got " + typeName(call.arguments[i]->type));
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
    }
}

void SemanticAnalyzer::enterScope() {
    scopes.emplace_back();
}

void SemanticAnalyzer::leaveScope() {
    scopes.pop_back();
}

void SemanticAnalyzer::declareVariable(DeclStmt &decl) {
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
    if (decl.type->isArray()) {
        if (decl.type->elementType->isVoid() || decl.type->arrayLength <= 0) {
            fail("only positive-length local arrays with non-void elements are supported: " + decl.name);
        }
    }

    nextStackOffset += decl.type->storageSize();
    decl.stackOffset = nextStackOffset;
    scope.emplace(decl.name, VariableSymbol{decl.type, decl.stackOffset});
}

VariableSymbol SemanticAnalyzer::resolveVariable(const std::string &name) const {
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
            Type::makeFunction(function->second.returnType, function->second.parameterTypes),
            0,
            true,
            functionSymbol(name)};
    }

    fail("use of undeclared variable: " + name);
}

bool SemanticAnalyzer::canAssign(const TypePtr &target, const TypePtr &value) const {
    TypePtr decayedValue = decayType(value);
    if (target->isInteger() && decayedValue->isInteger()) {
        return true;
    }
    if (target->isFloating() && decayedValue->isFloating()) {
        return true;
    }
    if (target->isFloating() && decayedValue->isInteger()) {
        return true;
    }
    if (target->isInteger() && decayedValue->isFloating()) {
        return true;
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
    if (param->isFloating() && decayedArg->isFloating()) {
        return true;
    }
    if (param->isFloating() && decayedArg->isInteger()) {
        return true;
    }
    if (param->isInteger() && decayedArg->isFloating()) {
        return true;
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
    if (type->kind == TypeKind::Bool ||
        type->kind == TypeKind::Char ||
        type->kind == TypeKind::UnsignedChar ||
        type->kind == TypeKind::Short ||
        type->kind == TypeKind::UnsignedShort) {
        return Type::makeInt();
    }
    return std::make_shared<Type>(*type);
}

TypePtr SemanticAnalyzer::commonIntegerType(const TypePtr &left, const TypePtr &right) const {
    TypePtr promotedLeft = promoteIntegerType(left);
    TypePtr promotedRight = promoteIntegerType(right);
    if (sameType(promotedLeft, promotedRight)) {
        return promotedLeft;
    }

    if (promotedLeft->isUnsignedInteger() == promotedRight->isUnsignedInteger()) {
        return integerRank(promotedLeft) >= integerRank(promotedRight) ? promotedLeft : promotedRight;
    }

    const TypePtr &unsignedType = promotedLeft->isUnsignedInteger() ? promotedLeft : promotedRight;
    const TypePtr &signedType = promotedLeft->isUnsignedInteger() ? promotedRight : promotedLeft;
    if (integerRank(unsignedType) >= integerRank(signedType)) {
        return unsignedType;
    }
    if (canRepresentAllValues(signedType, unsignedType)) {
        return signedType;
    }

    switch (signedType->kind) {
    case TypeKind::Int:
        return Type::makeUnsignedInt();
    case TypeKind::Long:
        return Type::makeUnsignedLong();
    case TypeKind::LongLong:
        return Type::makeUnsignedLongLong();
    default:
        return unsignedType;
    }
}

TypePtr SemanticAnalyzer::commonArithmeticType(const TypePtr &left, const TypePtr &right) const {
    if (left->isFloating() || right->isFloating()) {
        if (left->kind == TypeKind::Double || right->kind == TypeKind::Double) {
            return Type::makeDouble();
        }
        return Type::makeFloat();
    }
    return commonIntegerType(left, right);
}

int SemanticAnalyzer::integerRank(const TypePtr &type) const {
    switch (type->kind) {
    case TypeKind::Bool:
        return 0;
    case TypeKind::Char:
    case TypeKind::UnsignedChar:
        return 1;
    case TypeKind::Short:
    case TypeKind::UnsignedShort:
        return 2;
    case TypeKind::Int:
    case TypeKind::UnsignedInt:
        return 3;
    case TypeKind::Long:
    case TypeKind::UnsignedLong:
        return 4;
    case TypeKind::LongLong:
    case TypeKind::UnsignedLongLong:
        return 5;
    default:
        return 0;
    }
}

bool SemanticAnalyzer::canRepresentAllValues(const TypePtr &target, const TypePtr &source) const {
    if (!target->isInteger() || !source->isInteger()) {
        return false;
    }
    if (target->isUnsignedInteger()) {
        return false;
    }
    return target->valueSize() > source->valueSize();
}

std::string SemanticAnalyzer::typeName(const TypePtr &type) const {
    switch (type->kind) {
    case TypeKind::Bool:
        return "_Bool";
    case TypeKind::Char:
        return "char";
    case TypeKind::UnsignedChar:
        return "unsigned char";
    case TypeKind::Short:
        return "short";
    case TypeKind::UnsignedShort:
        return "unsigned short";
    case TypeKind::Int:
        return "int";
    case TypeKind::UnsignedInt:
        return "unsigned int";
    case TypeKind::Long:
        return "long";
    case TypeKind::UnsignedLong:
        return "unsigned long";
    case TypeKind::LongLong:
        return "long long";
    case TypeKind::UnsignedLongLong:
        return "unsigned long long";
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
    throw std::runtime_error("Semantic error: " + message);
}

int SemanticAnalyzer::alignTo(int value, int alignment) {
    return (value + alignment - 1) / alignment * alignment;
}
