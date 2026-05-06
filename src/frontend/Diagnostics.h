#pragma once

#include <string>
#include <vector>
#include <iosfwd>
#include <unordered_map>

// 诊断严重级别
enum class DiagSeverity {
    Error,
    Warning,
    Note
};

// 单条诊断信息
struct Diagnostic {
    DiagSeverity severity;
    int line;
    int column;
    std::string message;
    std::string filePath;  // 源文件路径
};

// 源码位置信息，嵌入到 AST 节点中
struct SourceLocation {
    int line = 0;
    int column = 0;
};

// 警告级别控制
enum class WarningLevel {
    None,      // -w: 不显示警告
    Default,   // 默认: 仅关键警告
    All,       // -Wall: 所有常见警告
    Extra      // -Wextra: 所有额外警告
};

// 诊断引擎：收集编译过程中的所有诊断信息
class DiagnosticEngine {
public:
    // 设置当前源文件路径
    void setSourceFile(const std::string &path);

    // 设置警告级别
    void setWarningLevel(WarningLevel level);

    // 设置是否将警告视为错误
    void setWarningsAsErrors(bool enabled);

    // 禁用特定警告
    void suppressWarning(const std::string &name);

    // 设置源文件内容（用于显示错误上下文）
    void setSourceContent(const std::string &content);

    // 报告一条诊断信息
    void report(DiagSeverity severity, int line, int column, const std::string &message);

    // 便捷方法：报告错误
    void error(int line, int column, const std::string &message);

    // 便捷方法：报告警告
    void warning(int line, int column, const std::string &message);

    // 便捷方法：报告备注
    void note(int line, int column, const std::string &message);

    // 是否存在错误
    bool hasErrors() const;

    // 获取所有诊断信息
    const std::vector<Diagnostic> &diagnostics() const;

    // 获取错误数量
    std::size_t errorCount() const;

    // 将所有诊断信息输出到指定流
    void printAll(std::ostream &os) const;

    // 将所有诊断信息格式化为字符串
    std::string formatAll() const;

    // 清空所有诊断信息
    void clear();

private:
    std::vector<Diagnostic> diags;
    std::size_t errors = 0;
    std::string currentFilePath;
    std::vector<std::string> sourceLines;  // 源文件按行分割
    WarningLevel warningLevel = WarningLevel::Default;
    bool warningsAsErrors = false;
    std::unordered_map<std::string, bool> suppressedWarnings;

    // 获取指定行的源代码
    std::string getSourceLine(int line) const;
};
