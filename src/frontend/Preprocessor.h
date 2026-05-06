#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DiagnosticEngine;

// 预处理器：在词法分析之前处理源代码
// 支持 #include, #define, #undef, #if, #ifdef, #ifndef, #else, #elif, #endif, #pragma once
class Preprocessor {
public:
    // includePaths: 搜索头文件的目录列表
    explicit Preprocessor(std::vector<std::filesystem::path> includePaths = {}, DiagnosticEngine *diag = nullptr);

    // 预处理源文件，返回展开后的源代码
    std::string process(const std::filesystem::path &sourcePath);

    // 预处理源代码文本，返回展开后的源代码
    std::string processSource(const std::filesystem::path &sourcePath, const std::string &source);

    // 添加预定义宏（来自 -D 命令行参数）
    void addDefine(const std::string &name, const std::string &value = "1");

    // 获取所有被包含的文件路径（用于依赖文件生成）
    const std::unordered_set<std::filesystem::path> &getIncludedFiles() const { return includedFiles; }

private:
    struct Macro {
        std::string name;
        std::vector<std::string> parameters;  // 函数宏的参数列表（空表示对象宏）
        std::string body;                      // 宏体（原始文本）
        bool isFunctionLike = false;
        bool isVariadic = false;               // 是否有 ... 可变参数
    };

    struct ConditionalState {
        bool conditionMet = false;     // 当前分支或之前的分支是否已满足
        bool skipping = false;         // 当前是否在跳过代码
        bool wasActive = true;         // 进入条件块之前的活跃状态
    };

    // 展开源代码中的预处理指令
    std::string expandSource(const std::filesystem::path &sourcePath, const std::string &source);

    // 处理一行预处理指令
    std::string processDirective(const std::string &directive, const std::filesystem::path &currentPath);

    // 处理 #include
    std::string processInclude(const std::string &argument, const std::filesystem::path &currentPath);

    // 处理 #define
    void processDefine(const std::string &argument);

    // 处理 #undef
    void processUndef(const std::string &argument);

    // 处理条件编译指令
    bool processConditional(const std::string &directive, const std::string &argument);

    // 展开宏
    std::string expandMacros(const std::string &text);

    // 展开对象宏
    std::string expandObjectMacro(const Macro &macro);

    // 展开函数宏
    std::string expandFunctionMacro(const Macro &macro, const std::vector<std::string> &arguments);

    // 解析函数宏调用参数
    std::vector<std::string> parseMacroArguments(const std::string &text, std::size_t &pos);

    // 查找头文件
    std::filesystem::path findHeader(const std::string &name, bool isAngleBracket, const std::filesystem::path &currentPath);

    // 评估条件表达式（简单的常量表达式）
    int evaluateCondition(const std::string &expression);

    // 检查标识符是否已定义
    bool isDefined(const std::string &name) const;

    // 跳过空白字符
    static std::string trim(const std::string &str);

    // 分割宏参数
    static std::vector<std::string> splitArguments(const std::string &str);

    [[noreturn]] void fail(const std::string &message) const;

    DiagnosticEngine *diag;
    std::vector<std::filesystem::path> includePaths;
    std::unordered_map<std::string, Macro> macros;
    std::unordered_set<std::filesystem::path> pragmaOnceFiles;
    std::unordered_set<std::filesystem::path> currentlyProcessing;  // 递归包含检测
    std::unordered_set<std::filesystem::path> includedFiles;  // 依赖文件生成
    std::vector<ConditionalState> conditionalStack;
    bool skipping = false;  // 当前是否在跳过代码（由于条件编译）

    // #line 指令和 __LINE__/__FILE__ 预定义宏的跟踪状态
    int currentLineNumber_ = 0;
    std::string currentFileName_;
    void processLineDirective(const std::string &argument, int &lineNum);
};
