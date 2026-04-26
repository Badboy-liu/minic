#include "Lexer.h"

#include <cctype>
#include <stdexcept>

Lexer::Lexer(std::string_view sourceText)
    : source(sourceText), current(0), line(1), column(1), tokenLine(1), tokenColumn(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skipWhitespaceAndComments();
        tokenLine = line;
        tokenColumn = column;

        if (isAtEnd()) {
            tokens.push_back(makeToken(TokenKind::EndOfFile, ""));
            break;
        }

        const char ch = advance();
        switch (ch) {
        case '(':
            tokens.push_back(makeToken(TokenKind::LeftParen, "("));
            break;
        case ')':
            tokens.push_back(makeToken(TokenKind::RightParen, ")"));
            break;
        case '{':
            tokens.push_back(makeToken(TokenKind::LeftBrace, "{"));
            break;
        case '}':
            tokens.push_back(makeToken(TokenKind::RightBrace, "}"));
            break;
        case '[':
            tokens.push_back(makeToken(TokenKind::LeftBracket, "["));
            break;
        case ']':
            tokens.push_back(makeToken(TokenKind::RightBracket, "]"));
            break;
        case ';':
            tokens.push_back(makeToken(TokenKind::Semicolon, ";"));
            break;
        case ',':
            tokens.push_back(makeToken(TokenKind::Comma, ","));
            break;
        case '+':
            tokens.push_back(makeToken(TokenKind::Plus, "+"));
            break;
        case '-':
            tokens.push_back(makeToken(TokenKind::Minus, "-"));
            break;
        case '*':
            tokens.push_back(makeToken(TokenKind::Star, "*"));
            break;
        case '/':
            tokens.push_back(makeToken(TokenKind::Slash, "/"));
            break;
        case '&':
            if (match('&')) {
                tokens.push_back(makeToken(TokenKind::AmpAmp, "&&"));
            } else {
                tokens.push_back(makeToken(TokenKind::Ampersand, "&"));
            }
            break;
        case '=': {
            const bool isEqualEqual = match('=');
            tokens.push_back(makeToken(isEqualEqual ? TokenKind::EqualEqual : TokenKind::Equal, isEqualEqual ? "==" : "="));
            break;
        }
        case '!':
            if (match('=')) {
                tokens.push_back(makeToken(TokenKind::BangEqual, "!="));
            } else {
                tokens.push_back(makeToken(TokenKind::Bang, "!"));
            }
            break;
        case '|':
            if (!match('|')) {
                fail("unexpected character '|'");
            }
            tokens.push_back(makeToken(TokenKind::PipePipe, "||"));
            break;
        case '<': {
            const bool isLessEqual = match('=');
            tokens.push_back(makeToken(isLessEqual ? TokenKind::LessEqual : TokenKind::Less, isLessEqual ? "<=" : "<"));
            break;
        }
        case '>': {
            const bool isGreaterEqual = match('=');
            tokens.push_back(makeToken(
                isGreaterEqual ? TokenKind::GreaterEqual : TokenKind::Greater,
                isGreaterEqual ? ">=" : ">"));
            break;
        }
        default:
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                --current;
                --column;
                tokens.push_back(lexNumber());
            } else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
                --current;
                --column;
                tokens.push_back(lexIdentifierOrKeyword());
            } else {
                fail(std::string("unexpected character '") + ch + "'");
            }
            break;
        }
    }

    return tokens;
}

bool Lexer::isAtEnd() const {
    return current >= source.size();
}

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source[current];
}

char Lexer::peekNext() const {
    return current + 1 >= source.size() ? '\0' : source[current + 1];
}

char Lexer::advance() {
    const char ch = source[current++];
    if (ch == '\n') {
        ++line;
        column = 1;
    } else {
        ++column;
    }
    return ch;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) {
        return false;
    }

    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        const char ch = peek();
        if (ch == ' ' || ch == '\r' || ch == '\t' || ch == '\n') {
            advance();
            continue;
        }

        if (ch == '/' && peekNext() == '/') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }

        break;
    }
}

Token Lexer::makeToken(TokenKind kind, std::string lexeme, int value) const {
    return Token{kind, std::move(lexeme), value, tokenLine, tokenColumn};
}

Token Lexer::lexNumber() {
    tokenLine = line;
    tokenColumn = column;
    const std::size_t start = current;
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    const std::string lexeme = source.substr(start, current - start);
    return makeToken(TokenKind::Number, lexeme, std::stoi(lexeme));
}

Token Lexer::lexIdentifierOrKeyword() {
    tokenLine = line;
    tokenColumn = column;
    const std::size_t start = current;
    while (!isAtEnd()) {
        const char ch = peek();
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
            break;
        }
        advance();
    }

    const std::string lexeme = source.substr(start, current - start);
    if (lexeme == "int") {
        return makeToken(TokenKind::KeywordInt, lexeme);
    }
    if (lexeme == "void") {
        return makeToken(TokenKind::KeywordVoid, lexeme);
    }
    if (lexeme == "return") {
        return makeToken(TokenKind::KeywordReturn, lexeme);
    }
    if (lexeme == "if") {
        return makeToken(TokenKind::KeywordIf, lexeme);
    }
    if (lexeme == "else") {
        return makeToken(TokenKind::KeywordElse, lexeme);
    }
    if (lexeme == "while") {
        return makeToken(TokenKind::KeywordWhile, lexeme);
    }
    if (lexeme == "for") {
        return makeToken(TokenKind::KeywordFor, lexeme);
    }
    if (lexeme == "break") {
        return makeToken(TokenKind::KeywordBreak, lexeme);
    }
    if (lexeme == "continue") {
        return makeToken(TokenKind::KeywordContinue, lexeme);
    }

    return makeToken(TokenKind::Identifier, lexeme);
}

[[noreturn]] void Lexer::fail(const std::string &message) const {
    throw std::runtime_error(
        "Lexer error at line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message);
}
