#pragma once

#include <string>

enum class TokenKind {
    EndOfFile,
    Identifier,
    Number,
    FloatLiteral,
    StringLiteral,
    KeywordBool,
    KeywordChar,
    KeywordShort,
    KeywordInt,
    KeywordLong,
    KeywordFloat,
    KeywordDouble,
    KeywordVoid,
    KeywordSigned,
    KeywordUnsigned,
    KeywordExtern,
    KeywordReturn,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordFor,
    KeywordBreak,
    KeywordContinue,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Plus,
    Minus,
    Star,
    Slash,
    Ampersand,
    Equal,
    Bang,
    EqualEqual,
    BangEqual,
    AmpAmp,
    PipePipe,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct Token {
    TokenKind kind;
    std::string lexeme;
    int intValue;
    double doubleValue;
    std::string stringValue;
    int line;
    int column;
};

const char *tokenKindName(TokenKind kind);
