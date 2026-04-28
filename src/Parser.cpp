#include "Parser.h"

#include <stdexcept>

Parser::Parser(std::vector<Token> tokenStream)
    : tokens(std::move(tokenStream)), current(0) {}

Program Parser::parseProgram() {
    Program program;

    while (!check(TokenKind::EndOfFile)) {
        parseExternalDeclaration(program);
    }

    return program;
}

void Parser::parseExternalDeclaration(Program &program) {
    const bool isExternStorage = match(TokenKind::KeywordExtern);
    TypePtr baseType = parseType();
    const Token &nameToken = consume(TokenKind::Identifier, "expected identifier");
    if (check(TokenKind::LeftParen)) {
        program.functions.push_back(parseFunction(std::move(baseType), nameToken.lexeme));
        return;
    }
    program.globals.push_back(parseGlobalVariable(std::move(baseType), nameToken.lexeme, isExternStorage));
}

Function Parser::parseFunction(TypePtr returnType, std::string name) {
    Function function;
    function.returnType = std::move(returnType);
    function.name = std::move(name);
    consume(TokenKind::LeftParen, "expected '(' after function name");

    if (!check(TokenKind::RightParen)) {
        if (check(TokenKind::KeywordVoid)) {
            TypePtr probe = parseBaseType();
            if (!check(TokenKind::RightParen)) {
                while (match(TokenKind::Star)) {
                    probe = Type::makePointer(probe);
                }
                const Token &paramName = consume(TokenKind::Identifier, "expected parameter name");
                function.parameters.push_back(Parameter{std::move(probe), paramName.lexeme, 0});
                while (match(TokenKind::Comma)) {
                    TypePtr paramType = parseType();
                    const Token &nextName = consume(TokenKind::Identifier, "expected parameter name");
                    function.parameters.push_back(Parameter{std::move(paramType), nextName.lexeme, 0});
                }
            }
        } else {
            do {
                TypePtr paramType = parseType();
                const Token &paramName = consume(TokenKind::Identifier, "expected parameter name");
                function.parameters.push_back(Parameter{std::move(paramType), paramName.lexeme, 0});
            } while (match(TokenKind::Comma));
        }
    }

    consume(TokenKind::RightParen, "expected ')' after function parameters");
    if (match(TokenKind::Semicolon)) {
        return function;
    }

    function.body = parseBlock();
    return function;
}

GlobalVar Parser::parseGlobalVariable(TypePtr declaredType, std::string name, bool isExternStorage) {
    GlobalVar global;
    global.type = parseTypeSuffix(std::move(declaredType));
    global.name = std::move(name);
    global.isExternStorage = isExternStorage;

    if (match(TokenKind::Equal)) {
        global.init = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after global declaration");
    return global;
}

TypePtr Parser::parseType() {
    TypePtr type = parseBaseType();
    while (match(TokenKind::Star)) {
        type = Type::makePointer(type);
    }
    return type;
}

TypePtr Parser::parseTypeSuffix(TypePtr baseType) {
    if (match(TokenKind::LeftBracket)) {
        const Token &lengthToken = consume(TokenKind::Number, "expected array length");
        consume(TokenKind::RightBracket, "expected ']' after array length");
        return Type::makeArray(std::move(baseType), lengthToken.intValue);
    }
    return baseType;
}

TypePtr Parser::parseBaseType() {
    if (match(TokenKind::KeywordChar)) {
        return Type::makeChar();
    }
    if (match(TokenKind::KeywordShort)) {
        return Type::makeShort();
    }
    if (match(TokenKind::KeywordLong)) {
        if (match(TokenKind::KeywordLong)) {
            return Type::makeLongLong();
        }
        return Type::makeLong();
    }
    if (match(TokenKind::KeywordInt)) {
        return Type::makeInt();
    }
    if (match(TokenKind::KeywordVoid)) {
        return Type::makeVoid();
    }
    fail(peek(), "expected type specifier");
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    consume(TokenKind::LeftBrace, "expected '{' to start a block");
    auto block = std::make_unique<BlockStmt>();

    while (!check(TokenKind::RightBrace)) {
        if (check(TokenKind::EndOfFile)) {
            fail(peek(), "unterminated block");
        }
        block->statements.push_back(parseStatement());
    }

    consume(TokenKind::RightBrace, "expected '}' to close a block");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (match(TokenKind::KeywordReturn)) {
        return parseReturnStatement();
    }
    if (isTypeSpecifier(peek().kind)) {
        return parseDeclaration(parseType());
    }
    if (match(TokenKind::KeywordIf)) {
        return parseIfStatement();
    }
    if (match(TokenKind::KeywordWhile)) {
        return parseWhileStatement();
    }
    if (match(TokenKind::KeywordFor)) {
        return parseForStatement();
    }
    if (match(TokenKind::KeywordBreak)) {
        return parseBreakStatement();
    }
    if (match(TokenKind::KeywordContinue)) {
        return parseContinueStatement();
    }
    if (check(TokenKind::LeftBrace)) {
        return parseBlock();
    }

    return parseExpressionStatement();
}

std::unique_ptr<Stmt> Parser::parseForStatement() {
    consume(TokenKind::LeftParen, "expected '(' after 'for'");

    std::unique_ptr<Stmt> init;
    if (match(TokenKind::Semicolon)) {
    } else if (isTypeSpecifier(peek().kind)) {
        init = parseDeclaration(parseType());
    } else {
        auto initExpr = parseExpression();
        consume(TokenKind::Semicolon, "expected ';' after for initializer");
        init = std::make_unique<ExprStmt>(std::move(initExpr));
    }

    std::unique_ptr<Expr> condition;
    if (!check(TokenKind::Semicolon)) {
        condition = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after for condition");

    std::unique_ptr<Expr> update;
    if (!check(TokenKind::RightParen)) {
        update = parseExpression();
    }
    consume(TokenKind::RightParen, "expected ')' after for clauses");

    return std::make_unique<ForStmt>(std::move(init), std::move(condition), std::move(update), parseStatement());
}

std::unique_ptr<Stmt> Parser::parseReturnStatement() {
    if (match(TokenKind::Semicolon)) {
        return std::make_unique<ReturnStmt>(nullptr);
    }

    auto expr = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after return value");
    return std::make_unique<ReturnStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::parseDeclaration(TypePtr declaredType) {
    const Token &nameToken = consume(TokenKind::Identifier, "expected variable name");
    TypePtr type = parseTypeSuffix(std::move(declaredType));

    std::unique_ptr<Expr> initializer;
    if (match(TokenKind::Equal)) {
        initializer = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after declaration");
    return std::make_unique<DeclStmt>(std::move(type), nameToken.lexeme, std::move(initializer));
}

std::unique_ptr<Stmt> Parser::parseIfStatement() {
    consume(TokenKind::LeftParen, "expected '(' after 'if'");
    auto condition = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after if condition");

    auto thenBranch = parseStatement();
    std::unique_ptr<Stmt> elseBranch;
    if (match(TokenKind::KeywordElse)) {
        elseBranch = parseStatement();
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

std::unique_ptr<Stmt> Parser::parseWhileStatement() {
    consume(TokenKind::LeftParen, "expected '(' after 'while'");
    auto condition = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after while condition");
    return std::make_unique<WhileStmt>(std::move(condition), parseStatement());
}

std::unique_ptr<Stmt> Parser::parseBreakStatement() {
    consume(TokenKind::Semicolon, "expected ';' after 'break'");
    return std::make_unique<BreakStmt>();
}

std::unique_ptr<Stmt> Parser::parseContinueStatement() {
    consume(TokenKind::Semicolon, "expected ';' after 'continue'");
    return std::make_unique<ContinueStmt>();
}

std::unique_ptr<Stmt> Parser::parseExpressionStatement() {
    auto expr = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after expression");
    return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto lhs = parseLogicalOr();
    if (!match(TokenKind::Equal)) {
        return lhs;
    }

    auto value = parseAssignment();
    return std::make_unique<AssignExpr>(std::move(lhs), std::move(value));
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (match(TokenKind::PipePipe)) {
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalOr, std::move(expr), parseLogicalAnd());
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseEquality();

    while (match(TokenKind::AmpAmp)) {
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(expr), parseEquality());
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto expr = parseComparison();

    while (true) {
        if (match(TokenKind::EqualEqual)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Equal, std::move(expr), parseComparison());
        } else if (match(TokenKind::BangEqual)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::NotEqual, std::move(expr), parseComparison());
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto expr = parseTerm();

    while (true) {
        if (match(TokenKind::Less)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Less, std::move(expr), parseTerm());
        } else if (match(TokenKind::LessEqual)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::LessEqual, std::move(expr), parseTerm());
        } else if (match(TokenKind::Greater)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Greater, std::move(expr), parseTerm());
        } else if (match(TokenKind::GreaterEqual)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::GreaterEqual, std::move(expr), parseTerm());
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseTerm() {
    auto expr = parseFactor();

    while (true) {
        if (match(TokenKind::Plus)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Add, std::move(expr), parseFactor());
        } else if (match(TokenKind::Minus)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Subtract, std::move(expr), parseFactor());
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseFactor() {
    auto expr = parseUnary();

    while (true) {
        if (match(TokenKind::Star)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Multiply, std::move(expr), parseUnary());
        } else if (match(TokenKind::Slash)) {
            expr = std::make_unique<BinaryExpr>(BinaryOp::Divide, std::move(expr), parseUnary());
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (match(TokenKind::Plus)) {
        return std::make_unique<UnaryExpr>(UnaryOp::Plus, parseUnary());
    }
    if (match(TokenKind::Minus)) {
        return std::make_unique<UnaryExpr>(UnaryOp::Minus, parseUnary());
    }
    if (match(TokenKind::Bang)) {
        return std::make_unique<UnaryExpr>(UnaryOp::LogicalNot, parseUnary());
    }
    if (match(TokenKind::Ampersand)) {
        return std::make_unique<UnaryExpr>(UnaryOp::AddressOf, parseUnary());
    }
    if (match(TokenKind::Star)) {
        return std::make_unique<UnaryExpr>(UnaryOp::Dereference, parseUnary());
    }

    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (match(TokenKind::LeftParen)) {
            if (expr->kind != Expr::Kind::Variable) {
                fail(previous(), "function call target must be an identifier");
            }

            auto *callee = static_cast<VariableExpr *>(expr.get());
            std::vector<std::unique_ptr<Expr>> arguments;
            if (!check(TokenKind::RightParen)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RightParen, "expected ')' after function arguments");
            expr = std::make_unique<CallExpr>(callee->name, std::move(arguments));
            continue;
        }

        if (match(TokenKind::LeftBracket)) {
            auto index = parseExpression();
            consume(TokenKind::RightBracket, "expected ']' after subscript");
            expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (match(TokenKind::Number)) {
        return std::make_unique<NumberExpr>(previous().intValue);
    }
    if (match(TokenKind::StringLiteral)) {
        return std::make_unique<StringExpr>(previous().stringValue);
    }
    if (match(TokenKind::Identifier)) {
        return std::make_unique<VariableExpr>(previous().lexeme);
    }
    if (match(TokenKind::LeftParen)) {
        auto expr = parseExpression();
        consume(TokenKind::RightParen, "expected ')' after expression");
        return expr;
    }

    fail(peek(), "expected expression");
}

bool Parser::isTypeSpecifier(TokenKind kind) const {
    return kind == TokenKind::KeywordChar ||
        kind == TokenKind::KeywordShort ||
        kind == TokenKind::KeywordInt ||
        kind == TokenKind::KeywordLong ||
        kind == TokenKind::KeywordVoid;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }

    advance();
    return true;
}

bool Parser::check(TokenKind kind) const {
    return peek().kind == kind;
}

const Token &Parser::advance() {
    if (current < tokens.size()) {
        ++current;
    }
    return previous();
}

const Token &Parser::peek() const {
    return tokens[current];
}

const Token &Parser::previous() const {
    return tokens[current - 1];
}

const Token &Parser::consume(TokenKind kind, const char *message) {
    if (check(kind)) {
        return advance();
    }

    fail(peek(), message);
}

[[noreturn]] void Parser::fail(const Token &token, const std::string &message) const {
    throw std::runtime_error(
        "Parser error at line " + std::to_string(token.line) + ", column " + std::to_string(token.column) +
        ": " + message + " near '" + tokenKindName(token.kind) + "'");
}
