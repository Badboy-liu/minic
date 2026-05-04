#include "Optimizer.h"

void Optimizer::optimize(Program &program) {
    for (auto &global : program.globals) {
        if (global.init) {
            optimizeExpr(global.init);
        }
    }
    for (auto &function : program.functions) {
        if (!function.isDeclaration()) {
            optimizeFunction(function);
        }
    }
}

void Optimizer::optimizeFunction(Function &function) {
    optimizeBlock(*function.body);
}

void Optimizer::optimizeBlock(BlockStmt &block) {
    for (auto &statement : block.statements) {
        optimizeStatement(*statement);
    }
}

void Optimizer::optimizeStatement(Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        auto &returnStmt = static_cast<ReturnStmt &>(stmt);
        if (returnStmt.expr) {
            optimizeExpr(returnStmt.expr);
        }
        return;
    }
    case Stmt::Kind::Expr:
        optimizeExpr(static_cast<ExprStmt &>(stmt).expr);
        return;
    case Stmt::Kind::Decl: {
        auto &decl = static_cast<DeclStmt &>(stmt);
        if (decl.init) {
            optimizeExpr(decl.init);
        }
        return;
    }
    case Stmt::Kind::Block:
        optimizeBlock(static_cast<BlockStmt &>(stmt));
        return;
    case Stmt::Kind::If: {
        auto &ifStmt = static_cast<IfStmt &>(stmt);
        optimizeExpr(ifStmt.condition);
        optimizeStatement(*ifStmt.thenBranch);
        if (ifStmt.elseBranch) {
            optimizeStatement(*ifStmt.elseBranch);
        }
        return;
    }
    case Stmt::Kind::While: {
        auto &whileStmt = static_cast<WhileStmt &>(stmt);
        optimizeExpr(whileStmt.condition);
        optimizeStatement(*whileStmt.body);
        return;
    }
    case Stmt::Kind::For: {
        auto &forStmt = static_cast<ForStmt &>(stmt);
        if (forStmt.init) {
            optimizeStatement(*forStmt.init);
        }
        if (forStmt.condition) {
            optimizeExpr(forStmt.condition);
        }
        if (forStmt.update) {
            optimizeExpr(forStmt.update);
        }
        optimizeStatement(*forStmt.body);
        return;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        return;
    }
}

void Optimizer::optimizeExpr(std::unique_ptr<Expr> &expr) {
    switch (expr->kind) {
    case Expr::Kind::Number:
    case Expr::Kind::String:
    case Expr::Kind::Variable:
        return;
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(*expr);
        optimizeExpr(unary.operand);
        int value = 0;
        if (!tryEvaluateIntegerConstant(*expr, value)) {
            return;
        }
        expr = makeFoldedNumber(value, *expr);
        return;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(*expr);
        optimizeExpr(binary.left);
        optimizeExpr(binary.right);
        int value = 0;
        if (!tryEvaluateIntegerConstant(*expr, value)) {
            return;
        }
        expr = makeFoldedNumber(value, *expr);
        return;
    }
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<InitializerListExpr &>(*expr);
        for (auto &element : list.elements) {
            optimizeExpr(element);
        }
        return;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<AssignExpr &>(*expr);
        optimizeExpr(assign.target);
        optimizeExpr(assign.value);
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<CallExpr &>(*expr);
        optimizeExpr(call.callee);
        for (auto &argument : call.arguments) {
            optimizeExpr(argument);
        }
        return;
    }
    case Expr::Kind::Index: {
        auto &index = static_cast<IndexExpr &>(*expr);
        optimizeExpr(index.base);
        optimizeExpr(index.index);
        return;
    }
    case Expr::Kind::MemberAccess: {
        auto &member = static_cast<MemberAccessExpr &>(*expr);
        optimizeExpr(member.base);
        return;
    }
    }
}

bool Optimizer::tryEvaluateIntegerConstant(const Expr &expr, int &value) {
    if (!expr.type || !expr.type->isInteger()) {
        return false;
    }

    switch (expr.kind) {
    case Expr::Kind::Number:
        value = static_cast<const NumberExpr &>(expr).value;
        return true;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        int operand = 0;
        if (!tryEvaluateIntegerConstant(*unary.operand, operand)) {
            return false;
        }
        switch (unary.op) {
        case UnaryOp::Plus:
            value = operand;
            return true;
        case UnaryOp::Minus:
            value = -operand;
            return true;
        case UnaryOp::LogicalNot:
            value = operand == 0 ? 1 : 0;
            return true;
        case UnaryOp::AddressOf:
        case UnaryOp::Dereference:
            return false;
        }
        return false;
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        int left = 0;
        int right = 0;
        if (!tryEvaluateIntegerConstant(*binary.left, left) ||
            !tryEvaluateIntegerConstant(*binary.right, right)) {
            return false;
        }
        switch (binary.op) {
        case BinaryOp::Add:
            value = left + right;
            return true;
        case BinaryOp::Subtract:
            value = left - right;
            return true;
        case BinaryOp::Multiply:
            value = left * right;
            return true;
        case BinaryOp::Divide:
            if (right == 0) {
                return false;
            }
            value = left / right;
            return true;
        case BinaryOp::Equal:
            value = left == right ? 1 : 0;
            return true;
        case BinaryOp::NotEqual:
            value = left != right ? 1 : 0;
            return true;
        case BinaryOp::LogicalAnd:
            value = (left != 0 && right != 0) ? 1 : 0;
            return true;
        case BinaryOp::LogicalOr:
            value = (left != 0 || right != 0) ? 1 : 0;
            return true;
        case BinaryOp::Less:
            value = left < right ? 1 : 0;
            return true;
        case BinaryOp::LessEqual:
            value = left <= right ? 1 : 0;
            return true;
        case BinaryOp::Greater:
            value = left > right ? 1 : 0;
            return true;
        case BinaryOp::GreaterEqual:
            value = left >= right ? 1 : 0;
            return true;
        }
        return false;
    }
    case Expr::Kind::String:
    case Expr::Kind::Variable:
    case Expr::Kind::InitializerList:
    case Expr::Kind::Assign:
    case Expr::Kind::Call:
    case Expr::Kind::Index:
    case Expr::Kind::MemberAccess:
        return false;
    }
    return false;
}

std::unique_ptr<Expr> Optimizer::makeFoldedNumber(int value, const Expr &original) {
    auto folded = std::make_unique<NumberExpr>(value);
    folded->type = original.type;
    folded->isLValue = false;
    return folded;
}
