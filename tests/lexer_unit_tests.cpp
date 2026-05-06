#include "Lexer.h"

#include <gtest/gtest.h>

// 辅助函数：将 token 流转换为 kind 序列
static std::vector<TokenKind> tokenizeKinds(const std::string &source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    std::vector<TokenKind> kinds;
    for (const auto &t : tokens) {
        if (t.kind != TokenKind::EndOfFile) {
            kinds.push_back(t.kind);
        }
    }
    return kinds;
}

// 辅助函数：获取指定 kind 的 token 数量
static int countTokens(const std::string &source, TokenKind kind) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    int count = 0;
    for (const auto &t : tokens) {
        if (t.kind == kind) count++;
    }
    return count;
}

// ===== 基本 token 测试 =====

TEST(LexerUnitTest, EmptySource) {
    Lexer lexer("");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::EndOfFile);
}

TEST(LexerUnitTest, SingleInteger) {
    Lexer lexer("42");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 42);
}

TEST(LexerUnitTest, HexLiteral) {
    Lexer lexer("0xFF");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 255);
}

TEST(LexerUnitTest, OctalLiteral) {
    Lexer lexer("077");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 63);
}

TEST(LexerUnitTest, BinaryLiteral) {
    Lexer lexer("0b1010");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 10);
}

TEST(LexerUnitTest, IntegerSuffixU) {
    Lexer lexer("42u");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 42);
}

TEST(LexerUnitTest, IntegerSuffixLL) {
    Lexer lexer("42LL");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 42);
}

// ===== 浮点数测试 =====

TEST(LexerUnitTest, FloatLiteral) {
    Lexer lexer("3.14");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::FloatLiteral);
    EXPECT_DOUBLE_EQ(tokens[0].doubleValue, 3.14);
}

TEST(LexerUnitTest, FloatWithExponent) {
    Lexer lexer("1.5e10");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::FloatLiteral);
}

// ===== 字符串字面量测试 =====

TEST(LexerUnitTest, StringLiteral) {
    Lexer lexer("\"hello\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "hello");
}

TEST(LexerUnitTest, StringWithEscapes) {
    Lexer lexer("\"a\\nb\\tc\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "a\nb\tc");
}

TEST(LexerUnitTest, StringConcatenation) {
    Lexer lexer("\"hello\" \" world\"");
    auto tokens = lexer.tokenize();
    // 应该被合并为一个字符串
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "hello world");
}

// ===== 宽字符串测试 =====

TEST(LexerUnitTest, WideStringLiteral) {
    Lexer lexer("L\"wide\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "wide");
    EXPECT_EQ(tokens[0].stringPrefix, "L");
}

TEST(LexerUnitTest, Utf8StringLiteral) {
    Lexer lexer("u8\"utf8\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "utf8");
    EXPECT_EQ(tokens[0].stringPrefix, "u8");
}

TEST(LexerUnitTest, Utf16StringLiteral) {
    Lexer lexer("u\"utf16\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "utf16");
    EXPECT_EQ(tokens[0].stringPrefix, "u");
}

TEST(LexerUnitTest, Utf32StringLiteral) {
    Lexer lexer("U\"utf32\"");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].stringValue, "utf32");
    EXPECT_EQ(tokens[0].stringPrefix, "U");
}

TEST(LexerUnitTest, WideCharLiteral) {
    Lexer lexer("L'A'");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 65);
}

// ===== 字符常量测试 =====

TEST(LexerUnitTest, CharLiteral) {
    Lexer lexer("'A'");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 65);
}

TEST(LexerUnitTest, CharEscapeNewline) {
    Lexer lexer("'\\n'");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(tokens[0].intValue, 10);
}

// ===== 关键字测试 =====

TEST(LexerUnitTest, Keywords) {
    auto kinds = tokenizeKinds("int char void if else while for return");
    EXPECT_EQ(kinds.size(), 8u);
    EXPECT_EQ(kinds[0], TokenKind::KeywordInt);
    EXPECT_EQ(kinds[1], TokenKind::KeywordChar);
    EXPECT_EQ(kinds[2], TokenKind::KeywordVoid);
    EXPECT_EQ(kinds[3], TokenKind::KeywordIf);
    EXPECT_EQ(kinds[4], TokenKind::KeywordElse);
    EXPECT_EQ(kinds[5], TokenKind::KeywordWhile);
    EXPECT_EQ(kinds[6], TokenKind::KeywordFor);
    EXPECT_EQ(kinds[7], TokenKind::KeywordReturn);
}

TEST(LexerUnitTest, ModernKeywords) {
    auto kinds = tokenizeKinds("const static inline _Bool _Alignas _Static_assert");
    EXPECT_EQ(kinds[0], TokenKind::KeywordConst);
    EXPECT_EQ(kinds[1], TokenKind::KeywordStatic);
    EXPECT_EQ(kinds[2], TokenKind::KeywordInline);
    EXPECT_EQ(kinds[3], TokenKind::KeywordBool);
    EXPECT_EQ(kinds[4], TokenKind::KeywordAlignas);
    EXPECT_EQ(kinds[5], TokenKind::KeywordStaticAssert);
}

// ===== 运算符测试 =====

TEST(LexerUnitTest, Operators) {
    auto kinds = tokenizeKinds("+ - * / = == != < > <= >= && || ++ -- += -=");
    EXPECT_EQ(kinds[0], TokenKind::Plus);
    EXPECT_EQ(kinds[1], TokenKind::Minus);
    EXPECT_EQ(kinds[2], TokenKind::Star);
    EXPECT_EQ(kinds[3], TokenKind::Slash);
    EXPECT_EQ(kinds[4], TokenKind::Equal);
    EXPECT_EQ(kinds[5], TokenKind::EqualEqual);
    EXPECT_EQ(kinds[6], TokenKind::BangEqual);
    EXPECT_EQ(kinds[7], TokenKind::Less);
    EXPECT_EQ(kinds[8], TokenKind::Greater);
    EXPECT_EQ(kinds[9], TokenKind::LessEqual);
    EXPECT_EQ(kinds[10], TokenKind::GreaterEqual);
    EXPECT_EQ(kinds[11], TokenKind::AmpAmp);
    EXPECT_EQ(kinds[12], TokenKind::PipePipe);
    EXPECT_EQ(kinds[13], TokenKind::PlusPlus);
    EXPECT_EQ(kinds[14], TokenKind::MinusMinus);
    EXPECT_EQ(kinds[15], TokenKind::PlusEqual);
    EXPECT_EQ(kinds[16], TokenKind::MinusEqual);
}

// ===== 标识符测试 =====

TEST(LexerUnitTest, Identifier) {
    Lexer lexer("myVar");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].lexeme, "myVar");
}

TEST(LexerUnitTest, IdentifierWithUnderscore) {
    Lexer lexer("_my_var_123");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].lexeme, "_my_var_123");
}

// ===== 注释测试 =====

TEST(LexerUnitTest, LineComment) {
    // 行注释应该被跳过
    int count = countTokens("42 // comment\n", TokenKind::Number);
    EXPECT_EQ(count, 1);
}

TEST(LexerUnitTest, BlockComment) {
    int count = countTokens("42 /* comment */ 43", TokenKind::Number);
    EXPECT_EQ(count, 2);
}

// ===== 行号跟踪测试 =====

TEST(LexerUnitTest, LineTracking) {
    Lexer lexer("a\nb\nc");
    auto tokens = lexer.tokenize();
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 2);
    EXPECT_EQ(tokens[2].line, 3);
}

// ===== 错误处理测试 =====

TEST(LexerUnitTest, UnterminatedString) {
    Lexer lexer("\"hello");
    EXPECT_THROW(lexer.tokenize(), std::runtime_error);
}

TEST(LexerUnitTest, UnterminatedChar) {
    Lexer lexer("'a");
    EXPECT_THROW(lexer.tokenize(), std::runtime_error);
}

TEST(LexerUnitTest, InvalidHexLiteral) {
    Lexer lexer("0xG");
    EXPECT_THROW(lexer.tokenize(), std::runtime_error);
}

// ===== 复杂表达式测试 =====

TEST(LexerUnitTest, ComplexExpression) {
    auto kinds = tokenizeKinds("x = a + b * (c - d)");
    EXPECT_EQ(kinds[0], TokenKind::Identifier);  // x
    EXPECT_EQ(kinds[1], TokenKind::Equal);        // =
    EXPECT_EQ(kinds[2], TokenKind::Identifier);  // a
    EXPECT_EQ(kinds[3], TokenKind::Plus);         // +
    EXPECT_EQ(kinds[4], TokenKind::Identifier);  // b
    EXPECT_EQ(kinds[5], TokenKind::Star);         // *
    EXPECT_EQ(kinds[6], TokenKind::LeftParen);    // (
    EXPECT_EQ(kinds[7], TokenKind::Identifier);  // c
    EXPECT_EQ(kinds[8], TokenKind::Minus);        // -
    EXPECT_EQ(kinds[9], TokenKind::Identifier);  // d
    EXPECT_EQ(kinds[10], TokenKind::RightParen);  // )
}

// ===== 特殊字符测试 =====

TEST(LexerUnitTest, SpecialCharacters) {
    auto kinds = tokenizeKinds("{ } [ ] ( ) ; , . -> ... ? :");
    EXPECT_EQ(kinds[0], TokenKind::LeftBrace);
    EXPECT_EQ(kinds[1], TokenKind::RightBrace);
    EXPECT_EQ(kinds[2], TokenKind::LeftBracket);
    EXPECT_EQ(kinds[3], TokenKind::RightBracket);
    EXPECT_EQ(kinds[4], TokenKind::LeftParen);
    EXPECT_EQ(kinds[5], TokenKind::RightParen);
    EXPECT_EQ(kinds[6], TokenKind::Semicolon);
    EXPECT_EQ(kinds[7], TokenKind::Comma);
    EXPECT_EQ(kinds[8], TokenKind::Dot);
    EXPECT_EQ(kinds[9], TokenKind::Arrow);
    EXPECT_EQ(kinds[10], TokenKind::DotDotDot);
    EXPECT_EQ(kinds[11], TokenKind::Question);
    EXPECT_EQ(kinds[12], TokenKind::Colon);
}
