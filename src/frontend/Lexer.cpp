#include "Lexer.h"

#include "Diagnostics.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

Lexer::Lexer(std::string_view sourceText, DiagnosticEngine *diag)
    : diag(diag), source(sourceText), current(0), line(1), column(1), tokenLine(1), tokenColumn(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(std::max<size_t>(256, source.size() / 4));

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
        case '.':
            if (!isAtEnd() && peek() == '.' && !isAtEnd() && peekNext() == '.') {
                advance();
                advance();
                tokens.push_back(makeToken(TokenKind::DotDotDot, "..."));
            } else if (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                --current;
                --column;
                tokens.push_back(lexNumber());
            } else {
                tokens.push_back(makeToken(TokenKind::Dot, "."));
            }
            break;
        case ';':
            tokens.push_back(makeToken(TokenKind::Semicolon, ";"));
            break;
        case ',':
            tokens.push_back(makeToken(TokenKind::Comma, ","));
            break;
        case '+':
            if (match('+')) {
                tokens.push_back(makeToken(TokenKind::PlusPlus, "++"));
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::PlusEqual, "+="));
            } else {
                tokens.push_back(makeToken(TokenKind::Plus, "+"));
            }
            break;
        case '-':
            if (match('>')) {
                tokens.push_back(makeToken(TokenKind::Arrow, "->"));
            } else if (match('-')) {
                tokens.push_back(makeToken(TokenKind::MinusMinus, "--"));
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::MinusEqual, "-="));
            } else {
                tokens.push_back(makeToken(TokenKind::Minus, "-"));
            }
            break;
        case '*':
            if (match('=')) {
                tokens.push_back(makeToken(TokenKind::StarEqual, "*="));
            } else {
                tokens.push_back(makeToken(TokenKind::Star, "*"));
            }
            break;
        case '/':
            if (match('=')) {
                tokens.push_back(makeToken(TokenKind::SlashEqual, "/="));
            } else {
                tokens.push_back(makeToken(TokenKind::Slash, "/"));
            }
            break;
        case '&':
            if (match('&')) {
                tokens.push_back(makeToken(TokenKind::AmpAmp, "&&"));
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::AmpEqual, "&="));
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
            if (match('|')) {
                tokens.push_back(makeToken(TokenKind::PipePipe, "||"));
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::PipeEqual, "|="));
            } else {
                tokens.push_back(makeToken(TokenKind::Pipe, "|"));
            }
            break;
        case '<':
            if (match('<')) {
                if (match('=')) {
                    tokens.push_back(makeToken(TokenKind::LessLessEqual, "<<="));
                } else {
                    tokens.push_back(makeToken(TokenKind::LessLess, "<<"));
                }
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::LessEqual, "<="));
            } else {
                tokens.push_back(makeToken(TokenKind::Less, "<"));
            }
            break;
        case '>':
            if (match('>')) {
                if (match('=')) {
                    tokens.push_back(makeToken(TokenKind::GreaterGreaterEqual, ">>="));
                } else {
                    tokens.push_back(makeToken(TokenKind::GreaterGreater, ">>"));
                }
            } else if (match('=')) {
                tokens.push_back(makeToken(TokenKind::GreaterEqual, ">="));
            } else {
                tokens.push_back(makeToken(TokenKind::Greater, ">"));
            }
            break;
        case '"':
            tokens.push_back(lexStringLiteral());
            break;
        case '\'':
            tokens.push_back(lexCharLiteral());
            break;
        case ':':
            tokens.push_back(makeToken(TokenKind::Colon, ":"));
            break;
        case '%':
            if (match('=')) {
                tokens.push_back(makeToken(TokenKind::PercentEqual, "%="));
            } else {
                tokens.push_back(makeToken(TokenKind::Percent, "%"));
            }
            break;
        case '^':
            if (match('=')) {
                tokens.push_back(makeToken(TokenKind::CaretEqual, "^="));
            } else {
                tokens.push_back(makeToken(TokenKind::Caret, "^"));
            }
            break;
        case '~':
            tokens.push_back(makeToken(TokenKind::Tilde, "~"));
            break;
        case '?':
            tokens.push_back(makeToken(TokenKind::Question, "?"));
            break;
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

    // 字符串字面量拼接：相邻的字符串字面量自动合并
    std::vector<Token> merged;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::StringLiteral && !merged.empty() &&
            merged.back().kind == TokenKind::StringLiteral) {
            merged.back().stringValue += tokens[i].stringValue;
            merged.back().lexeme += tokens[i].lexeme;
        } else {
            merged.push_back(std::move(tokens[i]));
        }
    }

    return merged;
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

        if (ch == '/' && peekNext() == '*') {
            advance(); // skip '/'
            advance(); // skip '*'
            while (!isAtEnd()) {
                if (peek() == '*' && peekNext() == '/') {
                    advance(); // skip '*'
                    advance(); // skip '/'
                    break;
                }
                advance();
            }
            continue;
        }

        break;
    }
}

Token Lexer::makeToken(TokenKind kind, std::string lexeme, long long value) const {
    return Token{kind, std::move(lexeme), value, 0.0, "", "", tokenLine, tokenColumn};
}

Token Lexer::makeFloatToken(std::string lexeme, double value) const {
    return Token{TokenKind::FloatLiteral, std::move(lexeme), 0, value, "", "", tokenLine, tokenColumn};
}

Token Lexer::lexNumber() {
    tokenLine = line;
    tokenColumn = column;
    const std::size_t start = current;
    bool isFloat = false;

    // 检查十六进制/八进制/二进制前缀
    if (peek() == '0' && !isAtEnd()) {
        char next = source[current + 1];
        if (next == 'x' || next == 'X') {
            // 十六进制: 0x...
            advance(); // '0'
            advance(); // 'x'
            if (isAtEnd() || !std::isxdigit(static_cast<unsigned char>(peek()))) {
                fail("expected hex digit after 0x");
            }
            while (!isAtEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
            // 跳过整数后缀
            while (!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')) {
                advance();
            }
            const std::string lexeme = source.substr(start, current - start);
            return makeToken(TokenKind::Number, lexeme, std::stoll(lexeme, nullptr, 16));
        }
        if (next == 'b' || next == 'B') {
            // 二进制: 0b...
            advance(); // '0'
            advance(); // 'b'
            if (isAtEnd() || (peek() != '0' && peek() != '1')) {
                fail("expected binary digit after 0b");
            }
            while (!isAtEnd() && (peek() == '0' || peek() == '1')) {
                advance();
            }
            while (!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')) {
                advance();
            }
            const std::string lexeme = source.substr(start, current - start);
            return makeToken(TokenKind::Number, lexeme, std::stoll(lexeme.substr(2), nullptr, 2));
        }
        if (std::isdigit(static_cast<unsigned char>(next))) {
            // 八进制: 0...
            advance(); // '0'
            while (!isAtEnd() && peek() >= '0' && peek() <= '7') {
                advance();
            }
            while (!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')) {
                advance();
            }
            const std::string lexeme = source.substr(start, current - start);
            return makeToken(TokenKind::Number, lexeme, std::stoll(lexeme, nullptr, 8));
        }
    }

    // 整数部分
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    // 小数部分
    if (!isAtEnd() && peek() == '.' && (current + 1 >= source.size() || std::isdigit(static_cast<unsigned char>(source[current + 1])))) {
        isFloat = true;
        advance(); // 消费 '.'
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    // 指数部分 (e/E)
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        isFloat = true;
        advance(); // 消费 'e'/'E'
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            advance();
        }
        if (isAtEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            fail("expected digit in floating point exponent");
        }
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    const std::string lexeme = source.substr(start, current - start);
    if (isFloat) {
        char *endptr = nullptr;
        const double doubleValue = std::strtod(lexeme.c_str(), &endptr);
        return makeFloatToken(lexeme, doubleValue);
    }
    // 跳过整数后缀 (U, L, LL, UL, ULL 等)
    while (!isAtEnd() && (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')) {
        advance();
    }
    const std::string finalLexeme = source.substr(start, current - start);
    return makeToken(TokenKind::Number, finalLexeme, std::stoll(finalLexeme));
}

Token Lexer::lexStringLiteral(std::string prefix) {
    tokenLine = line;
    tokenColumn = column - 1;
    if (!prefix.empty()) {
        // 前缀模式：开头的引号还没有被消费，需要跳过
        advance();
    }
    std::string value;

    while (!isAtEnd()) {
        const char ch = advance();
        if (ch == '"') {
            Token token = makeToken(TokenKind::StringLiteral, value);
            token.stringValue = value;
            token.stringPrefix = std::move(prefix);
            return token;
        }
        if (ch == '\\') {
            if (isAtEnd()) {
                fail("unterminated escape sequence in string literal");
            }
            const char escaped = advance();
            switch (escaped) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '"':
                value.push_back('"');
                break;
            case '\'':
                value.push_back('\'');
                break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                // 八进制转义：\0 到 \377
                int octVal = escaped - '0';
                for (int k = 0; k < 2 && !isAtEnd(); ++k) {
                    char c = peek();
                    if (c >= '0' && c <= '7') {
                        octVal = octVal * 8 + (advance() - '0');
                    } else {
                        break;
                    }
                }
                value.push_back(static_cast<char>(octVal & 0xFF));
                break;
            }
            case 'x': {
                // 十六进制转义：\x00 到 \xFF
                int hexVal = 0;
                int digits = 0;
                while (!isAtEnd() && digits < 2) {
                    char c = peek();
                    int d = -1;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    else break;
                    hexVal = hexVal * 16 + d;
                    advance();
                    ++digits;
                }
                if (digits == 0) {
                    fail("\\x followed by no hex digits");
                }
                value.push_back(static_cast<char>(hexVal & 0xFF));
                break;
            }
            default:
                fail(std::string("unsupported escape sequence '\\") + escaped + "'");
            }
            continue;
        }
        if (ch == '\n') {
            fail("unterminated string literal");
        }
        value.push_back(ch);
    }

    fail("unterminated string literal");
}

Token Lexer::lexCharLiteral(std::string prefix) {
    tokenLine = line;
    tokenColumn = column - 1;
    if (!prefix.empty()) {
        // 前缀模式：开头的单引号还没有被消费，需要跳过
        advance();
    }

    if (isAtEnd()) {
        fail("unterminated character literal");
    }

    int charValue;
    const char ch = advance();
    if (ch == '\\') {
        // 转义字符
        if (isAtEnd()) {
            fail("unterminated escape sequence in character literal");
        }
        const char escaped = advance();
        switch (escaped) {
        case 'n':
            charValue = 10;
            break;
        case 't':
            charValue = 9;
            break;
        case 'r':
            charValue = 13;
            break;
        case '\\':
            charValue = 92;
            break;
        case '\'':
            charValue = 39;
            break;
        case '"':
            charValue = 34;
            break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            // 八进制转义
            charValue = escaped - '0';
            for (int k = 0; k < 2 && !isAtEnd(); ++k) {
                char c = peek();
                if (c >= '0' && c <= '7') {
                    charValue = charValue * 8 + (advance() - '0');
                } else {
                    break;
                }
            }
            break;
        }
        case 'x': {
            // 十六进制转义
            charValue = 0;
            int digits = 0;
            while (!isAtEnd() && digits < 2) {
                char c = peek();
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                charValue = charValue * 16 + d;
                advance();
                ++digits;
            }
            if (digits == 0) {
                fail("\\x followed by no hex digits in character literal");
            }
            break;
        }
        default:
            fail(std::string("unsupported escape sequence '\\") + escaped + "' in character literal");
        }
    } else if (ch == '\'') {
        fail("empty character literal");
    } else if (ch == '\n') {
        fail("unterminated character literal");
    } else {
        charValue = static_cast<int>(static_cast<unsigned char>(ch));
        // 多字符字面量：继续读取直到关闭的 '
        while (!isAtEnd() && peek() != '\'' && peek() != '\n') {
            char next = advance();
            charValue = (charValue << 8) | static_cast<int>(static_cast<unsigned char>(next));
        }
    }

    // 读取关闭的单引号
    if (isAtEnd() || advance() != '\'') {
        fail("unterminated character literal");
    }

    return makeToken(TokenKind::Number, std::to_string(charValue), charValue);
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

    // 宽字符/多字节字符串前缀检测：L"...", u8"...", u"...", U"..."
    if (!isAtEnd()) {
        char next = peek();
        if (lexeme == "L" || lexeme == "U") {
            if (next == '"') return lexStringLiteral(lexeme);
            if (next == '\'') return lexCharLiteral(lexeme);
        } else if (lexeme == "u") {
            if (next == '"') return lexStringLiteral(lexeme);
            if (next == '\'') return lexCharLiteral(lexeme);
        } else if (lexeme == "u8") {
            if (next == '"') return lexStringLiteral(lexeme);
            if (next == '\'') return lexCharLiteral(lexeme);
        }
    }
    if (lexeme == "char") {
        return makeToken(TokenKind::KeywordChar, lexeme);
    }
    if (lexeme == "struct") {
        return makeToken(TokenKind::KeywordStruct, lexeme);
    }
    if (lexeme == "short") {
        return makeToken(TokenKind::KeywordShort, lexeme);
    }
    if (lexeme == "int") {
        return makeToken(TokenKind::KeywordInt, lexeme);
    }
    if (lexeme == "long") {
        return makeToken(TokenKind::KeywordLong, lexeme);
    }
    if (lexeme == "void") {
        return makeToken(TokenKind::KeywordVoid, lexeme);
    }
    if (lexeme == "extern") {
        return makeToken(TokenKind::KeywordExtern, lexeme);
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
    if (lexeme == "do") {
        return makeToken(TokenKind::KeywordDo, lexeme);
    }
    if (lexeme == "switch") {
        return makeToken(TokenKind::KeywordSwitch, lexeme);
    }
    if (lexeme == "case") {
        return makeToken(TokenKind::KeywordCase, lexeme);
    }
    if (lexeme == "default") {
        return makeToken(TokenKind::KeywordDefault, lexeme);
    }
    if (lexeme == "unsigned") {
        return makeToken(TokenKind::KeywordUnsigned, lexeme);
    }
    if (lexeme == "signed") {
        return makeToken(TokenKind::KeywordSigned, lexeme);
    }
    if (lexeme == "typedef") {
        return makeToken(TokenKind::KeywordTypedef, lexeme);
    }
    if (lexeme == "enum") {
        return makeToken(TokenKind::KeywordEnum, lexeme);
    }
    if (lexeme == "union") {
        return makeToken(TokenKind::KeywordUnion, lexeme);
    }
    if (lexeme == "const") {
        return makeToken(TokenKind::KeywordConst, lexeme);
    }
    if (lexeme == "volatile") {
        return makeToken(TokenKind::KeywordVolatile, lexeme);
    }
    if (lexeme == "sizeof") {
        return makeToken(TokenKind::KeywordSizeof, lexeme);
    }
    if (lexeme == "goto") {
        return makeToken(TokenKind::KeywordGoto, lexeme);
    }
    if (lexeme == "float") {
        return makeToken(TokenKind::KeywordFloat, lexeme);
    }
    if (lexeme == "double") {
        return makeToken(TokenKind::KeywordDouble, lexeme);
    }
    if (lexeme == "static") {
        return makeToken(TokenKind::KeywordStatic, lexeme);
    }
    if (lexeme == "restrict") {
        return makeToken(TokenKind::KeywordRestrict, lexeme);
    }
    if (lexeme == "inline") {
        return makeToken(TokenKind::KeywordInline, lexeme);
    }
    if (lexeme == "_Noreturn") {
        return makeToken(TokenKind::KeywordNoreturn, lexeme);
    }
    if (lexeme == "_Atomic") {
        return makeToken(TokenKind::KeywordAtomic, lexeme);
    }
    if (lexeme == "_Alignas") {
        return makeToken(TokenKind::KeywordAlignas, lexeme);
    }
    if (lexeme == "_Thread_local") {
        return makeToken(TokenKind::KeywordThreadLocal, lexeme);
    }
    if (lexeme == "_Static_assert") {
        return makeToken(TokenKind::KeywordStaticAssert, lexeme);
    }
    if (lexeme == "_Generic") {
        return makeToken(TokenKind::KeywordGeneric, lexeme);
    }
    if (lexeme == "_Alignof") {
        return makeToken(TokenKind::KeywordAlignof, lexeme);
    }
    if (lexeme == "_Bool") {
        return makeToken(TokenKind::KeywordBool, lexeme);
    }

    return makeToken(TokenKind::Identifier, lexeme);
}

[[noreturn]] void Lexer::fail(const std::string &message) const {
    if (diag) {
        diag->error(line, column, message);
    }
    throw std::runtime_error(
        "Lexer error at line " + std::to_string(line) + ", column " + std::to_string(column) + ": " + message);
}
