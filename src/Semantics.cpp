#include "Semantics.h"

#include <stdexcept>

void SemanticAnalyzer::analyze(Program &program) {
    bool hasMain = false;
    functions.clear();
    globals.clear();
    globalSignatures.clear();

    for (auto &global : program.globals) {
        analyzeGlobal(global);
    }

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

        if (function.parameters.size() > 4) {
            fail("functions with more than 4 parameters are not supported yet: " + function.name);
        }
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
        if (parameter.type->isVoid() || parameter.type->isArray()) {
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
    if (global.type->isVoid()) {
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
        if (global.init->kind != Expr::Kind::String ||
            !global.type->elementType->equals(*Type::makeChar())) {
            fail("global array initializers currently support only string literals for char[]: " + global.name);
        }
        const auto &stringExpr = static_cast<const StringExpr &>(*global.init);
        if (static_cast<int>(stringExpr.value.size()) + 1 > global.type->arrayLength) {
            fail("string literal is too long for global array: " + global.name);
        }
        return;
    }

    if (global.init->kind != Expr::Kind::Number || !global.type->isInteger()) {
        fail("global initializers currently support only integer constants or char[] string literals: " + global.name);
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
                fail("array initializers are not supported yet");
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
        variable.isLValue = true;
        expr.type = symbol.type;
        expr.isLValue = true;
        return;
    }
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(expr);
        analyzeExpr(*unary.operand);
        switch (unary.op) {
        case UnaryOp::Plus:
        case UnaryOp::Minus:
            if (!unary.operand->type->isInteger()) {
                fail("unary +/- requires int operand");
            }
            expr.type = Type::makeInt();
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
            if (!unary.operand->isLValue) {
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
            expr.isLValue = true;
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
            if (leftType->isInteger() && rightType->isInteger()) {
                expr.type = Type::makeInt();
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
            if (leftType->isInteger() && rightType->isInteger()) {
                expr.type = Type::makeInt();
            } else if (leftType->isPointer() && rightType->isInteger()) {
                expr.type = leftType;
            } else {
                fail("invalid operands to '-'");
            }
            expr.isLValue = false;
            return;
        case BinaryOp::Multiply:
        case BinaryOp::Divide:
            if (!leftType->isInteger() || !rightType->isInteger()) {
                fail("arithmetic operator requires int operands");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        case BinaryOp::Equal:
        case BinaryOp::NotEqual:
            if (!(sameType(leftType, rightType) || (leftType->isScalar() && rightType->isScalar()))) {
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
            if (!leftType->isInteger() || !rightType->isInteger()) {
                fail("comparison operator requires int operands");
            }
            expr.type = Type::makeInt();
            expr.isLValue = false;
            return;
        }
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
        const auto found = functions.find(call.callee);
        if (found == functions.end()) {
            fail("call to undefined function: " + call.callee);
        }
        if (call.arguments.size() != found->second.parameterTypes.size()) {
            fail(
                "wrong number of arguments in call to " + call.callee + ": expected " +
                std::to_string(found->second.parameterTypes.size()) + ", got " + std::to_string(call.arguments.size()));
        }
        call.parameterTypes = found->second.parameterTypes;
        for (std::size_t i = 0; i < call.arguments.size(); ++i) {
            analyzeExpr(*call.arguments[i]);
            if (!isEquivalentArgumentType(found->second.parameterTypes[i], call.arguments[i]->type)) {
                fail(
                    "argument type mismatch in call to " + call.callee + ": expected " +
                    typeName(found->second.parameterTypes[i]) + ", got " + typeName(call.arguments[i]->type));
            }
        }
        expr.type = found->second.returnType;
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

    fail("use of undeclared variable: " + name);
}

bool SemanticAnalyzer::canAssign(const TypePtr &target, const TypePtr &value) const {
    TypePtr decayedValue = decayType(value);
    if (target->isInteger() && decayedValue->isInteger()) {
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
    return sameType(param, decayedArg);
}

TypePtr SemanticAnalyzer::decayType(const TypePtr &type) const {
    return type->decay();
}

std::string SemanticAnalyzer::typeName(const TypePtr &type) const {
    switch (type->kind) {
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
    case TypeKind::Void:
        return "void";
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
