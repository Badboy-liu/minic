#pragma once

#include "Ast.h"
#include "Diagnostics.h"
#include "Token.h"

#include <cstddef>
#include <optional>
#include <memory>
#include <unordered_map>
#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokenStream, DiagnosticEngine *diag = nullptr);

    Program parseProgram();

    // 是否有错误
    bool hasErrors() const;

private:
    struct ParsedDeclarator {
        std::string name;
        TypePtr type;
        std::unique_ptr<Expr> vlaSizeExpr;  // VLA 的运行时大小表达式
    };

    Parameter parseParameter();
    TypePtr parseStructType();
    TypePtr parseUnionType();
    void parseExternalDeclaration(Program &program);
    Function parseFunction(TypePtr returnType, std::string name);
    GlobalVar parseGlobalVariable(TypePtr declaredType, std::string name, bool isExternStorage);
    TypePtr parseType();
    TypePtr parseBaseType();
    TypePtr parseTypeSuffix(TypePtr baseType);
    ParsedDeclarator parseVariableDeclarator(TypePtr declaredType);
    std::vector<TypePtr> parseFunctionTypeParameters();
    std::unique_ptr<Expr> parseInitializer();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseForStatement();
    std::unique_ptr<Stmt> parseReturnStatement();
    std::unique_ptr<Stmt> parseDeclaration(TypePtr declaredType);
    std::unique_ptr<Stmt> parseIfStatement();
    std::unique_ptr<Stmt> parseWhileStatement();
    std::unique_ptr<Stmt> parseDoWhileStatement();
    std::unique_ptr<Stmt> parseSwitchStatement();
    std::unique_ptr<Stmt> parseCaseBody();
    std::unique_ptr<Stmt> parseBreakStatement();
    std::unique_ptr<Stmt> parseContinueStatement();
    std::unique_ptr<Stmt> parseGotoStatement();
    std::unique_ptr<Stmt> parseStaticAssert();
    std::unique_ptr<Stmt> parseExpressionStatement();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseTernary();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseBitwiseOr();
    std::unique_ptr<Expr> parseBitwiseXor();
    std::unique_ptr<Expr> parseBitwiseAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseTerm();
    std::unique_ptr<Expr> parseFactor();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parseGeneric();
    std::unique_ptr<Expr> parsePrimary();
    bool isTypeSpecifier(TokenKind kind) const;

    bool match(TokenKind kind);
    bool check(TokenKind kind) const;
    const Token &advance();
    const Token &peek() const;
    const Token &previous() const;
    const Token &consume(TokenKind kind, const char *message);
    [[noreturn]] void fail(const Token &token, const std::string &message) const;

    // 报告错误到诊断引擎（非致命）
    void diagError(const Token &token, const std::string &message);

    // 跳过 token 直到找到同步点（用于错误恢复）
    void synchronize();

    std::vector<Token> tokens;
    std::unordered_map<std::string, TypePtr> structTypes;
    std::unordered_map<std::string, TypePtr> unionTypes;
    std::unordered_map<std::string, TypePtr> typedefs;
    std::unordered_map<std::string, int> enumConstants;
    std::size_t current;
    DiagnosticEngine *diag;
    bool hasParseErrors = false;
};
