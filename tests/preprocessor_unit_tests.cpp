#include "Preprocessor.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// 辅助函数：创建临时文件并返回路径
static fs::path createTempFile(const std::string &name, const std::string &content) {
    fs::path tempDir = fs::temp_directory_path() / "minic_test";
    fs::create_directories(tempDir);
    fs::path filePath = tempDir / name;
    std::ofstream out(filePath);
    out << content;
    out.close();
    return filePath;
}

// ===== 基本功能测试 =====

TEST(PreprocessorUnitTest, EmptySource) {
    Preprocessor pp;
    std::string result = pp.processSource("test.c", "");
    EXPECT_TRUE(result.empty());
}

TEST(PreprocessorUnitTest, PassthroughPlainCode) {
    Preprocessor pp;
    std::string result = pp.processSource("test.c", "int x = 42;");
    EXPECT_EQ(result.find("int x = 42;"), 0u);
}

// ===== #define 测试 =====

TEST(PreprocessorUnitTest, ObjectMacro) {
    Preprocessor pp;
    pp.addDefine("VALUE", "42");
    std::string result = pp.processSource("test.c", "int x = VALUE;");
    EXPECT_NE(result.find("42"), std::string::npos);
    EXPECT_EQ(result.find("VALUE"), std::string::npos);
}

TEST(PreprocessorUnitTest, DefineWithDefault) {
    Preprocessor pp;
    pp.addDefine("FLAG");
    std::string result = pp.processSource("test.c", "int x = FLAG;");
    EXPECT_NE(result.find("1"), std::string::npos);
}

TEST(PreprocessorUnitTest, UndefDirective) {
    Preprocessor pp;
    std::string source = "#define X 10\n#undef X\nint x = X;\n";
    std::string result = pp.processSource("test.c", source);
    // X 应该未被展开
    EXPECT_NE(result.find("X"), std::string::npos);
}

// ===== #ifdef / #ifndef 测试 =====

TEST(PreprocessorUnitTest, IfdefDefined) {
    Preprocessor pp;
    pp.addDefine("MY_FLAG");
    std::string source = "#ifdef MY_FLAG\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_NE(result.find("int x = 1;"), std::string::npos);
    EXPECT_EQ(result.find("int x = 2;"), std::string::npos);
}

TEST(PreprocessorUnitTest, IfdefUndefined) {
    Preprocessor pp;
    std::string source = "#ifdef MY_FLAG\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_EQ(result.find("int x = 1;"), std::string::npos);
    EXPECT_NE(result.find("int x = 2;"), std::string::npos);
}

TEST(PreprocessorUnitTest, IfndefDefined) {
    Preprocessor pp;
    pp.addDefine("MY_FLAG");
    std::string source = "#ifndef MY_FLAG\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_EQ(result.find("int x = 1;"), std::string::npos);
    EXPECT_NE(result.find("int x = 2;"), std::string::npos);
}

TEST(PreprocessorUnitTest, IfndefUndefined) {
    Preprocessor pp;
    std::string source = "#ifndef MY_FLAG\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_NE(result.find("int x = 1;"), std::string::npos);
    EXPECT_EQ(result.find("int x = 2;"), std::string::npos);
}

// ===== #if 测试 =====

TEST(PreprocessorUnitTest, IfTrue) {
    Preprocessor pp;
    std::string source = "#if 1\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_NE(result.find("int x = 1;"), std::string::npos);
    EXPECT_EQ(result.find("int x = 2;"), std::string::npos);
}

TEST(PreprocessorUnitTest, IfFalse) {
    Preprocessor pp;
    std::string source = "#if 0\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_EQ(result.find("int x = 1;"), std::string::npos);
    EXPECT_NE(result.find("int x = 2;"), std::string::npos);
}

TEST(PreprocessorUnitTest, IfExpression) {
    Preprocessor pp;
    pp.addDefine("VERSION", "2");
    std::string source = "#if VERSION >= 2\nint x = 1;\n#else\nint x = 2;\n#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_NE(result.find("int x = 1;"), std::string::npos);
}

// ===== #error 测试 =====

TEST(PreprocessorUnitTest, ErrorDirective) {
    Preprocessor pp;
    std::string source = "#error not supported\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

// ===== #include 测试 =====

TEST(PreprocessorUnitTest, IncludeQuote) {
    fs::path header = createTempFile("test_header.h", "int header_var = 1;\n");
    fs::path source = createTempFile("test_include.c", "#include \"test_header.h\"\n");

    Preprocessor pp({}, nullptr);
    std::string result = pp.process(source);
    EXPECT_NE(result.find("int header_var = 1;"), std::string::npos);

    // 清理
    fs::remove(header);
    fs::remove(source);
}

TEST(PreprocessorUnitTest, IncludeMissing) {
    Preprocessor pp;
    std::string source = "#include \"nonexistent.h\"\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

// ===== 嵌套条件编译 =====

TEST(PreprocessorUnitTest, NestedConditional) {
    Preprocessor pp;
    pp.addDefine("A");
    std::string source =
        "#ifdef A\n"
        "  #ifdef B\n"
        "    int x = 1;\n"
        "  #else\n"
        "    int x = 2;\n"
        "  #endif\n"
        "#else\n"
        "  int x = 3;\n"
        "#endif\n";
    std::string result = pp.processSource("test.c", source);
    EXPECT_NE(result.find("int x = 2;"), std::string::npos);
    EXPECT_EQ(result.find("int x = 1;"), std::string::npos);
    EXPECT_EQ(result.find("int x = 3;"), std::string::npos);
}

// ===== 行号指令测试 =====

TEST(PreprocessorUnitTest, LineDirective) {
    Preprocessor pp;
    std::string source = "#line 100 \"custom.c\"\nint x = 1;\n";
    std::string result = pp.processSource("test.c", source);
    // #line 指令应该影响后续行号
    EXPECT_NE(result.find("int x = 1;"), std::string::npos);
}

// ===== 错误处理测试 =====

TEST(PreprocessorUnitTest, UnterminatedInclude) {
    Preprocessor pp;
    std::string source = "#include \"unclosed\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

TEST(PreprocessorUnitTest, ElseWithoutIf) {
    Preprocessor pp;
    std::string source = "#else\nint x = 1;\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

TEST(PreprocessorUnitTest, EndifWithoutIf) {
    Preprocessor pp;
    std::string source = "#endif\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

TEST(PreprocessorUnitTest, EmptyDefine) {
    Preprocessor pp;
    std::string source = "#define\n";
    EXPECT_THROW(pp.processSource("test.c", source), std::runtime_error);
}

// ===== 递归包含检测 =====

TEST(PreprocessorUnitTest, RecursiveInclude) {
    // 创建两个互相包含的文件
    fs::path headerA = createTempFile("rec_a.h", "#include \"rec_b.h\"\nint a = 1;\n");
    fs::path headerB = createTempFile("rec_b.h", "#include \"rec_a.h\"\nint b = 1;\n");
    fs::path source = createTempFile("rec_test.c", "#include \"rec_a.h\"\n");

    Preprocessor pp({}, nullptr);
    EXPECT_THROW(pp.process(source), std::runtime_error);

    // 清理
    fs::remove(headerA);
    fs::remove(headerB);
    fs::remove(source);
}

// ===== pragma once 测试 =====

TEST(PreprocessorUnitTest, PragmaOnce) {
    fs::path header = createTempFile("pragma_once.h", "#pragma once\nint x = 1;\n");
    fs::path source = createTempFile("pragma_test.c",
        "#include \"pragma_once.h\"\n#include \"pragma_once.h\"\n");

    Preprocessor pp({}, nullptr);
    std::string result = pp.process(source);
    // x = 1 应该只出现一次（pragma once 防止重复包含）
    int count = 0;
    std::size_t pos = 0;
    while ((pos = result.find("int x = 1;", pos)) != std::string::npos) {
        count++;
        pos += 10;
    }
    EXPECT_EQ(count, 1);

    // 清理
    fs::remove(header);
    fs::remove(source);
}
