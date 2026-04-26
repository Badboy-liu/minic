#pragma once

#include "Ast.h"
#include "Token.h"

#include <cstddef>
#include <memory>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokenStream);

    Program parseProgram();

private:
    Function parseFunction();
    TypePtr parseType();
    TypePtr parseBaseType();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseForStatement();
    std::unique_ptr<Stmt> parseReturnStatement();
    std::unique_ptr<Stmt> parseDeclaration(TypePtr declaredType);
    std::unique_ptr<Stmt> parseIfStatement();
    std::unique_ptr<Stmt> parseWhileStatement();
    std::unique_ptr<Stmt> parseBreakStatement();
    std::unique_ptr<Stmt> parseContinueStatement();
    std::unique_ptr<Stmt> parseExpressionStatement();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseTerm();
    std::unique_ptr<Expr> parseFactor();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    bool isTypeSpecifier(TokenKind kind) const;

    bool match(TokenKind kind);
    bool check(TokenKind kind) const;
    const Token &advance();
    const Token &peek() const;
    const Token &previous() const;
    const Token &consume(TokenKind kind, const char *message);
    [[noreturn]] void fail(const Token &token, const std::string &message) const;

    std::vector<Token> tokens;
    std::size_t current;
};
