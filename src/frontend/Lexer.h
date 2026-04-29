#pragma once

#include "Token.h"

#include <string>
#include <string_view>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string_view sourceText);

    std::vector<Token> tokenize();

private:
    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    void skipWhitespaceAndComments();
    Token makeToken(TokenKind kind, std::string lexeme, int value = 0) const;
    Token lexNumber();
    Token lexStringLiteral();
    Token lexIdentifierOrKeyword();
    [[noreturn]] void fail(const std::string &message) const;

    std::string source;
    std::size_t current;
    int line;
    int column;
    int tokenLine;
    int tokenColumn;
};
