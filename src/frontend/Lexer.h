#pragma once

#include "Token.h"

#include <string>
#include <string_view>
#include <vector>

class DiagnosticEngine;

class Lexer {
public:
    explicit Lexer(std::string_view sourceText, DiagnosticEngine *diag = nullptr);

    std::vector<Token> tokenize();

private:
    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind kind, std::string lexeme, long long value = 0) const;
    Token makeFloatToken(std::string lexeme, double value) const;
    Token lexNumber();
    Token lexStringLiteral(std::string prefix = "");
    Token lexCharLiteral(std::string prefix = "");
    Token lexIdentifierOrKeyword();
    [[noreturn]] void fail(const std::string &message) const;

    DiagnosticEngine *diag;
    std::string source;
    std::size_t current;
    int line;
    int column;
    int tokenLine;
    int tokenColumn;
};
