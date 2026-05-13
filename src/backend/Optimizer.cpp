#include "Optimizer.h"
#include "PassManager.h"

#include <functional>

void Optimizer::optimize(Program &program, int level) {
    optLevel = level;
    if (optLevel <= 0) return;

    // 收集可内联函数的元信息（不移动 AST 节点）
    collectInlineCandidates(program);

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
    constantEnv.clear();
    floatConstantEnv.clear();

    PassManager pm;
    if (optLevel >= 2) {
        pm.addPass(std::make_unique<ConstantPropagationPass>());
        pm.addPass(std::make_unique<CopyPropagationPass>());
        pm.addPass(std::make_unique<TailRecursionPass>());
        pm.addPass(std::make_unique<LoopUnrollPass>());
    }
    pm.addPass(std::make_unique<ConstantFoldingPass>());
    if (optLevel >= 2) {
        pm.addPass(std::make_unique<CSEPass>());
        pm.addPass(std::make_unique<DeadStoreElimPass>());
    }
    pm.runFunction(function, *this);
}

void Optimizer::optimizeBlock(BlockStmt &block) {
    // 先对每条语句做常量折叠
    for (auto &statement : block.statements) {
        optimizeStatement(*statement);
    }
    if (optLevel >= 2) {
        // 死代码消除
        eliminateDeadCode(block);
        // 循环不变量外提
        hoistInvariantsFromBlock(block);
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
    case Stmt::Kind::DoWhile: {
        auto &doWhileStmt = static_cast<DoWhileStmt &>(stmt);
        optimizeStatement(*doWhileStmt.body);
        optimizeExpr(doWhileStmt.condition);
        return;
    }
    case Stmt::Kind::Switch: {
        auto &sw = static_cast<SwitchStmt &>(stmt);
        optimizeExpr(sw.scrutinee);
        for (auto &c : sw.cases) {
            optimizeExpr(c.label);
            optimizeStatement(*c.body);
        }
        if (sw.defaultBody) {
            optimizeStatement(*sw.defaultBody);
        }
        return;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        return;
    case Stmt::Kind::Goto:
        return;
    case Stmt::Kind::Label:
        optimizeStatement(*static_cast<LabelStmt &>(stmt).body);
        return;
    }
}

void Optimizer::optimizeExpr(std::unique_ptr<Expr> &expr) {
    switch (expr->kind) {
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
    case Expr::Kind::Variable:
        return;
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(*expr);
        if (unary.operand) {
            optimizeExpr(unary.operand);
        }
        long long value = 0;
        if (tryEvaluateIntegerConstant(*expr, value)) {
            expr = makeFoldedNumber(value, *expr);
            return;
        }
        double fval = 0;
        if (tryEvaluateFloatConstant(*expr, fval)) {
            expr = makeFoldedFloat(fval, *expr);
            return;
        }
        return;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(*expr);
        optimizeExpr(binary.left);
        optimizeExpr(binary.right);
        long long value = 0;
        if (tryEvaluateIntegerConstant(*expr, value)) {
            expr = makeFoldedNumber(value, *expr);
            return;
        }
        double fval = 0;
        if (tryEvaluateFloatConstant(*expr, fval)) {
            expr = makeFoldedFloat(fval, *expr);
            return;
        }
        if (optLevel >= 2 && expr->type && expr->type->isInteger()) {
            // 算术恒等式消除：x+0→x, x*1→x, x*0→0, x-0→x, x/1→x, etc.
            if (applyArithmeticIdentity(expr)) {
                return;
            }
            // 强度削减：x * 2^n → x << n, x % 2^n → x & (2^n-1)
            applyStrengthReduction(expr);
        }
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
        // 尝试函数内联
        if (optLevel >= 2) {
            tryInlineCall(expr);
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
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<TernaryExpr &>(*expr);
        optimizeExpr(ternary.condition);
        optimizeExpr(ternary.thenExpr);
        optimizeExpr(ternary.elseExpr);
        long long value = 0;
        if (tryEvaluateIntegerConstant(*expr, value)) {
            expr = makeFoldedNumber(value, *expr);
            return;
        }
        double fval = 0;
        if (tryEvaluateFloatConstant(*expr, fval)) {
            expr = makeFoldedFloat(fval, *expr);
            return;
        }
        return;
    }
    case Expr::Kind::Cast: {
        auto &cast = static_cast<CastExpr &>(*expr);
        optimizeExpr(cast.operand);
        long long value = 0;
        if (tryEvaluateIntegerConstant(*expr, value)) {
            expr = makeFoldedNumber(value, *expr);
            return;
        }
        double fval = 0;
        if (tryEvaluateFloatConstant(*expr, fval)) {
            expr = makeFoldedFloat(fval, *expr);
            return;
        }
        return;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<StmtExpr &>(*expr);
        for (auto &stmt : se.statements) {
            optimizeStatement(*stmt);
        }
        if (se.result) {
            optimizeExpr(se.result);
        }
        return;
    }
    }
}

bool Optimizer::tryEvaluateIntegerConstant(const Expr &expr, long long &value) {
    if (!expr.type || !expr.type->isInteger()) {
        return false;
    }

    switch (expr.kind) {
    case Expr::Kind::Number:
        value = static_cast<const NumberExpr &>(expr).value;
        return true;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (!unary.operand) {
            return false;
        }
        long long operand = 0;
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
        case UnaryOp::BitwiseNot:
            value = ~operand;
            return true;
        case UnaryOp::Sizeof:
            return false;
        case UnaryOp::AddressOf:
        case UnaryOp::Dereference:
        case UnaryOp::PreIncrement:
        case UnaryOp::PreDecrement:
        case UnaryOp::PostIncrement:
        case UnaryOp::PostDecrement:
            return false;
        }
        return false;
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        long long left = 0;
        long long right = 0;
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
        case BinaryOp::Modulo:
            if (right == 0) {
                return false;
            }
            value = left % right;
            return true;
        case BinaryOp::ShiftLeft:
            value = left << right;
            return true;
        case BinaryOp::ShiftRight:
            value = left >> right;
            return true;
        case BinaryOp::BitwiseAnd:
            value = left & right;
            return true;
        case BinaryOp::BitwiseXor:
            value = left ^ right;
            return true;
        case BinaryOp::BitwiseOr:
            value = left | right;
            return true;
        case BinaryOp::Comma:
            value = right;
            return true;
        }
        return false;
    }
    case Expr::Kind::Ternary: {
        const auto &ternary = static_cast<const TernaryExpr &>(expr);
        long long cond = 0;
        if (!tryEvaluateIntegerConstant(*ternary.condition, cond)) {
            return false;
        }
        if (cond != 0) {
            return tryEvaluateIntegerConstant(*ternary.thenExpr, value);
        }
        return tryEvaluateIntegerConstant(*ternary.elseExpr, value);
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        long long operandValue = 0;
        if (!tryEvaluateIntegerConstant(*cast.operand, operandValue)) {
            return false;
        }
        // 类型转换截断
        const int dstSize = cast.targetType->valueSize();
        if (dstSize == 1) {
            value = static_cast<char>(static_cast<unsigned char>(operandValue & 0xFF));
        } else if (dstSize == 2) {
            value = static_cast<short>(static_cast<unsigned short>(operandValue & 0xFFFF));
        } else {
            value = operandValue;
        }
        return true;
    }
    case Expr::Kind::FloatLiteral:
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

std::unique_ptr<Expr> Optimizer::makeFoldedNumber(long long value, const Expr &original) {
    auto folded = std::make_unique<NumberExpr>(value);
    folded->type = original.type;
    folded->isLValue = false;
    return folded;
}

bool Optimizer::tryEvaluateFloatConstant(const Expr &expr, double &value) {
    if (!expr.type || !expr.type->isFloatingPoint()) return false;

    switch (expr.kind) {
    case Expr::Kind::FloatLiteral:
        value = static_cast<const FloatLiteralExpr &>(expr).value;
        return true;
    case Expr::Kind::Number:
        value = static_cast<double>(static_cast<const NumberExpr &>(expr).value);
        return true;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (!unary.operand) return false;
        double operand = 0;
        if (!tryEvaluateFloatConstant(*unary.operand, operand)) return false;
        switch (unary.op) {
        case UnaryOp::Plus:
            value = operand;
            return true;
        case UnaryOp::Minus:
            value = -operand;
            return true;
        default:
            return false;
        }
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        double left = 0, right = 0;
        if (!tryEvaluateFloatConstant(*binary.left, left) ||
            !tryEvaluateFloatConstant(*binary.right, right)) return false;
        switch (binary.op) {
        case BinaryOp::Add:      value = left + right; return true;
        case BinaryOp::Subtract: value = left - right; return true;
        case BinaryOp::Multiply: value = left * right; return true;
        case BinaryOp::Divide:
            if (right == 0.0) return false;
            value = left / right;
            return true;
        case BinaryOp::Equal:        value = (left == right) ? 1.0 : 0.0; return true;
        case BinaryOp::NotEqual:     value = (left != right) ? 1.0 : 0.0; return true;
        case BinaryOp::Less:         value = (left < right) ? 1.0 : 0.0; return true;
        case BinaryOp::LessEqual:    value = (left <= right) ? 1.0 : 0.0; return true;
        case BinaryOp::Greater:      value = (left > right) ? 1.0 : 0.0; return true;
        case BinaryOp::GreaterEqual: value = (left >= right) ? 1.0 : 0.0; return true;
        default: return false;
        }
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        if (cast.targetType && cast.targetType->isFloatingPoint()) {
            double operand = 0;
            if (tryEvaluateFloatConstant(*cast.operand, operand)) {
                value = operand;
                return true;
            }
            long long intVal = 0;
            if (tryEvaluateIntegerConstant(*cast.operand, intVal)) {
                value = static_cast<double>(intVal);
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

std::unique_ptr<Expr> Optimizer::makeFoldedFloat(double value, const Expr &original) {
    auto folded = std::make_unique<FloatLiteralExpr>(value);
    folded->type = original.type;
    folded->isLValue = false;
    return folded;
}

// 强度削减：x * 2^n → x << n, x % 2^n → x & (2^n-1)
void Optimizer::applyStrengthReduction(std::unique_ptr<Expr> &expr) {
    if (expr->kind != Expr::Kind::Binary) return;
    auto &binary = static_cast<BinaryExpr &>(*expr);

    // 检查右操作数是否为常量
    long long rightValue = 0;
    if (!tryEvaluateIntegerConstant(*binary.right, rightValue)) return;

    // x * 2^n → x << n (当 2^n > 0 且不溢出)
    if (binary.op == BinaryOp::Multiply && rightValue > 0 && (rightValue & (rightValue - 1)) == 0) {
        long long shiftAmount = 0;
        long long temp = rightValue;
        while (temp > 1) { temp >>= 1; ++shiftAmount; }
        auto shiftExpr = std::make_unique<BinaryExpr>(BinaryOp::ShiftLeft, std::move(binary.left),
            std::make_unique<NumberExpr>(shiftAmount));
        shiftExpr->type = expr->type;
        shiftExpr->isLValue = false;
        expr = std::move(shiftExpr);
        return;
    }

    // x % 2^n → x & (2^n-1) (仅对无符号类型安全)
    if (binary.op == BinaryOp::Modulo && rightValue > 0 && (rightValue & (rightValue - 1)) == 0 &&
        expr->type && expr->type->isUnsigned) {
        long long mask = rightValue - 1;
        auto andExpr = std::make_unique<BinaryExpr>(BinaryOp::BitwiseAnd, std::move(binary.left),
            std::make_unique<NumberExpr>(mask));
        andExpr->type = expr->type;
        andExpr->isLValue = false;
        expr = std::move(andExpr);
        return;
    }

    // x / 2^n → x >> n (仅对无符号类型安全，有符号右移是实现定义的)
    if (binary.op == BinaryOp::Divide && rightValue > 0 && (rightValue & (rightValue - 1)) == 0 &&
        expr->type && expr->type->isUnsigned) {
        long long shiftAmount = 0;
        long long temp = rightValue;
        while (temp > 1) { temp >>= 1; ++shiftAmount; }
        auto shiftExpr = std::make_unique<BinaryExpr>(BinaryOp::ShiftRight, std::move(binary.left),
            std::make_unique<NumberExpr>(shiftAmount));
        shiftExpr->type = expr->type;
        shiftExpr->isLValue = false;
        expr = std::move(shiftExpr);
        return;
    }

    // x * (2^n + 1) → (x << n) + x
    if (binary.op == BinaryOp::Multiply && rightValue > 2) {
        long long shiftAmount = 0;
        long long temp = rightValue;
        while (temp > 1) { temp >>= 1; ++shiftAmount; }
        if ((1LL << shiftAmount) + 1 == rightValue) {
            auto shifted = std::make_unique<BinaryExpr>(BinaryOp::ShiftLeft,
                std::move(binary.left), std::make_unique<NumberExpr>(shiftAmount));
            shifted->type = expr->type;
            shifted->isLValue = false;
            auto addExpr = std::make_unique<BinaryExpr>(BinaryOp::Add,
                std::move(shifted), std::move(binary.left));
            addExpr->type = expr->type;
            addExpr->isLValue = false;
            expr = std::move(addExpr);
            return;
        }
    }

    // x * (2^n - 1) → (x << n) - x
    if (binary.op == BinaryOp::Multiply && rightValue > 2) {
        long long shiftAmount = 0;
        long long temp = rightValue + 1;
        while (temp > 1) { temp >>= 1; ++shiftAmount; }
        if ((1LL << shiftAmount) - 1 == rightValue) {
            auto shifted = std::make_unique<BinaryExpr>(BinaryOp::ShiftLeft,
                std::move(binary.left), std::make_unique<NumberExpr>(shiftAmount));
            shifted->type = expr->type;
            shifted->isLValue = false;
            auto subExpr = std::make_unique<BinaryExpr>(BinaryOp::Subtract,
                std::move(shifted), std::move(binary.left));
            subExpr->type = expr->type;
            subExpr->isLValue = false;
            expr = std::move(subExpr);
            return;
        }
    }
}

bool Optimizer::applyArithmeticIdentity(std::unique_ptr<Expr> &expr) {
    if (expr->kind != Expr::Kind::Binary) return false;
    auto &binary = static_cast<BinaryExpr &>(*expr);
    long long leftVal = 0, rightVal = 0;
    const bool leftIsConst = tryEvaluateIntegerConstant(*binary.left, leftVal);
    const bool rightIsConst = tryEvaluateIntegerConstant(*binary.right, rightVal);

    switch (binary.op) {
    case BinaryOp::Add:
        if (rightIsConst && rightVal == 0) { expr = std::move(binary.left); return true; }
        if (leftIsConst && leftVal == 0) { expr = std::move(binary.right); return true; }
        break;
    case BinaryOp::Subtract:
        if (rightIsConst && rightVal == 0) { expr = std::move(binary.left); return true; }
        break;
    case BinaryOp::Multiply:
        if (rightIsConst && rightVal == 1) { expr = std::move(binary.left); return true; }
        if (leftIsConst && leftVal == 1) { expr = std::move(binary.right); return true; }
        if ((rightIsConst && rightVal == 0) || (leftIsConst && leftVal == 0)) {
            expr = std::make_unique<NumberExpr>(0);
            expr->type = binary.type;
            expr->isLValue = false;
            return true;
        }
        break;
    case BinaryOp::Divide:
        if (rightIsConst && rightVal == 1) { expr = std::move(binary.left); return true; }
        break;
    case BinaryOp::BitwiseAnd:
        if (rightIsConst && rightVal == -1) { expr = std::move(binary.left); return true; }
        if (leftIsConst && leftVal == -1) { expr = std::move(binary.right); return true; }
        if ((rightIsConst && rightVal == 0) || (leftIsConst && leftVal == 0)) {
            expr = std::make_unique<NumberExpr>(0);
            expr->type = binary.type;
            expr->isLValue = false;
            return true;
        }
        // x & x → x
        if (binary.left->kind == Expr::Kind::Variable && binary.right->kind == Expr::Kind::Variable &&
            static_cast<VariableExpr &>(*binary.left).name == static_cast<VariableExpr &>(*binary.right).name) {
            expr = std::move(binary.left);
            return true;
        }
        break;
    case BinaryOp::BitwiseOr:
        if (rightIsConst && rightVal == 0) { expr = std::move(binary.left); return true; }
        if (leftIsConst && leftVal == 0) { expr = std::move(binary.right); return true; }
        if (rightIsConst && rightVal == -1) {
            expr = std::make_unique<NumberExpr>(-1);
            expr->type = binary.type;
            expr->isLValue = false;
            return true;
        }
        // x | x → x
        if (binary.left->kind == Expr::Kind::Variable && binary.right->kind == Expr::Kind::Variable &&
            static_cast<VariableExpr &>(*binary.left).name == static_cast<VariableExpr &>(*binary.right).name) {
            expr = std::move(binary.left);
            return true;
        }
        break;
    case BinaryOp::BitwiseXor:
        if (rightIsConst && rightVal == 0) { expr = std::move(binary.left); return true; }
        if (leftIsConst && leftVal == 0) { expr = std::move(binary.right); return true; }
        // x ^ x → 0
        if (binary.left->kind == Expr::Kind::Variable && binary.right->kind == Expr::Kind::Variable &&
            static_cast<VariableExpr &>(*binary.left).name == static_cast<VariableExpr &>(*binary.right).name) {
            expr = std::make_unique<NumberExpr>(0);
            expr->type = binary.type;
            expr->isLValue = false;
            return true;
        }
        break;
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
        if (rightIsConst && rightVal == 0) { expr = std::move(binary.left); return true; }
        break;
    default:
        break;
    }
    return false;
}

// 从块中的 while 循环提取不变量赋值，放到循环之前
void Optimizer::hoistInvariantsFromBlock(BlockStmt &block) {
    for (std::size_t i = 0; i < block.statements.size(); ++i) {
        auto &stmt = block.statements[i];

        // 提取循环体、条件和被修改变量
        BlockStmt *body = nullptr;
        Expr *condition = nullptr;
        std::unique_ptr<Stmt> *loopBodyPtr = nullptr;

        if (stmt->kind == Stmt::Kind::While) {
            auto &whileStmt = static_cast<WhileStmt &>(*stmt);
            if (!whileStmt.body || whileStmt.body->kind != Stmt::Kind::Block) continue;
            body = static_cast<BlockStmt *>(whileStmt.body.get());
            condition = whileStmt.condition.get();
            loopBodyPtr = &whileStmt.body;
        } else if (stmt->kind == Stmt::Kind::For) {
            auto &forStmt = static_cast<ForStmt &>(*stmt);
            if (!forStmt.body || forStmt.body->kind != Stmt::Kind::Block) continue;
            body = static_cast<BlockStmt *>(forStmt.body.get());
            condition = forStmt.condition.get();
            loopBodyPtr = &forStmt.body;
        } else if (stmt->kind == Stmt::Kind::DoWhile) {
            auto &doStmt = static_cast<DoWhileStmt &>(*stmt);
            if (!doStmt.body || doStmt.body->kind != Stmt::Kind::Block) continue;
            body = static_cast<BlockStmt *>(doStmt.body.get());
            condition = doStmt.condition.get();
            loopBodyPtr = &doStmt.body;
        } else {
            continue;
        }

        // 收集循环体内被修改的变量
        std::unordered_set<std::string> modifiedVars;
        collectModifiedVars(**loopBodyPtr, modifiedVars);

        // 收集循环条件中使用的变量
        std::unordered_set<std::string> condVars;
        if (condition) {
            collectUsedVars(*condition, condVars);
        }

        std::vector<std::unique_ptr<Stmt>> hoisted;

        for (auto it = body->statements.begin(); it != body->statements.end(); ) {
            if ((*it)->kind != Stmt::Kind::Expr) { ++it; continue; }
            auto &exprStmt = static_cast<ExprStmt &>(**it);
            if (exprStmt.expr->kind != Expr::Kind::Assign) { ++it; continue; }
            auto &assign = static_cast<AssignExpr &>(*exprStmt.expr);
            if (assign.isCompound || assign.target->kind != Expr::Kind::Variable) { ++it; continue; }

            auto &targetVar = static_cast<VariableExpr &>(*assign.target);
            // 目标变量不能在条件中使用
            if (condVars.count(targetVar.name)) { ++it; continue; }
            // 表达式必须是循环不变的
            if (!isLoopInvariant(*assign.value, modifiedVars)) { ++it; continue; }

            hoisted.push_back(std::move(*it));
            it = body->statements.erase(it);
        }

        // 将外提的语句插入到循环之前
        if (!hoisted.empty()) {
            auto pos = block.statements.begin() + static_cast<std::ptrdiff_t>(i);
            for (auto &h : hoisted) {
                pos = block.statements.insert(pos, std::move(h));
                ++pos;
            }
        }
    }
}

// 尾递归消除：将尾递归调用转换为循环
void Optimizer::eliminateTailRecursion(Function &function) {
    if (!function.body || function.body->statements.empty()) return;

    // 检查最后一条语句是否为 return f(args)
    auto &lastStmt = function.body->statements.back();
    if (lastStmt->kind != Stmt::Kind::Return) return;
    auto &returnStmt = static_cast<ReturnStmt &>(*lastStmt);
    if (!returnStmt.expr || returnStmt.expr->kind != Expr::Kind::Call) return;
    auto &call = static_cast<CallExpr &>(*returnStmt.expr);
    if (call.callee->kind != Expr::Kind::Variable) return;
    const auto &callName = static_cast<VariableExpr &>(*call.callee).name;
    if (callName != function.name) return;
    if (call.arguments.size() != function.parameters.size()) return;

    // 生成参数赋值语句：param_i = arg_i
    std::vector<std::unique_ptr<Stmt>> assignments;
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        auto target = std::make_unique<VariableExpr>(function.parameters[i].name);
        target->type = function.parameters[i].type;
        target->isLValue = true;
        auto assign = std::make_unique<AssignExpr>(std::move(target), std::move(call.arguments[i]));
        assign->type = function.parameters[i].type;
        assign->isLValue = false;
        auto exprStmt = std::make_unique<ExprStmt>(std::move(assign));
        assignments.push_back(std::move(exprStmt));
    }

    // 移除 return 语句
    function.body->statements.pop_back();

    // 将原始函数体包装在 while(true) { ... } 中
    auto loopBody = std::make_unique<BlockStmt>();
    // 先添加参数赋值（放在循环体开头，模拟参数更新）
    for (auto &a : assignments) {
        loopBody->statements.push_back(std::move(a));
    }
    // 添加原始函数体
    for (auto &stmt : function.body->statements) {
        loopBody->statements.push_back(std::move(stmt));
    }
    function.body->statements.clear();

    // 创建 while(true) 条件
    auto trueExpr = std::make_unique<NumberExpr>(1);
    trueExpr->type = Type::makeInt();
    trueExpr->isLValue = false;
    auto whileStmt = std::make_unique<WhileStmt>(std::move(trueExpr), std::move(loopBody));
    function.body->statements.push_back(std::move(whileStmt));

    // 添加一个不可达的 return 0，避免语义分析警告
    auto zero = std::make_unique<NumberExpr>(0);
    zero->type = function.returnType;
    zero->isLValue = false;
    function.body->statements.push_back(std::make_unique<ReturnStmt>(std::move(zero)));
}

bool Optimizer::isLoopInvariant(const Expr &expr, const std::unordered_set<std::string> &modifiedVars) {
    switch (expr.kind) {
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
        return true;
    case Expr::Kind::Variable:
        return modifiedVars.count(static_cast<const VariableExpr &>(expr).name) == 0;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (!unary.operand) return false;
        return isLoopInvariant(*unary.operand, modifiedVars);
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        return isLoopInvariant(*binary.left, modifiedVars) && isLoopInvariant(*binary.right, modifiedVars);
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        return isLoopInvariant(*cast.operand, modifiedVars);
    }
    default:
        return false; // 保守处理：调用、索引等不外提
    }
}

void Optimizer::collectModifiedVars(const Stmt &stmt, std::unordered_set<std::string> &vars) {
    switch (stmt.kind) {
    case Stmt::Kind::Block:
        for (const auto &s : static_cast<const BlockStmt &>(stmt).statements) {
            collectModifiedVars(*s, vars);
        }
        break;
    case Stmt::Kind::Expr: {
        const auto &exprStmt = static_cast<const ExprStmt &>(stmt);
        if (exprStmt.expr->kind == Expr::Kind::Assign) {
            const auto &assign = static_cast<const AssignExpr &>(*exprStmt.expr);
            if (assign.target->kind == Expr::Kind::Variable) {
                vars.insert(static_cast<const VariableExpr &>(*assign.target).name);
            }
        }
        break;
    }
    case Stmt::Kind::Decl:
        vars.insert(static_cast<const DeclStmt &>(stmt).name);
        break;
    case Stmt::Kind::If: {
        const auto &ifStmt = static_cast<const IfStmt &>(stmt);
        collectModifiedVars(*ifStmt.thenBranch, vars);
        if (ifStmt.elseBranch) collectModifiedVars(*ifStmt.elseBranch, vars);
        break;
    }
    default:
        break;
    }
}

void Optimizer::collectUsedVars(const Expr &expr, std::unordered_set<std::string> &vars) {
    switch (expr.kind) {
    case Expr::Kind::Variable:
        vars.insert(static_cast<const VariableExpr &>(expr).name);
        break;
    case Expr::Kind::Unary:
        if (static_cast<const UnaryExpr &>(expr).operand) {
            collectUsedVars(*static_cast<const UnaryExpr &>(expr).operand, vars);
        }
        break;
    case Expr::Kind::Binary:
        collectUsedVars(*static_cast<const BinaryExpr &>(expr).left, vars);
        collectUsedVars(*static_cast<const BinaryExpr &>(expr).right, vars);
        break;
    case Expr::Kind::Assign: {
        const auto &assign = static_cast<const AssignExpr &>(expr);
        if (assign.target) collectUsedVars(*assign.target, vars);
        if (assign.value) collectUsedVars(*assign.value, vars);
        break;
    }
    case Expr::Kind::Call:
        for (const auto &arg : static_cast<const CallExpr &>(expr).arguments) {
            collectUsedVars(*arg, vars);
        }
        break;
    case Expr::Kind::Index:
        collectUsedVars(*static_cast<const IndexExpr &>(expr).base, vars);
        collectUsedVars(*static_cast<const IndexExpr &>(expr).index, vars);
        break;
    case Expr::Kind::Ternary:
        collectUsedVars(*static_cast<const TernaryExpr &>(expr).condition, vars);
        collectUsedVars(*static_cast<const TernaryExpr &>(expr).thenExpr, vars);
        collectUsedVars(*static_cast<const TernaryExpr &>(expr).elseExpr, vars);
        break;
    case Expr::Kind::Cast:
        collectUsedVars(*static_cast<const CastExpr &>(expr).operand, vars);
        break;
    default:
        break;
    }
}

// ===================== 常量传播 =====================

// 在表达式中替换已知常量变量为数字字面量
void Optimizer::propagateExpr(std::unique_ptr<Expr> &expr) {
    if (!expr) return;

    switch (expr->kind) {
    case Expr::Kind::Variable: {
        auto &var = static_cast<VariableExpr &>(*expr);
        auto it = constantEnv.find(var.name);
        if (it != constantEnv.end()) {
            // 替换为整数常量，保留原类型
            auto num = std::make_unique<NumberExpr>(it->second);
            num->type = expr->type;
            num->isLValue = false;
            expr = std::move(num);
            return;
        }
        auto fit = floatConstantEnv.find(var.name);
        if (fit != floatConstantEnv.end()) {
            // 替换为浮点常量，保留原类型
            auto flt = std::make_unique<FloatLiteralExpr>(fit->second);
            flt->type = expr->type;
            flt->isLValue = false;
            expr = std::move(flt);
        }
        return;
    }
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(*expr);
        // 自增自减会修改变量，使其常量失效
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement ||
            unary.op == UnaryOp::PostIncrement || unary.op == UnaryOp::PostDecrement) {
            if (unary.operand && unary.operand->kind == Expr::Kind::Variable) {
                auto &name = static_cast<VariableExpr &>(*unary.operand).name;
                constantEnv.erase(name);
                floatConstantEnv.erase(name);
            }
        }
        if (unary.op == UnaryOp::AddressOf) {
            // 取地址的结果不应被当作常量传播
            return;
        }
        if (unary.op == UnaryOp::Dereference) {
            // 解引用可能读取被修改的内存，不传播
            return;
        }
        propagateExpr(unary.operand);
        return;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(*expr);
        propagateExpr(binary.left);
        propagateExpr(binary.right);
        return;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<AssignExpr &>(*expr);
        // 先传播右侧（值）中的变量引用
        propagateExpr(assign.value);
        // 注意：不传播赋值目标（左值），因为赋值目标需要是可寻址的变量
        // propagateExpr(assign.target);  // 错误！会把赋值目标替换为常量

        // 如果赋值目标是简单变量
        if (assign.target->kind == Expr::Kind::Variable) {
            auto &varName = static_cast<VariableExpr &>(*assign.target).name;
            if (assign.isCompound) {
                // 复合赋值：旧值未知，不能设为常量
                constantEnv.erase(varName);
                floatConstantEnv.erase(varName);
            } else if (assign.value->kind == Expr::Kind::Number) {
                // 简单赋值为整数常量
                constantEnv[varName] = static_cast<NumberExpr &>(*assign.value).value;
                floatConstantEnv.erase(varName);
            } else if (assign.value->kind == Expr::Kind::FloatLiteral) {
                // 简单赋值为浮点常量
                floatConstantEnv[varName] = static_cast<FloatLiteralExpr &>(*assign.value).value;
                constantEnv.erase(varName);
            } else {
                // 赋值为非常量表达式，移除旧映射
                constantEnv.erase(varName);
                floatConstantEnv.erase(varName);
            }
        }
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<CallExpr &>(*expr);
        propagateExpr(call.callee);
        // 函数调用可能修改任何变量，清除所有常量映射
        constantEnv.clear();
        floatConstantEnv.clear();
        for (auto &arg : call.arguments) {
            propagateExpr(arg);
        }
        return;
    }
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<TernaryExpr &>(*expr);
        propagateExpr(ternary.condition);
        propagateExpr(ternary.thenExpr);
        propagateExpr(ternary.elseExpr);
        return;
    }
    case Expr::Kind::Cast: {
        auto &cast = static_cast<CastExpr &>(*expr);
        propagateExpr(cast.operand);
        return;
    }
    case Expr::Kind::Index: {
        auto &index = static_cast<IndexExpr &>(*expr);
        propagateExpr(index.base);
        propagateExpr(index.index);
        return;
    }
    case Expr::Kind::MemberAccess: {
        auto &member = static_cast<MemberAccessExpr &>(*expr);
        propagateExpr(member.base);
        return;
    }
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<InitializerListExpr &>(*expr);
        for (auto &element : list.elements) {
            propagateExpr(element);
        }
        return;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<StmtExpr &>(*expr);
        for (auto &stmt : se.statements) {
            propagateStatement(*stmt);
        }
        propagateExpr(se.result);
        return;
    }
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
    case Expr::Kind::CompoundLiteral:
    case Expr::Kind::BuiltinVaStart:
    case Expr::Kind::BuiltinVaArg:
    case Expr::Kind::BuiltinVaEnd:
    case Expr::Kind::Generic:
        return;
    }
}

// 常量传播：遍历语句，追踪变量赋值并替换已知常量
void Optimizer::propagateStatement(Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Decl: {
        auto &decl = static_cast<DeclStmt &>(stmt);
        // 传播初始化表达式中的变量
        propagateExpr(decl.init);
        // 静态局部变量跨调用持久化，不能作为常量传播
        if (decl.isStatic) {
            constantEnv.erase(decl.name);
            floatConstantEnv.erase(decl.name);
            return;
        }
        // 如果初始化为常量，记录到常量环境
        if (decl.init && decl.init->kind == Expr::Kind::Number) {
            // 不追踪指针类型的变量（可能取了地址）
            if (!decl.type->isPointer()) {
                constantEnv[decl.name] = static_cast<NumberExpr &>(*decl.init).value;
            }
            floatConstantEnv.erase(decl.name);
        } else if (decl.init && decl.init->kind == Expr::Kind::FloatLiteral) {
            if (!decl.type->isPointer()) {
                floatConstantEnv[decl.name] = static_cast<FloatLiteralExpr &>(*decl.init).value;
            }
            constantEnv.erase(decl.name);
        } else {
            // 初始化为非常量或无初始化，确保不误用旧映射
            // 注意：新声明的变量如果没有初始化，值未定义，不应传播
            constantEnv.erase(decl.name);
            floatConstantEnv.erase(decl.name);
        }
        return;
    }
    case Stmt::Kind::Expr: {
        propagateExpr(static_cast<ExprStmt &>(stmt).expr);
        return;
    }
    case Stmt::Kind::Return: {
        auto &ret = static_cast<ReturnStmt &>(stmt);
        propagateExpr(ret.expr);
        return;
    }
    case Stmt::Kind::Block:
        propagateBlock(static_cast<BlockStmt &>(stmt));
        return;
    case Stmt::Kind::If: {
        auto &ifStmt = static_cast<IfStmt &>(stmt);
        propagateExpr(ifStmt.condition);
        // then 和 else 分支可能修改变量，所以需要保存当前常量环境
        auto savedEnv = constantEnv;
        auto savedFloatEnv = floatConstantEnv;
        propagateStatement(*ifStmt.thenBranch);
        auto thenEnv = constantEnv;
        auto thenFloatEnv = floatConstantEnv;
        constantEnv = savedEnv;
        floatConstantEnv = savedFloatEnv;
        if (ifStmt.elseBranch) {
            propagateStatement(*ifStmt.elseBranch);
        }
        // 合并：两个分支都有的相同常量才保留
        for (auto it = constantEnv.begin(); it != constantEnv.end(); ) {
            auto thenIt = thenEnv.find(it->first);
            if (thenIt == thenEnv.end() || thenIt->second != it->second) {
                it = constantEnv.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = floatConstantEnv.begin(); it != floatConstantEnv.end(); ) {
            auto thenIt = thenFloatEnv.find(it->first);
            if (thenIt == thenFloatEnv.end() || thenIt->second != it->second) {
                it = floatConstantEnv.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    case Stmt::Kind::While: {
        auto &whileStmt = static_cast<WhileStmt &>(stmt);
        // 收集循环体中被修改的变量，仅传播循环体中未修改的常量到条件
        std::unordered_set<std::string> modified;
        collectModifiedVars(*whileStmt.body, modified);
        auto savedEnv = constantEnv;
        auto savedFloatEnv = floatConstantEnv;
        for (const auto &var : modified) {
            constantEnv.erase(var);
            floatConstantEnv.erase(var);
        }
        propagateExpr(whileStmt.condition);
        constantEnv = savedEnv;
        floatConstantEnv = savedFloatEnv;
        constantEnv.clear();
        floatConstantEnv.clear();
        return;
    }
    case Stmt::Kind::For: {
        auto &forStmt = static_cast<ForStmt &>(stmt);
        if (forStmt.init) {
            propagateStatement(*forStmt.init);
        }
        // 收集循环体和 update 中被修改的变量，仅传播未修改的常量到条件
        std::unordered_set<std::string> modified;
        collectModifiedVars(*forStmt.body, modified);
        if (forStmt.update) {
            collectUsedVars(*forStmt.update, modified);
        }
        auto savedEnv = constantEnv;
        auto savedFloatEnv = floatConstantEnv;
        for (const auto &var : modified) {
            constantEnv.erase(var);
            floatConstantEnv.erase(var);
        }
        propagateExpr(forStmt.condition);
        constantEnv = savedEnv;
        floatConstantEnv = savedFloatEnv;
        constantEnv.clear();
        floatConstantEnv.clear();
        return;
    }
    case Stmt::Kind::DoWhile: {
        auto &doWhileStmt = static_cast<DoWhileStmt &>(stmt);
        constantEnv.clear();
        floatConstantEnv.clear();
        propagateExpr(doWhileStmt.condition);
        return;
    }
    case Stmt::Kind::Switch: {
        auto &sw = static_cast<SwitchStmt &>(stmt);
        propagateExpr(sw.scrutinee);
        constantEnv.clear();
        floatConstantEnv.clear();
        return;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
    case Stmt::Kind::Goto:
        // 控制流跳转后常量环境不可靠，清除
        constantEnv.clear();
        floatConstantEnv.clear();
        return;
    case Stmt::Kind::Label: {
        auto &label = static_cast<LabelStmt &>(stmt);
        propagateStatement(*label.body);
        return;
    }
    case Stmt::Kind::StaticAssert:
        return;
    }
}

// 在块级别做常量传播
void Optimizer::propagateBlock(BlockStmt &block) {
    for (auto &statement : block.statements) {
        propagateStatement(*statement);
    }
}

// ===================== 复制传播 =====================

void Optimizer::applyCopyPropagation(Function &function) {
    copyEnv.clear();
    if (function.body) {
        copyPropagateBlock(*function.body);
    }
}

void Optimizer::copyPropagateBlock(BlockStmt &block) {
    for (auto &statement : block.statements) {
        copyPropagateStatement(*statement);
    }
}

void Optimizer::copyPropagateStatement(Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Decl: {
        auto &decl = static_cast<DeclStmt &>(stmt);
        copyPropagateExpr(decl.init);
        // 如果初始化为变量，记录复制关系
        if (decl.init && decl.init->kind == Expr::Kind::Variable) {
            copyEnv[decl.name] = static_cast<VariableExpr *>(decl.init.get());
        } else {
            copyEnv.erase(decl.name);
        }
        return;
    }
    case Stmt::Kind::Expr:
        copyPropagateExpr(static_cast<ExprStmt &>(stmt).expr);
        return;
    case Stmt::Kind::Return:
        copyPropagateExpr(static_cast<ReturnStmt &>(stmt).expr);
        return;
    case Stmt::Kind::Block:
        copyPropagateBlock(static_cast<BlockStmt &>(stmt));
        return;
    case Stmt::Kind::If: {
        auto &ifStmt = static_cast<IfStmt &>(stmt);
        copyPropagateExpr(ifStmt.condition);
        auto saved = copyEnv;
        copyPropagateStatement(*ifStmt.thenBranch);
        auto thenEnv = copyEnv;
        copyEnv = saved;
        if (ifStmt.elseBranch) {
            copyPropagateStatement(*ifStmt.elseBranch);
        }
        // 合并：两边都有的相同映射才保留（按源变量名比较）
        for (auto it = copyEnv.begin(); it != copyEnv.end();) {
            auto thenIt = thenEnv.find(it->first);
            if (thenIt == thenEnv.end() ||
                !thenIt->second || !it->second ||
                thenIt->second->name != it->second->name) {
                it = copyEnv.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    case Stmt::Kind::While: {
        auto &whileStmt = static_cast<WhileStmt &>(stmt);
        copyPropagateExpr(whileStmt.condition);
        copyEnv.clear();
        copyPropagateStatement(*whileStmt.body);
        copyEnv.clear();
        return;
    }
    case Stmt::Kind::For: {
        auto &forStmt = static_cast<ForStmt &>(stmt);
        if (forStmt.init) {
            copyPropagateStatement(*forStmt.init);
        }
        copyPropagateExpr(forStmt.condition);
        copyEnv.clear();
        if (forStmt.update) {
            copyPropagateExpr(forStmt.update);
        }
        copyPropagateStatement(*forStmt.body);
        copyEnv.clear();
        return;
    }
    case Stmt::Kind::DoWhile: {
        auto &doWhileStmt = static_cast<DoWhileStmt &>(stmt);
        copyEnv.clear();
        copyPropagateStatement(*doWhileStmt.body);
        copyPropagateExpr(doWhileStmt.condition);
        copyEnv.clear();
        return;
    }
    case Stmt::Kind::Switch: {
        auto &sw = static_cast<SwitchStmt &>(stmt);
        copyPropagateExpr(sw.scrutinee);
        copyEnv.clear();
        for (auto &c : sw.cases) {
            copyPropagateExpr(c.label);
            copyPropagateStatement(*c.body);
        }
        if (sw.defaultBody) {
            copyPropagateStatement(*sw.defaultBody);
        }
        copyEnv.clear();
        return;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
    case Stmt::Kind::Goto:
        copyEnv.clear();
        return;
    case Stmt::Kind::Label:
        copyPropagateStatement(*static_cast<LabelStmt &>(stmt).body);
        return;
    case Stmt::Kind::StaticAssert:
        return;
    }
}

void Optimizer::copyPropagateExpr(std::unique_ptr<Expr> &expr) {
    if (!expr) return;

    switch (expr->kind) {
    case Expr::Kind::Variable: {
        auto &var = static_cast<VariableExpr &>(*expr);
        auto it = copyEnv.find(var.name);
        if (it != copyEnv.end() && it->second) {
            // 替换为复制源变量，保留 stackOffset/isGlobal/symbolName
            auto *src = it->second;
            auto replacement = std::make_unique<VariableExpr>(src->name);
            replacement->type = expr->type;
            replacement->isLValue = expr->isLValue;
            replacement->stackOffset = src->stackOffset;
            replacement->isGlobal = src->isGlobal;
            replacement->symbolName = src->symbolName;
            replacement->line = expr->line;
            replacement->column = expr->column;
            expr = std::move(replacement);
        }
        return;
    }
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(*expr);
        // 自增自减修改变量，清除相关映射
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement ||
            unary.op == UnaryOp::PostIncrement || unary.op == UnaryOp::PostDecrement) {
            if (unary.operand && unary.operand->kind == Expr::Kind::Variable) {
                auto &name = static_cast<VariableExpr &>(*unary.operand).name;
                for (auto it = copyEnv.begin(); it != copyEnv.end();) {
                    if (it->first == name || (it->second && it->second->name == name)) {
                        it = copyEnv.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
        if (unary.op == UnaryOp::AddressOf || unary.op == UnaryOp::Dereference) {
            return;
        }
        copyPropagateExpr(unary.operand);
        return;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(*expr);
        copyPropagateExpr(binary.left);
        copyPropagateExpr(binary.right);
        return;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<AssignExpr &>(*expr);
        copyPropagateExpr(assign.value);
        if (assign.target->kind == Expr::Kind::Variable) {
            auto &varName = static_cast<VariableExpr &>(*assign.target).name;
            if (!assign.isCompound && assign.value->kind == Expr::Kind::Variable) {
                // x = y：记录复制关系
                copyEnv[varName] = static_cast<VariableExpr *>(assign.value.get());
            } else {
                copyEnv.erase(varName);
                // 清除指向此变量的映射
                for (auto it = copyEnv.begin(); it != copyEnv.end();) {
                    if (it->second && it->second->name == varName) {
                        it = copyEnv.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<CallExpr &>(*expr);
        copyPropagateExpr(call.callee);
        for (auto &arg : call.arguments) {
            copyPropagateExpr(arg);
        }
        // 函数调用可能修改任何变量，保守清除
        copyEnv.clear();
        return;
    }
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<TernaryExpr &>(*expr);
        copyPropagateExpr(ternary.condition);
        copyPropagateExpr(ternary.thenExpr);
        copyPropagateExpr(ternary.elseExpr);
        return;
    }
    case Expr::Kind::Cast:
        copyPropagateExpr(static_cast<CastExpr &>(*expr).operand);
        return;
    case Expr::Kind::Index: {
        auto &index = static_cast<IndexExpr &>(*expr);
        copyPropagateExpr(index.base);
        copyPropagateExpr(index.index);
        return;
    }
    case Expr::Kind::MemberAccess:
        copyPropagateExpr(static_cast<MemberAccessExpr &>(*expr).base);
        return;
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<InitializerListExpr &>(*expr);
        for (auto &element : list.elements) {
            copyPropagateExpr(element);
        }
        return;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<StmtExpr &>(*expr);
        for (auto &s : se.statements) {
            copyPropagateStatement(*s);
        }
        copyPropagateExpr(se.result);
        return;
    }
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
    case Expr::Kind::CompoundLiteral:
    case Expr::Kind::BuiltinVaStart:
    case Expr::Kind::BuiltinVaArg:
    case Expr::Kind::BuiltinVaEnd:
    case Expr::Kind::Generic:
        return;
    }
}

// ===================== 死存储消除 =====================

void Optimizer::applyDeadStoreElimination(Function &function) {
    if (function.body) {
        eliminateDeadStoresInBlock(*function.body);
    }
}

// 循环展开：将常数次 for 循环展开为顺序语句
// 匹配模式：for (var = init; var < bound; var += step) body
// 其中 bound 和 step 为常量，展开阈值为 16
void Optimizer::applyLoopUnrolling(Function &function) {
    if (!function.body) return;
    applyLoopUnrollBlock(*function.body);
}

void Optimizer::applyLoopUnrollBlock(BlockStmt &block) {
    constexpr int UNROLL_LIMIT = 16;

    for (std::size_t i = 0; i < block.statements.size(); ++i) {
        auto &stmt = block.statements[i];
        if (stmt->kind != Stmt::Kind::For) {
            // 递归处理嵌套块
            if (stmt->kind == Stmt::Kind::Block) {
                applyLoopUnrollBlock(static_cast<BlockStmt &>(*stmt));
            } else if (stmt->kind == Stmt::Kind::If) {
                auto &ifStmt = static_cast<IfStmt &>(*stmt);
                if (ifStmt.thenBranch && ifStmt.thenBranch->kind == Stmt::Kind::Block)
                    applyLoopUnrollBlock(static_cast<BlockStmt &>(*ifStmt.thenBranch));
                if (ifStmt.elseBranch && ifStmt.elseBranch->kind == Stmt::Kind::Block)
                    applyLoopUnrollBlock(static_cast<BlockStmt &>(*ifStmt.elseBranch));
            }
            continue;
        }

        auto &forStmt = static_cast<ForStmt &>(*stmt);
        if (!forStmt.body || forStmt.body->kind != Stmt::Kind::Block) continue;
        auto &body = static_cast<BlockStmt &>(*forStmt.body);

        // 检测 for (var = init; var < bound; var += step) 模式
        // 1. init 必须是 DeclStmt 或 ExprStmt(Assign)
        // 2. condition 必须是 var < bound 或 var <= bound
        // 3. update 必须是 var++ 或 var += step 或 var = var + step

        std::string iterVar;
        long long initVal = 0;

        // 解析 init
        if (!forStmt.init) continue;
        if (forStmt.init->kind == Stmt::Kind::Decl) {
            auto &decl = static_cast<DeclStmt &>(*forStmt.init);
            iterVar = decl.name;
            if (!decl.init || decl.init->kind != Expr::Kind::Number) continue;
            initVal = static_cast<NumberExpr &>(*decl.init).value;
        } else if (forStmt.init->kind == Stmt::Kind::Expr) {
            auto &exprStmt = static_cast<ExprStmt &>(*forStmt.init);
            if (exprStmt.expr->kind != Expr::Kind::Assign) continue;
            auto &assign = static_cast<AssignExpr &>(*exprStmt.expr);
            if (assign.isCompound || assign.target->kind != Expr::Kind::Variable) continue;
            iterVar = static_cast<VariableExpr &>(*assign.target).name;
            if (assign.value->kind != Expr::Kind::Number) continue;
            initVal = static_cast<NumberExpr &>(*assign.value).value;
        } else {
            continue;
        }

        // 解析 condition
        if (!forStmt.condition || forStmt.condition->kind != Expr::Kind::Binary) continue;
        auto &cond = static_cast<BinaryExpr &>(*forStmt.condition);
        if (cond.left->kind != Expr::Kind::Variable) continue;
        if (static_cast<VariableExpr &>(*cond.left).name != iterVar) continue;
        if (cond.right->kind != Expr::Kind::Number) continue;
        long long boundVal = static_cast<NumberExpr &>(*cond.right).value;

        bool inclusive = false;  // <= vs <
        if (cond.op == BinaryOp::Less) {
            inclusive = false;
        } else if (cond.op == BinaryOp::LessEqual) {
            inclusive = true;
        } else {
            continue;
        }

        // 解析 update
        if (!forStmt.update) continue;
        long long stepVal = 1;

        if (forStmt.update->kind == Expr::Kind::Unary) {
            // var++ 或 ++var
            auto &unary = static_cast<UnaryExpr &>(*forStmt.update);
            if (unary.op != UnaryOp::PostIncrement && unary.op != UnaryOp::PreIncrement) continue;
            if (unary.operand->kind != Expr::Kind::Variable) continue;
            if (static_cast<VariableExpr &>(*unary.operand).name != iterVar) continue;
            stepVal = 1;
        } else if (forStmt.update->kind == Expr::Kind::Assign) {
            auto &assign = static_cast<AssignExpr &>(*forStmt.update);
            if (assign.target->kind != Expr::Kind::Variable) continue;
            if (static_cast<VariableExpr &>(*assign.target).name != iterVar) continue;

            if (assign.isCompound) {
                // var += step
                if (assign.value->kind != Expr::Kind::Number) continue;
                stepVal = static_cast<NumberExpr &>(*assign.value).value;
            } else if (assign.value->kind == Expr::Kind::Binary) {
                // var = var + step
                auto &rhs = static_cast<BinaryExpr &>(*assign.value);
                if (rhs.op != BinaryOp::Add && rhs.op != BinaryOp::Subtract) continue;
                if (rhs.left->kind != Expr::Kind::Variable) continue;
                if (static_cast<VariableExpr &>(*rhs.left).name != iterVar) continue;
                if (rhs.right->kind != Expr::Kind::Number) continue;
                stepVal = static_cast<NumberExpr &>(*rhs.right).value;
                if (rhs.op == BinaryOp::Subtract) stepVal = -stepVal;
            } else {
                continue;
            }
        } else {
            continue;
        }

        if (stepVal <= 0) continue;

        // 计算迭代次数
        long long tripCount;
        if (inclusive) {
            tripCount = (boundVal - initVal) / stepVal + 1;
        } else {
            tripCount = (boundVal - initVal + stepVal - 1) / stepVal;
        }
        if (tripCount < 2 || tripCount > UNROLL_LIMIT) continue;

        // 检查循环体是否有 break/continue（不展开含控制流跳转的循环）
        bool hasJump = false;
        std::function<void(const Stmt &)> checkJump = [&](const Stmt &s) {
            if (s.kind == Stmt::Kind::Break || s.kind == Stmt::Kind::Continue) {
                hasJump = true;
                return;
            }
            if (s.kind == Stmt::Kind::Block) {
                for (auto &child : static_cast<const BlockStmt &>(s).statements) {
                    checkJump(*child);
                    if (hasJump) return;
                }
            } else if (s.kind == Stmt::Kind::If) {
                auto &ifS = static_cast<const IfStmt &>(s);
                checkJump(*ifS.thenBranch);
                if (hasJump) return;
                if (ifS.elseBranch) checkJump(*ifS.elseBranch);
            }
        };
        checkJump(body);
        if (hasJump) continue;

        // 展开循环
        auto result = std::make_unique<BlockStmt>();

        // 添加初始化语句
        if (forStmt.init) {
            result->statements.push_back(cloneStmt(*forStmt.init));
        }

        int count = static_cast<int>(tripCount);
        for (int iter = 0; iter < count; ++iter) {
            // 克隆循环体
            for (auto &bodyStmt : body.statements) {
                result->statements.push_back(cloneStmt(*bodyStmt));
            }
            // 添加 update（最后一次不添加）
            if (iter < count - 1 && forStmt.update) {
                result->statements.push_back(
                    std::make_unique<ExprStmt>(cloneExpr(*forStmt.update)));
            }
        }

        // 替换原始 for 语句
        block.statements[i] = std::move(result);
        // 继续处理替换后的块（可能包含嵌套 for）
        --i;
    }
}

// 收集表达式中使用的变量名
void Optimizer::collectExprUses(const Expr &expr, std::unordered_set<std::string> &uses) {
    switch (expr.kind) {
    case Expr::Kind::Variable:
        uses.insert(static_cast<const VariableExpr &>(expr).name);
        return;
    case Expr::Kind::Unary:
        collectExprUses(*static_cast<const UnaryExpr &>(expr).operand, uses);
        return;
    case Expr::Kind::Binary: {
        auto &binary = static_cast<const BinaryExpr &>(expr);
        collectExprUses(*binary.left, uses);
        collectExprUses(*binary.right, uses);
        return;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<const AssignExpr &>(expr);
        collectExprUses(*assign.target, uses);
        collectExprUses(*assign.value, uses);
        return;
    }
    case Expr::Kind::Call: {
        auto &call = static_cast<const CallExpr &>(expr);
        if (call.callee) collectExprUses(*call.callee, uses);
        for (auto &arg : call.arguments) {
            collectExprUses(*arg, uses);
        }
        return;
    }
    case Expr::Kind::Index: {
        auto &index = static_cast<const IndexExpr &>(expr);
        collectExprUses(*index.base, uses);
        collectExprUses(*index.index, uses);
        return;
    }
    case Expr::Kind::MemberAccess:
        collectExprUses(*static_cast<const MemberAccessExpr &>(expr).base, uses);
        return;
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<const TernaryExpr &>(expr);
        collectExprUses(*ternary.condition, uses);
        collectExprUses(*ternary.thenExpr, uses);
        collectExprUses(*ternary.elseExpr, uses);
        return;
    }
    case Expr::Kind::Cast:
        collectExprUses(*static_cast<const CastExpr &>(expr).operand, uses);
        return;
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<const InitializerListExpr &>(expr);
        for (auto &elem : list.elements) {
            collectExprUses(*elem, uses);
        }
        return;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<const StmtExpr &>(expr);
        for (auto &stmt : se.statements) {
            if (stmt->kind == Stmt::Kind::Expr) {
                collectExprUses(*static_cast<const ExprStmt &>(*stmt).expr, uses);
            }
        }
        if (se.result) collectExprUses(*se.result, uses);
        return;
    }
    case Expr::Kind::CompoundLiteral:
        if (static_cast<const CompoundLiteralExpr &>(expr).init) {
            collectExprUses(*static_cast<const CompoundLiteralExpr &>(expr).init, uses);
        }
        return;
    case Expr::Kind::BuiltinVaStart:
        if (static_cast<const BuiltinVaStartExpr &>(expr).ap) {
            collectExprUses(*static_cast<const BuiltinVaStartExpr &>(expr).ap, uses);
        }
        return;
    case Expr::Kind::BuiltinVaArg:
        if (static_cast<const BuiltinVaArgExpr &>(expr).ap) {
            collectExprUses(*static_cast<const BuiltinVaArgExpr &>(expr).ap, uses);
        }
        return;
    case Expr::Kind::BuiltinVaEnd:
        if (static_cast<const BuiltinVaEndExpr &>(expr).ap) {
            collectExprUses(*static_cast<const BuiltinVaEndExpr &>(expr).ap, uses);
        }
        return;
    case Expr::Kind::Generic: {
        auto &g = static_cast<const GenericExpr &>(expr);
        if (g.controllingExpr) collectExprUses(*g.controllingExpr, uses);
        for (auto &assoc : g.associations) {
            if (assoc.expr) collectExprUses(*assoc.expr, uses);
        }
        return;
    }
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
        return;
    }
}

// 检查表达式是否有副作用
bool Optimizer::hasSideEffects(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Call:
        return true;  // 函数调用视为有副作用
    case Expr::Kind::Unary: {
        auto &unary = static_cast<const UnaryExpr &>(expr);
        // 自增自减有副作用
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement ||
            unary.op == UnaryOp::PostIncrement || unary.op == UnaryOp::PostDecrement) {
            return true;
        }
        return hasSideEffects(*unary.operand);
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<const BinaryExpr &>(expr);
        return hasSideEffects(*binary.left) || hasSideEffects(*binary.right);
    }
    case Expr::Kind::Assign:
        return true;  // 赋值本身有副作用
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<const TernaryExpr &>(expr);
        return hasSideEffects(*ternary.condition) || hasSideEffects(*ternary.thenExpr) ||
               hasSideEffects(*ternary.elseExpr);
    }
    case Expr::Kind::Cast:
        return hasSideEffects(*static_cast<const CastExpr &>(expr).operand);
    case Expr::Kind::Index: {
        auto &index = static_cast<const IndexExpr &>(expr);
        return hasSideEffects(*index.base) || hasSideEffects(*index.index);
    }
    case Expr::Kind::MemberAccess:
        return hasSideEffects(*static_cast<const MemberAccessExpr &>(expr).base);
    case Expr::Kind::InitializerList: {
        auto &list = static_cast<const InitializerListExpr &>(expr);
        for (auto &elem : list.elements) {
            if (hasSideEffects(*elem)) return true;
        }
        return false;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<const StmtExpr &>(expr);
        for (auto &stmt : se.statements) {
            if (stmt->kind == Stmt::Kind::Expr && hasSideEffects(*static_cast<const ExprStmt &>(*stmt).expr)) {
                return true;
            }
        }
        return se.result && hasSideEffects(*se.result);
    }
    case Expr::Kind::BuiltinVaStart:
    case Expr::Kind::BuiltinVaArg:
    case Expr::Kind::BuiltinVaEnd:
        return true;
    case Expr::Kind::CompoundLiteral:
        return static_cast<const CompoundLiteralExpr &>(expr).init &&
               hasSideEffects(*static_cast<const CompoundLiteralExpr &>(expr).init);
    case Expr::Kind::Generic: {
        auto &g = static_cast<const GenericExpr &>(expr);
        if (g.controllingExpr && hasSideEffects(*g.controllingExpr)) return true;
        for (auto &assoc : g.associations) {
            if (assoc.expr && hasSideEffects(*assoc.expr)) return true;
        }
        return false;
    }
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
    case Expr::Kind::Variable:
        return false;
    }
    return false;
}

// 死存储消除：移除写入后未读取的变量赋值
// 保守策略：只在确认变量在后续所有路径中未被使用时才移除
bool Optimizer::eliminateDeadStoresInBlock(BlockStmt &block) {
    // 向后扫描：如果一个赋值的变量在后续语句中未被使用，且无副作用，则移除
    bool changed = false;
    for (int i = static_cast<int>(block.statements.size()) - 1; i >= 0; --i) {
        auto &stmt = block.statements[static_cast<size_t>(i)];

        // 只处理简单的表达式语句：x = expr;
        if (stmt->kind != Stmt::Kind::Expr) continue;
        auto &exprStmt = static_cast<ExprStmt &>(*stmt);
        if (exprStmt.expr->kind != Expr::Kind::Assign) continue;
        auto &assign = static_cast<AssignExpr &>(*exprStmt.expr);

        // 只处理简单变量赋值（非复合赋值）
        if (assign.isCompound) continue;
        if (assign.target->kind != Expr::Kind::Variable) continue;
        auto &targetVar = static_cast<VariableExpr &>(*assign.target);
        // 跳过全局变量——其存储对其他函数可见
        if (targetVar.isGlobal) continue;
        auto &varName = targetVar.name;

        // 如果 RHS 有副作用，保留
        if (hasSideEffects(*assign.value)) continue;

        // 检查变量在后续语句中是否被使用（包括嵌套块）
        bool usedLater = false;
        for (size_t j = static_cast<size_t>(i) + 1; j < block.statements.size(); ++j) {
            std::unordered_set<std::string> laterUses;
            // 递归收集后续语句及其嵌套块中的所有变量使用
            std::function<void(const Stmt &)> collectStmtUses = [&](const Stmt &s) {
                switch (s.kind) {
                case Stmt::Kind::Expr:
                    collectExprUses(*static_cast<const ExprStmt &>(s).expr, laterUses);
                    break;
                case Stmt::Kind::Return:
                    if (static_cast<const ReturnStmt &>(s).expr)
                        collectExprUses(*static_cast<const ReturnStmt &>(s).expr, laterUses);
                    break;
                case Stmt::Kind::Decl:
                    if (static_cast<const DeclStmt &>(s).init)
                        collectExprUses(*static_cast<const DeclStmt &>(s).init, laterUses);
                    break;
                case Stmt::Kind::Block:
                    for (auto &nested : static_cast<const BlockStmt &>(s).statements) {
                        collectStmtUses(*nested);
                    }
                    break;
                case Stmt::Kind::If: {
                    auto &ifStmt = static_cast<const IfStmt &>(s);
                    collectExprUses(*ifStmt.condition, laterUses);
                    collectStmtUses(*ifStmt.thenBranch);
                    if (ifStmt.elseBranch) collectStmtUses(*ifStmt.elseBranch);
                    break;
                }
                case Stmt::Kind::While: {
                    auto &whileStmt = static_cast<const WhileStmt &>(s);
                    collectExprUses(*whileStmt.condition, laterUses);
                    collectStmtUses(*whileStmt.body);
                    break;
                }
                case Stmt::Kind::For: {
                    auto &forStmt = static_cast<const ForStmt &>(s);
                    if (forStmt.init) collectStmtUses(*forStmt.init);
                    if (forStmt.condition) collectExprUses(*forStmt.condition, laterUses);
                    if (forStmt.update) collectExprUses(*forStmt.update, laterUses);
                    collectStmtUses(*forStmt.body);
                    break;
                }
                case Stmt::Kind::DoWhile: {
                    auto &doWhileStmt = static_cast<const DoWhileStmt &>(s);
                    collectStmtUses(*doWhileStmt.body);
                    collectExprUses(*doWhileStmt.condition, laterUses);
                    break;
                }
                case Stmt::Kind::Switch: {
                    auto &sw = static_cast<const SwitchStmt &>(s);
                    collectExprUses(*sw.scrutinee, laterUses);
                    for (auto &c : sw.cases) {
                        collectExprUses(*c.label, laterUses);
                        collectStmtUses(*c.body);
                    }
                    if (sw.defaultBody) collectStmtUses(*sw.defaultBody);
                    break;
                }
                default:
                    break;
                }
            };
            collectStmtUses(*block.statements[j]);
            if (laterUses.count(varName)) {
                usedLater = true;
                break;
            }
            // 如果后续语句重新赋值同一变量，停止搜索
            if (block.statements[j]->kind == Stmt::Kind::Expr) {
                auto &laterExpr = static_cast<const ExprStmt &>(*block.statements[j]).expr;
                if (laterExpr->kind == Expr::Kind::Assign) {
                    auto &laterAssign = static_cast<const AssignExpr &>(*laterExpr);
                    if (laterAssign.target->kind == Expr::Kind::Variable &&
                        static_cast<const VariableExpr &>(*laterAssign.target).name == varName) {
                        break;
                    }
                }
            }
        }

        if (!usedLater) {
            block.statements.erase(block.statements.begin() + i);
            changed = true;
        }
    }

    return changed;
}

// ===================== 死代码消除 =====================

// 消除不可达代码和无副作用的无用表达式，返回是否有修改
bool Optimizer::eliminateDeadCode(BlockStmt &block) {
    bool changed = false;

    // 递归处理嵌套块和语句
    for (auto &stmt : block.statements) {
        switch (stmt->kind) {
        case Stmt::Kind::Block:
            changed |= eliminateDeadCode(static_cast<BlockStmt &>(*stmt));
            break;
        case Stmt::Kind::If: {
            auto &ifStmt = static_cast<IfStmt &>(*stmt);
            if (ifStmt.thenBranch && ifStmt.thenBranch->kind == Stmt::Kind::Block) {
                changed |= eliminateDeadCode(static_cast<BlockStmt &>(*ifStmt.thenBranch));
            }
            if (ifStmt.elseBranch && ifStmt.elseBranch->kind == Stmt::Kind::Block) {
                changed |= eliminateDeadCode(static_cast<BlockStmt &>(*ifStmt.elseBranch));
            }
            break;
        }
        case Stmt::Kind::While:
            if (static_cast<WhileStmt &>(*stmt).body->kind == Stmt::Kind::Block) {
                changed |= eliminateDeadCode(static_cast<BlockStmt &>(*static_cast<WhileStmt &>(*stmt).body));
            }
            break;
        case Stmt::Kind::For:
            if (static_cast<ForStmt &>(*stmt).body->kind == Stmt::Kind::Block) {
                changed |= eliminateDeadCode(static_cast<BlockStmt &>(*static_cast<ForStmt &>(*stmt).body));
            }
            break;
        case Stmt::Kind::DoWhile:
            if (static_cast<DoWhileStmt &>(*stmt).body->kind == Stmt::Kind::Block) {
                changed |= eliminateDeadCode(static_cast<BlockStmt &>(*static_cast<DoWhileStmt &>(*stmt).body));
            }
            break;
        default:
            break;
        }
    }

    // 规则 a: return 之后的所有语句都是不可达的
    for (std::size_t i = 0; i < block.statements.size(); ++i) {
        if (block.statements[i]->kind == Stmt::Kind::Return) {
            if (i + 1 < block.statements.size()) {
                block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                       block.statements.end());
                changed = true;
            }
            break;
        }
    }

    // 规则 b/c/d: 常量条件的 if/while/do-while
    for (std::size_t i = 0; i < block.statements.size(); ++i) {
        auto &stmt = block.statements[i];

        if (stmt->kind == Stmt::Kind::If) {
            auto &ifStmt = static_cast<IfStmt &>(*stmt);
            if (ifStmt.condition->kind == Expr::Kind::Number) {
                long long condValue = static_cast<NumberExpr &>(*ifStmt.condition).value;
                if (condValue != 0) {
                    // 规则 b: 条件恒真，用 then 分支替换
                    if (ifStmt.thenBranch->kind == Stmt::Kind::Block) {
                        // 展开块的语句到当前层
                        auto &thenBlock = static_cast<BlockStmt &>(*ifStmt.thenBranch);
                        auto extracted = std::move(thenBlock.statements);
                        block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i));
                        block.statements.insert(block.statements.begin() + static_cast<std::ptrdiff_t>(i),
                                                std::make_move_iterator(extracted.begin()),
                                                std::make_move_iterator(extracted.end()));
                        changed = true;
                        --i; // 重新检查插入的语句
                    } else {
                        stmt = std::move(ifStmt.thenBranch);
                        changed = true;
                        --i;
                    }
                } else {
                    // 规则 c: 条件恒假
                    if (ifStmt.elseBranch) {
                        if (ifStmt.elseBranch->kind == Stmt::Kind::Block) {
                            auto &elseBlock = static_cast<BlockStmt &>(*ifStmt.elseBranch);
                            auto extracted = std::move(elseBlock.statements);
                            block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i));
                            block.statements.insert(block.statements.begin() + static_cast<std::ptrdiff_t>(i),
                                                    std::make_move_iterator(extracted.begin()),
                                                    std::make_move_iterator(extracted.end()));
                            changed = true;
                            --i;
                        } else {
                            stmt = std::move(ifStmt.elseBranch);
                            changed = true;
                            --i;
                        }
                    } else {
                        // 无 else，删除整个 if
                        block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i));
                        changed = true;
                        --i;
                    }
                }
            }
        } else if (stmt->kind == Stmt::Kind::While) {
            auto &whileStmt = static_cast<WhileStmt &>(*stmt);
            if (whileStmt.condition->kind == Expr::Kind::Number) {
                long long condValue = static_cast<NumberExpr &>(*whileStmt.condition).value;
                if (condValue == 0) {
                    // 规则 d: while(0) 永远不执行
                    block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    --i;
                }
            }
        } else if (stmt->kind == Stmt::Kind::DoWhile) {
            auto &doWhileStmt = static_cast<DoWhileStmt &>(*stmt);
            if (doWhileStmt.condition->kind == Expr::Kind::Number) {
                long long condValue = static_cast<NumberExpr &>(*doWhileStmt.condition).value;
                if (condValue == 0) {
                    // 规则 e: do-while(0) 只执行一次，用 body 替换
                    if (doWhileStmt.body->kind == Stmt::Kind::Block) {
                        auto &bodyBlock = static_cast<BlockStmt &>(*doWhileStmt.body);
                        auto extracted = std::move(bodyBlock.statements);
                        block.statements.erase(block.statements.begin() + static_cast<std::ptrdiff_t>(i));
                        block.statements.insert(block.statements.begin() + static_cast<std::ptrdiff_t>(i),
                                                std::make_move_iterator(extracted.begin()),
                                                std::make_move_iterator(extracted.end()));
                        changed = true;
                        --i;
                    } else {
                        stmt = std::move(doWhileStmt.body);
                        changed = true;
                        --i;
                    }
                }
            }
        }
    }

    return changed;
}

// ===================== 函数内联 =====================

// 收集可内联函数的元信息（仅存指针，不移动 AST）
void Optimizer::collectInlineCandidates(Program &program) {
    for (auto &function : program.functions) {
        if (!function.isDeclaration() && isInlinableFunction(function)) {
            inlineCandidates[function.name] = InlineCandidate{&function};
        }
    }
}

// 检查函数是否可内联
// 普通函数：单 return 语句，参数不超过 6 个，语句不超过 5 个，无控制流
// inline 标记函数：放宽限制（允许更多参数/语句和简单控制流）
bool Optimizer::isInlinableFunction(const Function &func) {
    if (!func.body || func.body->kind != Stmt::Kind::Block) return false;
    const auto &body = static_cast<const BlockStmt &>(*func.body);
    if (body.statements.empty()) return false;

    bool relaxed = func.isInline;
    std::size_t maxParams = relaxed ? 8 : 6;
    std::size_t maxStmts = relaxed ? 12 : 5;

    if (func.parameters.size() > maxParams) return false;
    if (body.statements.size() > maxStmts) return false;

    for (std::size_t i = 0; i < body.statements.size(); ++i) {
        switch (body.statements[i]->kind) {
        case Stmt::Kind::Decl:
        case Stmt::Kind::Expr:
            break;
        case Stmt::Kind::Return:
            if (i != body.statements.size() - 1) return false;
            if (!static_cast<const ReturnStmt &>(*body.statements[i]).expr) return false;
            break;
        case Stmt::Kind::If:
        case Stmt::Kind::While:
        case Stmt::Kind::For:
        case Stmt::Kind::DoWhile:
            if (!relaxed) return false;  // 普通函数不允许控制流
            break;
        default:
            return false;  // switch/break/continue/goto 等不内联
        }
    }
    // 最后一条语句必须是 Return（非 void 函数）或整个函数无返回值
    return body.statements.back()->kind == Stmt::Kind::Return;
}

// 深拷贝表达式树
std::unique_ptr<Expr> Optimizer::cloneExpr(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number: {
        auto &n = static_cast<const NumberExpr &>(expr);
        auto c = std::make_unique<NumberExpr>(n.value);
        c->type = n.type;
        c->isLValue = n.isLValue;
        return c;
    }
    case Expr::Kind::FloatLiteral: {
        auto &f = static_cast<const FloatLiteralExpr &>(expr);
        auto c = std::make_unique<FloatLiteralExpr>(f.value);
        c->type = f.type;
        c->isLValue = f.isLValue;
        return c;
    }
    case Expr::Kind::String: {
        auto &s = static_cast<const StringExpr &>(expr);
        auto c = std::make_unique<StringExpr>(s.value);
        c->type = s.type;
        c->isLValue = s.isLValue;
        return c;
    }
    case Expr::Kind::Variable: {
        auto &v = static_cast<const VariableExpr &>(expr);
        auto c = std::make_unique<VariableExpr>(v.name);
        c->type = v.type;
        c->isLValue = v.isLValue;
        c->stackOffset = v.stackOffset;
        c->isGlobal = v.isGlobal;
        c->symbolName = v.symbolName;
        return c;
    }
    case Expr::Kind::Unary: {
        auto &u = static_cast<const UnaryExpr &>(expr);
        auto c = std::make_unique<UnaryExpr>(u.op, u.operand ? cloneExpr(*u.operand) : nullptr);
        c->type = u.type;
        c->isLValue = u.isLValue;
        return c;
    }
    case Expr::Kind::Binary: {
        auto &b = static_cast<const BinaryExpr &>(expr);
        auto c = std::make_unique<BinaryExpr>(b.op, cloneExpr(*b.left), cloneExpr(*b.right));
        c->type = b.type;
        c->isLValue = b.isLValue;
        return c;
    }
    case Expr::Kind::Ternary: {
        auto &t = static_cast<const TernaryExpr &>(expr);
        auto c = std::make_unique<TernaryExpr>(cloneExpr(*t.condition), cloneExpr(*t.thenExpr), cloneExpr(*t.elseExpr));
        c->type = t.type;
        c->isLValue = t.isLValue;
        return c;
    }
    case Expr::Kind::Cast: {
        auto &ca = static_cast<const CastExpr &>(expr);
        auto c = std::make_unique<CastExpr>(ca.targetType, cloneExpr(*ca.operand));
        c->type = ca.type;
        c->isLValue = ca.isLValue;
        return c;
    }
    case Expr::Kind::Call: {
        auto &ca = static_cast<const CallExpr &>(expr);
        std::vector<std::unique_ptr<Expr>> args;
        for (const auto &a : ca.arguments) {
            args.push_back(cloneExpr(*a));
        }
        auto c = std::make_unique<CallExpr>(cloneExpr(*ca.callee), std::move(args));
        c->type = ca.type;
        c->isLValue = ca.isLValue;
        return c;
    }
    case Expr::Kind::Index: {
        auto &idx = static_cast<const IndexExpr &>(expr);
        auto c = std::make_unique<IndexExpr>(cloneExpr(*idx.base), cloneExpr(*idx.index));
        c->type = idx.type;
        c->isLValue = idx.isLValue;
        return c;
    }
    case Expr::Kind::MemberAccess: {
        auto &m = static_cast<const MemberAccessExpr &>(expr);
        auto c = std::make_unique<MemberAccessExpr>(cloneExpr(*m.base), m.memberName);
        c->type = m.type;
        c->isLValue = m.isLValue;
        return c;
    }
    case Expr::Kind::Assign: {
        auto &a = static_cast<const AssignExpr &>(expr);
        auto c = std::make_unique<AssignExpr>(cloneExpr(*a.target), cloneExpr(*a.value));
        c->isCompound = a.isCompound;
        c->type = a.type;
        c->isLValue = a.isLValue;
        return c;
    }
    case Expr::Kind::InitializerList: {
        auto &il = static_cast<const InitializerListExpr &>(expr);
        std::vector<std::unique_ptr<Expr>> elems;
        for (const auto &e : il.elements) {
            elems.push_back(cloneExpr(*e));
        }
        auto c = std::make_unique<InitializerListExpr>(std::move(elems));
        c->type = il.type;
        c->isLValue = il.isLValue;
        return c;
    }
    case Expr::Kind::CompoundLiteral: {
        auto &cl = static_cast<const CompoundLiteralExpr &>(expr);
        auto initClone = std::make_unique<InitializerListExpr>(std::vector<std::unique_ptr<Expr>>{});
        if (cl.init) {
            for (const auto &e : cl.init->elements) {
                initClone->elements.push_back(cloneExpr(*e));
            }
            initClone->type = cl.init->type;
            initClone->isLValue = cl.init->isLValue;
        }
        auto c = std::make_unique<CompoundLiteralExpr>(cl.compoundType, std::move(initClone));
        c->type = cl.type;
        c->isLValue = cl.isLValue;
        return c;
    }
    case Expr::Kind::StmtExpr: {
        auto &se = static_cast<const StmtExpr &>(expr);
        std::vector<std::unique_ptr<Stmt>> clonedStmts;
        for (const auto &s : se.statements) {
            clonedStmts.push_back(cloneStmt(*s));
        }
        auto clonedResult = se.result ? cloneExpr(*se.result) : nullptr;
        auto c = std::make_unique<StmtExpr>(std::move(clonedStmts), std::move(clonedResult));
        c->type = se.type;
        c->isLValue = se.isLValue;
        return c;
    }
    case Expr::Kind::BuiltinVaStart:
    case Expr::Kind::BuiltinVaArg:
    case Expr::Kind::BuiltinVaEnd:
    case Expr::Kind::Generic:
        // 不可克隆的表达式类型，返回 nullptr
        return nullptr;
    }
    return nullptr;
}

// 深拷贝语句树
std::unique_ptr<Stmt> Optimizer::cloneStmt(const Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Decl: {
        auto &d = static_cast<const DeclStmt &>(stmt);
        auto clonedInit = d.init ? cloneExpr(*d.init) : nullptr;
        auto c = std::make_unique<DeclStmt>(d.type, d.name, std::move(clonedInit));
        c->line = d.line;
        c->column = d.column;
        c->stackOffset = d.stackOffset;
        c->isStatic = d.isStatic;
        c->staticSymbolName = d.staticSymbolName;
        c->vlaSizeExpr = d.vlaSizeExpr ? cloneExpr(*d.vlaSizeExpr) : nullptr;
        return c;
    }
    case Stmt::Kind::Expr: {
        auto &e = static_cast<const ExprStmt &>(stmt);
        auto clonedExpr = e.expr ? cloneExpr(*e.expr) : nullptr;
        return std::make_unique<ExprStmt>(std::move(clonedExpr));
    }
    case Stmt::Kind::Return: {
        auto &r = static_cast<const ReturnStmt &>(stmt);
        auto clonedExpr = r.expr ? cloneExpr(*r.expr) : nullptr;
        return std::make_unique<ReturnStmt>(std::move(clonedExpr));
    }
    case Stmt::Kind::Block: {
        auto &b = static_cast<const BlockStmt &>(stmt);
        auto c = std::make_unique<BlockStmt>();
        for (const auto &s : b.statements) {
            c->statements.push_back(cloneStmt(*s));
        }
        return c;
    }
    case Stmt::Kind::If: {
        auto &i = static_cast<const IfStmt &>(stmt);
        auto clonedCond = i.condition ? cloneExpr(*i.condition) : nullptr;
        auto clonedThen = i.thenBranch ? cloneStmt(*i.thenBranch) : nullptr;
        auto clonedElse = i.elseBranch ? cloneStmt(*i.elseBranch) : nullptr;
        return std::make_unique<IfStmt>(std::move(clonedCond), std::move(clonedThen), std::move(clonedElse));
    }
    case Stmt::Kind::While: {
        auto &w = static_cast<const WhileStmt &>(stmt);
        auto clonedCond = w.condition ? cloneExpr(*w.condition) : nullptr;
        auto clonedBody = w.body ? cloneStmt(*w.body) : nullptr;
        return std::make_unique<WhileStmt>(std::move(clonedCond), std::move(clonedBody));
    }
    case Stmt::Kind::For: {
        auto &f = static_cast<const ForStmt &>(stmt);
        auto clonedInit = f.init ? cloneStmt(*f.init) : nullptr;
        auto clonedCond = f.condition ? cloneExpr(*f.condition) : nullptr;
        auto clonedUpdate = f.update ? cloneExpr(*f.update) : nullptr;
        auto clonedBody = f.body ? cloneStmt(*f.body) : nullptr;
        return std::make_unique<ForStmt>(std::move(clonedInit), std::move(clonedCond), std::move(clonedUpdate), std::move(clonedBody));
    }
    case Stmt::Kind::DoWhile: {
        auto &d = static_cast<const DoWhileStmt &>(stmt);
        auto clonedBody = d.body ? cloneStmt(*d.body) : nullptr;
        auto clonedCond = d.condition ? cloneExpr(*d.condition) : nullptr;
        return std::make_unique<DoWhileStmt>(std::move(clonedBody), std::move(clonedCond));
    }
    case Stmt::Kind::Break:
        return std::make_unique<BreakStmt>();
    case Stmt::Kind::Continue:
        return std::make_unique<ContinueStmt>();
    case Stmt::Kind::Goto: {
        auto &g = static_cast<const GotoStmt &>(stmt);
        return std::make_unique<GotoStmt>(g.targetName);
    }
    case Stmt::Kind::Label: {
        auto &l = static_cast<const LabelStmt &>(stmt);
        auto clonedInner = l.body ? cloneStmt(*l.body) : nullptr;
        return std::make_unique<LabelStmt>(l.name, std::move(clonedInner));
    }
    default:
        return nullptr;
    }
}
void Optimizer::tryInlineCall(std::unique_ptr<Expr> &expr) {
    if (expr->kind != Expr::Kind::Call) return;
    auto &call = static_cast<CallExpr &>(*expr);
    if (call.callee->kind != Expr::Kind::Variable) return;
    const auto &funcName = static_cast<const VariableExpr &>(*call.callee).name;

    auto it = inlineCandidates.find(funcName);
    if (it == inlineCandidates.end()) return;

    const auto &candidate = it->second;
    const auto &func = *candidate.func;
    if (call.arguments.size() != func.parameters.size()) return;

    // 只内联标量参数的函数（不内联结构体/联合体按值传递）
    for (const auto &param : func.parameters) {
        if (param.type->isStruct() || param.type->isUnion()) return;
    }

    auto &body = static_cast<const BlockStmt &>(*func.body);

    // 创建参数替换映射：参数名 → 参数表达式（深拷贝）
    std::unordered_map<std::string, std::unique_ptr<Expr>> paramMap;
    for (std::size_t i = 0; i < func.parameters.size(); ++i) {
        paramMap[func.parameters[i].name] = cloneExpr(*call.arguments[i]);
    }

    // 替换表达式中的参数变量
    auto substituteExpr = [&](auto &self, std::unique_ptr<Expr> &e) -> void {
        if (!e) return;
        if (e->kind == Expr::Kind::Variable) {
            const auto &varName = static_cast<const VariableExpr &>(*e).name;
            auto pIt = paramMap.find(varName);
            if (pIt != paramMap.end()) {
                e = cloneExpr(*pIt->second);
                return;
            }
        }
        if (e->kind == Expr::Kind::Binary) {
            auto &binary = static_cast<BinaryExpr &>(*e);
            self(self, binary.left);
            self(self, binary.right);
        } else if (e->kind == Expr::Kind::Unary) {
            auto &unary = static_cast<UnaryExpr &>(*e);
            if (unary.operand) self(self, unary.operand);
        } else if (e->kind == Expr::Kind::Ternary) {
            auto &ternary = static_cast<TernaryExpr &>(*e);
            self(self, ternary.condition);
            self(self, ternary.thenExpr);
            self(self, ternary.elseExpr);
        } else if (e->kind == Expr::Kind::Cast) {
            self(self, static_cast<CastExpr &>(*e).operand);
        } else if (e->kind == Expr::Kind::Assign) {
            auto &assign = static_cast<AssignExpr &>(*e);
            self(self, assign.target);
            self(self, assign.value);
        } else if (e->kind == Expr::Kind::Call) {
            auto &c = static_cast<CallExpr &>(*e);
            self(self, c.callee);
            for (auto &arg : c.arguments) self(self, arg);
        } else if (e->kind == Expr::Kind::Index) {
            auto &idx = static_cast<IndexExpr &>(*e);
            self(self, idx.base);
            self(self, idx.index);
        }
    };

    // 单语句且为 return：直接替换为表达式
    if (body.statements.size() == 1 && body.statements[0]->kind == Stmt::Kind::Return) {
        auto &ret = static_cast<const ReturnStmt &>(*body.statements[0]);
        if (ret.expr) {
            auto inlined = cloneExpr(*ret.expr);
            substituteExpr(substituteExpr, inlined);
            if (inlined) {
                inlined->type = expr->type;
                expr = std::move(inlined);
            }
        }
        return;
    }
    // 多语句函数：创建 StmtExpr（语句表达式）
    // 仅内联不含局部变量声明的函数（DeclStmt 需要栈分配，暂不支持）
    // 且不允许对参数赋值（内联后参数被替换为右值，赋值目标无效）
    std::unordered_set<std::string> paramNames;
    for (const auto &p : func.parameters) paramNames.insert(p.name);
    for (const auto &s : body.statements) {
        if (s->kind == Stmt::Kind::Decl) return;
        if (s->kind == Stmt::Kind::Expr) {
            auto &es = static_cast<const ExprStmt &>(*s);
            if (es.expr && es.expr->kind == Expr::Kind::Assign) {
                auto &assign = static_cast<const AssignExpr &>(*es.expr);
                if (assign.target->kind == Expr::Kind::Variable) {
                    auto &name = static_cast<const VariableExpr &>(*assign.target).name;
                    if (paramNames.count(name)) return;  // 参数被赋值，无法安全内联
                }
            }
        }
    }
    std::vector<std::unique_ptr<Stmt>> inlinedStmts;
    std::unique_ptr<Expr> resultExpr;
    for (std::size_t i = 0; i < body.statements.size(); ++i) {
        if (body.statements[i]->kind == Stmt::Kind::Return) {
            auto &ret = static_cast<const ReturnStmt &>(*body.statements[i]);
            if (ret.expr) {
                resultExpr = cloneExpr(*ret.expr);
                substituteExpr(substituteExpr, resultExpr);
            }
        } else if (body.statements[i]->kind == Stmt::Kind::Decl) {
            auto &decl = static_cast<const DeclStmt &>(*body.statements[i]);
            auto clonedInit = decl.init ? cloneExpr(*decl.init) : nullptr;
            substituteExpr(substituteExpr, clonedInit);
            inlinedStmts.push_back(std::make_unique<DeclStmt>(decl.type, decl.name, std::move(clonedInit)));
            // 传播行号信息
            static_cast<DeclStmt &>(*inlinedStmts.back()).line = decl.line;
            static_cast<DeclStmt &>(*inlinedStmts.back()).column = decl.column;
        } else if (body.statements[i]->kind == Stmt::Kind::Expr) {
            auto &es = static_cast<const ExprStmt &>(*body.statements[i]);
            if (es.expr) {
                auto clonedExpr = cloneExpr(*es.expr);
                substituteExpr(substituteExpr, clonedExpr);
                inlinedStmts.push_back(std::make_unique<ExprStmt>(std::move(clonedExpr)));
            }
        }
    }
    if (resultExpr) {
        resultExpr->type = expr->type;
        auto stmtExpr = std::make_unique<StmtExpr>(std::move(inlinedStmts), std::move(resultExpr));
        stmtExpr->type = expr->type;
        expr = std::move(stmtExpr);
    }
}

// ===================== 公共子表达式消除（CSE） =====================

// 计算表达式的唯一键（用于比较是否为相同表达式）
std::string Optimizer::computeExprKey(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
        return "N" + std::to_string(static_cast<const NumberExpr &>(expr).value);
    case Expr::Kind::Variable:
        return "V" + static_cast<const VariableExpr &>(expr).name;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (!unary.operand) return "";
        return "U" + std::to_string(static_cast<int>(unary.op)) + "(" + computeExprKey(*unary.operand) + ")";
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        return "B" + std::to_string(static_cast<int>(binary.op)) + "(" +
               computeExprKey(*binary.left) + "," + computeExprKey(*binary.right) + ")";
    }
    case Expr::Kind::Ternary: {
        const auto &ternary = static_cast<const TernaryExpr &>(expr);
        return "T(" + computeExprKey(*ternary.condition) + "?" +
               computeExprKey(*ternary.thenExpr) + ":" + computeExprKey(*ternary.elseExpr) + ")";
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        return "C(" + computeExprKey(*cast.operand) + ")";
    }
    case Expr::Kind::Index: {
        const auto &index = static_cast<const IndexExpr &>(expr);
        return "I(" + computeExprKey(*index.base) + "[" + computeExprKey(*index.index) + "])";
    }
    case Expr::Kind::FloatLiteral:
        return "F" + std::to_string(static_cast<const FloatLiteralExpr &>(expr).value);
    case Expr::Kind::String:
        return "S" + static_cast<const StringExpr &>(expr).value;
    default:
        return "";
    }
}

// 检查表达式是否是纯的（无副作用）
bool Optimizer::isPureExpression(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
    case Expr::Kind::FloatLiteral:
    case Expr::Kind::String:
    case Expr::Kind::Variable:
        return true;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement ||
            unary.op == UnaryOp::PostIncrement || unary.op == UnaryOp::PostDecrement) {
            return false;
        }
        if (!unary.operand) return false;
        return isPureExpression(*unary.operand);
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        // 短路运算符有隐式控制流，CSE 不能安全处理
        if (binary.op == BinaryOp::LogicalAnd || binary.op == BinaryOp::LogicalOr) {
            return false;
        }
        return isPureExpression(*binary.left) && isPureExpression(*binary.right);
    }
    case Expr::Kind::Ternary: {
        const auto &ternary = static_cast<const TernaryExpr &>(expr);
        return isPureExpression(*ternary.condition) &&
               isPureExpression(*ternary.thenExpr) &&
               isPureExpression(*ternary.elseExpr);
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        return isPureExpression(*cast.operand);
    }
    // Index/MemberAccess 涉及内存读取，可能受指针别名影响，视为非纯
    case Expr::Kind::Index:
    case Expr::Kind::MemberAccess:
    default:
        return false;
    }
}

// 收集表达式中引用的所有变量名
void Optimizer::collectExprVars(const Expr &expr, std::unordered_set<std::string> &vars) {
    switch (expr.kind) {
    case Expr::Kind::Variable:
        vars.insert(static_cast<const VariableExpr &>(expr).name);
        break;
    case Expr::Kind::Unary:
        if (static_cast<const UnaryExpr &>(expr).operand)
            collectExprVars(*static_cast<const UnaryExpr &>(expr).operand, vars);
        break;
    case Expr::Kind::Binary:
        collectExprVars(*static_cast<const BinaryExpr &>(expr).left, vars);
        collectExprVars(*static_cast<const BinaryExpr &>(expr).right, vars);
        break;
    case Expr::Kind::Ternary:
        collectExprVars(*static_cast<const TernaryExpr &>(expr).condition, vars);
        collectExprVars(*static_cast<const TernaryExpr &>(expr).thenExpr, vars);
        collectExprVars(*static_cast<const TernaryExpr &>(expr).elseExpr, vars);
        break;
    case Expr::Kind::Cast:
        collectExprVars(*static_cast<const CastExpr &>(expr).operand, vars);
        break;
    case Expr::Kind::Index:
        collectExprVars(*static_cast<const IndexExpr &>(expr).base, vars);
        collectExprVars(*static_cast<const IndexExpr &>(expr).index, vars);
        break;
    default:
        break;
    }
}

// 使依赖指定变量的所有 CSE 条目失效
void Optimizer::invalidateCSE(std::unordered_map<std::string, CSEEntry> &availableExprs,
                              const std::string &varName) {
    for (auto it = availableExprs.begin(); it != availableExprs.end(); ) {
        if (it->second.usedVars.count(varName)) {
            it = availableExprs.erase(it);
        } else {
            ++it;
        }
    }
}

// 对单个表达式应用 CSE
void Optimizer::cseExpr(std::unique_ptr<Expr> &expr,
                         std::unordered_map<std::string, CSEEntry> &availableExprs,
                         std::vector<std::unique_ptr<Stmt>> &newStmts,
                         int &tempCounter,
                         int &stackSize) {
    if (!expr) return;

    // 先递归处理子表达式
    switch (expr->kind) {
    case Expr::Kind::Unary: {
        auto &unary = static_cast<UnaryExpr &>(*expr);
        cseExpr(unary.operand, availableExprs, newStmts, tempCounter, stackSize);
        break;
    }
    case Expr::Kind::Binary: {
        auto &binary = static_cast<BinaryExpr &>(*expr);
        // 短路运算符有隐式控制流，CSE 不能安全处理其子表达式
        if (binary.op == BinaryOp::LogicalAnd || binary.op == BinaryOp::LogicalOr) {
            return;
        }
        cseExpr(binary.left, availableExprs, newStmts, tempCounter, stackSize);
        cseExpr(binary.right, availableExprs, newStmts, tempCounter, stackSize);
        break;
    }
    case Expr::Kind::Ternary: {
        auto &ternary = static_cast<TernaryExpr &>(*expr);
        cseExpr(ternary.condition, availableExprs, newStmts, tempCounter, stackSize);
        cseExpr(ternary.thenExpr, availableExprs, newStmts, tempCounter, stackSize);
        cseExpr(ternary.elseExpr, availableExprs, newStmts, tempCounter, stackSize);
        break;
    }
    case Expr::Kind::Cast: {
        auto &cast = static_cast<CastExpr &>(*expr);
        cseExpr(cast.operand, availableExprs, newStmts, tempCounter, stackSize);
        break;
    }
    case Expr::Kind::Index: {
        auto &index = static_cast<IndexExpr &>(*expr);
        cseExpr(index.base, availableExprs, newStmts, tempCounter, stackSize);
        cseExpr(index.index, availableExprs, newStmts, tempCounter, stackSize);
        break;
    }
    case Expr::Kind::Assign: {
        auto &assign = static_cast<AssignExpr &>(*expr);
        cseExpr(assign.value, availableExprs, newStmts, tempCounter, stackSize);
        if (assign.target->kind == Expr::Kind::Variable) {
            invalidateCSE(availableExprs, static_cast<const VariableExpr &>(*assign.target).name);
        } else {
            availableExprs.clear();
        }
        return;
    }
    case Expr::Kind::Call:
        // 函数调用可能修改任意内存
        availableExprs.clear();
        return;
    default:
        return;
    }

    if (!isPureExpression(*expr)) return;

    std::string key = computeExprKey(*expr);
    if (key.empty()) return;

    auto it = availableExprs.find(key);
    if (it != availableExprs.end()) {
        auto var = std::make_unique<VariableExpr>(it->second.tempName);
        var->type = expr->type;
        var->isLValue = false;
        var->stackOffset = it->second.stackOffset;
        expr = std::move(var);
        return;
    }

    if (expr->kind == Expr::Kind::Binary || expr->kind == Expr::Kind::Unary ||
        expr->kind == Expr::Kind::Index) {
        std::string tempName = "__cse_" + std::to_string(tempCounter++);

        int align = expr->type ? expr->type->alignment() : 8;
        int size = expr->type ? expr->type->storageSize() : 8;
        if (stackSize % align != 0) {
            stackSize += align - (stackSize % align);
        }
        stackSize += size;
        int stackOffset = stackSize;

        std::unordered_set<std::string> usedVars;
        collectExprVars(*expr, usedVars);
        availableExprs[key] = CSEEntry{tempName, stackOffset, usedVars};

        auto clonedExpr = cloneExpr(*expr);
        auto decl = std::make_unique<DeclStmt>(expr->type, tempName, std::move(clonedExpr));
        decl->line = expr->line;
        decl->stackOffset = stackOffset;
        newStmts.push_back(std::move(decl));

        auto var = std::make_unique<VariableExpr>(tempName);
        var->type = expr->type;
        var->isLValue = false;
        var->stackOffset = stackOffset;
        expr = std::move(var);
    }
}

// 对整个函数应用 CSE
void Optimizer::applyCSE(Function &function) {
    if (!function.body || function.body->kind != Stmt::Kind::Block) return;
    auto &block = static_cast<BlockStmt &>(*function.body);

    std::unordered_map<std::string, CSEEntry> availableExprs;
    int tempCounter = 0;
    int stackSize = function.stackSize;
    std::vector<std::unique_ptr<Stmt>> newStmts;

    for (auto &stmt : block.statements) {
        if (stmt->kind == Stmt::Kind::Expr) {
            auto &exprStmt = static_cast<ExprStmt &>(*stmt);
            if (exprStmt.expr->kind == Expr::Kind::Assign) {
                auto &assign = static_cast<AssignExpr &>(*exprStmt.expr);
                cseExpr(exprStmt.expr, availableExprs, newStmts, tempCounter, stackSize);
                if (assign.target->kind == Expr::Kind::Variable) {
                    invalidateCSE(availableExprs, static_cast<const VariableExpr &>(*assign.target).name);
                } else {
                    // 通过指针或数组下标赋值，使所有 CSE 条目失效
                    availableExprs.clear();
                }
            } else if (exprStmt.expr->kind == Expr::Kind::Call) {
                // 函数调用可能修改任意内存，清除所有 CSE
                cseExpr(exprStmt.expr, availableExprs, newStmts, tempCounter, stackSize);
                availableExprs.clear();
            } else {
                cseExpr(exprStmt.expr, availableExprs, newStmts, tempCounter, stackSize);
            }
        } else if (stmt->kind == Stmt::Kind::Decl) {
            auto &decl = static_cast<DeclStmt &>(*stmt);
            if (decl.init) {
                cseExpr(decl.init, availableExprs, newStmts, tempCounter, stackSize);
            }
            invalidateCSE(availableExprs, decl.name);
        } else if (stmt->kind == Stmt::Kind::Return) {
            auto &ret = static_cast<ReturnStmt &>(*stmt);
            if (ret.expr) {
                cseExpr(ret.expr, availableExprs, newStmts, tempCounter, stackSize);
            }
        } else if (stmt->kind == Stmt::Kind::If) {
            auto &ifStmt = static_cast<IfStmt &>(*stmt);
            cseExpr(ifStmt.condition, availableExprs, newStmts, tempCounter, stackSize);

            // 保存当前可用表达式状态
            auto savedExprs = availableExprs;
            int savedCounter = tempCounter;
            int savedStack = stackSize;

            // 处理 then 分支
            std::vector<std::unique_ptr<Stmt>> thenStmts;
            if (ifStmt.thenBranch->kind == Stmt::Kind::Block) {
                auto &thenBlock = static_cast<BlockStmt &>(*ifStmt.thenBranch);
                for (auto &s : thenBlock.statements) {
                    if (s->kind == Stmt::Kind::Expr) {
                        auto &e = static_cast<ExprStmt &>(*s);
                        cseExpr(e.expr, availableExprs, thenStmts, tempCounter, stackSize);
                        if (e.expr->kind == Expr::Kind::Assign) {
                            auto &a = static_cast<AssignExpr &>(*e.expr);
                            if (a.target->kind == Expr::Kind::Variable) {
                                invalidateCSE(availableExprs, static_cast<const VariableExpr &>(*a.target).name);
                            } else {
                                availableExprs.clear();
                            }
                        }
                    } else if (s->kind == Stmt::Kind::Decl) {
                        auto &d = static_cast<DeclStmt &>(*s);
                        if (d.init) cseExpr(d.init, availableExprs, thenStmts, tempCounter, stackSize);
                        invalidateCSE(availableExprs, d.name);
                    } else {
                        // 控制流语句，保守清除
                        availableExprs.clear();
                    }
                    thenStmts.push_back(std::move(s));
                }
                thenBlock.statements = std::move(thenStmts);
            }

            auto thenExprs = availableExprs;
            int thenCounter = tempCounter;
            int thenStack = stackSize;

            // 处理 else 分支（如果有）
            if (ifStmt.elseBranch) {
                availableExprs = savedExprs;
                tempCounter = savedCounter;
                stackSize = savedStack;

                std::vector<std::unique_ptr<Stmt>> elseStmts;
                if (ifStmt.elseBranch->kind == Stmt::Kind::Block) {
                    auto &elseBlock = static_cast<BlockStmt &>(*ifStmt.elseBranch);
                    for (auto &s : elseBlock.statements) {
                        if (s->kind == Stmt::Kind::Expr) {
                            auto &e = static_cast<ExprStmt &>(*s);
                            cseExpr(e.expr, availableExprs, elseStmts, tempCounter, stackSize);
                            if (e.expr->kind == Expr::Kind::Assign) {
                                auto &a = static_cast<AssignExpr &>(*e.expr);
                                if (a.target->kind == Expr::Kind::Variable) {
                                    invalidateCSE(availableExprs, static_cast<const VariableExpr &>(*a.target).name);
                                } else {
                                    availableExprs.clear();
                                }
                            }
                        } else if (s->kind == Stmt::Kind::Decl) {
                            auto &d = static_cast<DeclStmt &>(*s);
                            if (d.init) cseExpr(d.init, availableExprs, elseStmts, tempCounter, stackSize);
                            invalidateCSE(availableExprs, d.name);
                        } else {
                            availableExprs.clear();
                        }
                        elseStmts.push_back(std::move(s));
                    }
                    elseBlock.statements = std::move(elseStmts);
                }

                // 合并：取两边交集
                for (auto it = availableExprs.begin(); it != availableExprs.end();) {
                    auto thenIt = thenExprs.find(it->first);
                    if (thenIt == thenExprs.end() || thenIt->second.tempName != it->second.tempName) {
                        it = availableExprs.erase(it);
                    } else {
                        ++it;
                    }
                }
                tempCounter = std::max(tempCounter, thenCounter);
                stackSize = std::max(stackSize, thenStack);
            } else {
                // 没有 else 分支，保守清除
                availableExprs.clear();
            }
        } else if (stmt->kind == Stmt::Kind::While || stmt->kind == Stmt::Kind::For ||
                   stmt->kind == Stmt::Kind::DoWhile) {
            // 循环：保守清除（循环可能执行多次）
            availableExprs.clear();
        }

        newStmts.push_back(std::move(stmt));
    }

    function.stackSize = (stackSize + 15) & ~15;
    block.statements = std::move(newStmts);
}
