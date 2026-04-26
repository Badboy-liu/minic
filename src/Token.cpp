#include "Token.h"

const char *tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "end of file";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Number:
        return "number";
    case TokenKind::KeywordInt:
        return "int";
    case TokenKind::KeywordVoid:
        return "void";
    case TokenKind::KeywordReturn:
        return "return";
    case TokenKind::KeywordIf:
        return "if";
    case TokenKind::KeywordElse:
        return "else";
    case TokenKind::KeywordWhile:
        return "while";
    case TokenKind::KeywordFor:
        return "for";
    case TokenKind::KeywordBreak:
        return "break";
    case TokenKind::KeywordContinue:
        return "continue";
    case TokenKind::LeftParen:
        return "(";
    case TokenKind::RightParen:
        return ")";
    case TokenKind::LeftBrace:
        return "{";
    case TokenKind::RightBrace:
        return "}";
    case TokenKind::LeftBracket:
        return "[";
    case TokenKind::RightBracket:
        return "]";
    case TokenKind::Semicolon:
        return ";";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";
    case TokenKind::Ampersand:
        return "&";
    case TokenKind::Equal:
        return "=";
    case TokenKind::Bang:
        return "!";
    case TokenKind::EqualEqual:
        return "==";
    case TokenKind::BangEqual:
        return "!=";
    case TokenKind::AmpAmp:
        return "&&";
    case TokenKind::PipePipe:
        return "||";
    case TokenKind::Less:
        return "<";
    case TokenKind::LessEqual:
        return "<=";
    case TokenKind::Greater:
        return ">";
    case TokenKind::GreaterEqual:
        return ">=";
    }

    return "unknown";
}
