#include "Diagnostics.h"

#include <sstream>
#include <algorithm>

void DiagnosticEngine::setSourceFile(const std::string &path) {
    currentFilePath = path;
}

void DiagnosticEngine::setWarningLevel(WarningLevel level) {
    warningLevel = level;
}

void DiagnosticEngine::setWarningsAsErrors(bool enabled) {
    warningsAsErrors = enabled;
}

void DiagnosticEngine::suppressWarning(const std::string &name) {
    suppressedWarnings[name] = true;
}

void DiagnosticEngine::setSourceContent(const std::string &content) {
    sourceLines.clear();
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        // 移除行尾的 \r（Windows 换行符）
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        sourceLines.push_back(line);
    }
}

std::string DiagnosticEngine::getSourceLine(int line) const {
    if (line < 1 || line > static_cast<int>(sourceLines.size())) {
        return "";
    }
    return sourceLines[line - 1];
}

void DiagnosticEngine::report(DiagSeverity severity, int line, int column, const std::string &message) {
    diags.push_back({severity, line, column, message, currentFilePath});
    if (severity == DiagSeverity::Error) {
        ++errors;
    }
}

void DiagnosticEngine::error(int line, int column, const std::string &message) {
    report(DiagSeverity::Error, line, column, message);
}

void DiagnosticEngine::warning(int line, int column, const std::string &message) {
    if (warningLevel == WarningLevel::None) return;
    if (warningsAsErrors) {
        report(DiagSeverity::Error, line, column, message);
    } else {
        report(DiagSeverity::Warning, line, column, message);
    }
}

void DiagnosticEngine::note(int line, int column, const std::string &message) {
    report(DiagSeverity::Note, line, column, message);
}

bool DiagnosticEngine::hasErrors() const {
    return errors > 0;
}

const std::vector<Diagnostic> &DiagnosticEngine::diagnostics() const {
    return diags;
}

std::size_t DiagnosticEngine::errorCount() const {
    return errors;
}

static const char *severityName(DiagSeverity severity) {
    switch (severity) {
    case DiagSeverity::Error:   return "error";
    case DiagSeverity::Warning: return "warning";
    case DiagSeverity::Note:    return "note";
    }
    return "unknown";
}

static std::string severityColor(DiagSeverity severity) {
    switch (severity) {
    case DiagSeverity::Error:   return "\033[1;31m";  // 红色粗体
    case DiagSeverity::Warning: return "\033[1;33m";  // 黄色粗体
    case DiagSeverity::Note:    return "\033[1;36m";  // 青色粗体
    }
    return "";
}

static const std::string RESET_COLOR = "\033[0m";
static const std::string GREEN_COLOR = "\033[1;32m";
static const std::string BLUE_COLOR = "\033[1;34m";

void DiagnosticEngine::printAll(std::ostream &os) const {
    for (const auto &diag : diags) {
        // 文件位置
        if (!diag.filePath.empty()) {
            os << BLUE_COLOR << diag.filePath << RESET_COLOR << ":";
        }
        os << GREEN_COLOR << diag.line << ":" << diag.column << RESET_COLOR
           << ": " << severityColor(diag.severity) << severityName(diag.severity) << RESET_COLOR
           << ": " << diag.message << "\n";

        // 显示源代码上下文
        std::string sourceLine = getSourceLine(diag.line);
        if (!sourceLine.empty()) {
            os << "  " << sourceLine << "\n";
            // 显示错误位置的指示器
            if (diag.column > 0) {
                os << "  ";
                for (int i = 1; i < diag.column; ++i) {
                    os << (sourceLine[i - 1] == '\t' ? '\t' : ' ');
                }
                os << severityColor(diag.severity) << "^" << RESET_COLOR << "\n";
            }
        }
    }
}

std::string DiagnosticEngine::formatAll() const {
    std::ostringstream oss;
    printAll(oss);
    return oss.str();
}

void DiagnosticEngine::clear() {
    diags.clear();
    errors = 0;
    sourceLines.clear();
    currentFilePath.clear();
}
