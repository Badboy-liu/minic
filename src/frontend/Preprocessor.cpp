#include "Preprocessor.h"

#include "Diagnostics.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

Preprocessor::Preprocessor(std::vector<fs::path> paths, DiagnosticEngine *diag)
    : diag(diag), includePaths(std::move(paths)) {}

[[noreturn]] void Preprocessor::fail(const std::string &message) const {
    if (diag) {
        diag->error(currentLineNumber_, 0, message);
    }
    throw std::runtime_error("预处理器：" + message);
}

void Preprocessor::addDefine(const std::string &name, const std::string &value) {
    Macro macro;
    macro.name = name;
    macro.body = value;
    macros[name] = std::move(macro);
}

std::string Preprocessor::process(const fs::path &sourcePath) {
    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        fail("无法打开源文件: " + sourcePath.string());
    }
    std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // 标记初始源文件正在处理（用于递归包含检测）
    fs::path canonicalSource;
    try {
        canonicalSource = fs::canonical(sourcePath);
    } catch (...) {
        canonicalSource = sourcePath;
    }
    currentlyProcessing.insert(canonicalSource);
    std::string result = expandSource(sourcePath, source);
    currentlyProcessing.erase(canonicalSource);
    return result;
}

std::string Preprocessor::processSource(const fs::path &sourcePath, const std::string &source) {
    // 标记初始源文件正在处理（用于递归包含检测）
    fs::path canonicalSource;
    try {
        canonicalSource = fs::canonical(sourcePath);
    } catch (...) {
        canonicalSource = sourcePath;
    }
    currentlyProcessing.insert(canonicalSource);
    std::string result = expandSource(sourcePath, source);
    currentlyProcessing.erase(canonicalSource);
    return result;
}

std::string Preprocessor::expandSource(const fs::path &sourcePath, const std::string &source) {
    std::string result;
    result.reserve(source.size());

    std::istringstream stream(source);
    std::string line;
    int lineNum = 0;
    currentFileName_ = sourcePath.filename().string();

    while (std::getline(stream, line)) {
        ++lineNum;
        currentLineNumber_ = lineNum;

        // 移除行尾的 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // 检查是否是预处理指令
        std::string trimmed = trim(line);
        if (!trimmed.empty() && trimmed[0] == '#') {
            // 解析指令名和参数
            std::size_t directiveStart = 1;
            while (directiveStart < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[directiveStart]))) {
                ++directiveStart;
            }
            std::size_t directiveEnd = directiveStart;
            while (directiveEnd < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[directiveEnd])) &&
                   trimmed[directiveEnd] != '(') {
                ++directiveEnd;
            }
            std::string directive = trimmed.substr(directiveStart, directiveEnd - directiveStart);
            std::string argument = trim(trimmed.substr(directiveEnd));

            // 处理条件编译指令（这些指令即使在跳过模式下也需要处理）
            if (directive == "if" || directive == "ifdef" || directive == "ifndef" ||
                directive == "else" || directive == "elif" || directive == "endif") {
                processConditional(directive, argument);
                continue;
            }

            // 如果当前在跳过代码，忽略其他指令
            if (skipping) {
                continue;
            }

            // 处理其他指令
            if (directive == "include") {
                result += processInclude(argument, sourcePath);
            } else if (directive == "define") {
                processDefine(argument);
            } else if (directive == "undef") {
                processUndef(argument);
            } else if (directive == "pragma") {
                // 处理 #pragma once
                if (argument == "once") {
                    pragmaOnceFiles.insert(fs::canonical(sourcePath));
                }
                // 其他 pragma 指令忽略
            } else if (directive == "line") {
                processLineDirective(argument, lineNum);
            } else if (directive == "warning") {
                std::cerr << sourcePath.string() << ":" << lineNum
                          << ": warning: #warning " << argument << "\n";
            } else if (directive == "error") {
                fail("#error " + argument + " (at " + sourcePath.string() + ":" + std::to_string(lineNum) + ")");
            } else {
                // 未知指令，保留原样（可能不是预处理指令）
                result += line + "\n";
            }
        } else {
            // 普通行，展开宏
            if (!skipping) {
                result += expandMacros(line) + "\n";
            }
        }
    }

    return result;
}

std::string Preprocessor::processDirective(const std::string &directive, const std::filesystem::path &currentPath) {
    // 这个方法主要用于处理展开后的指令（递归展开时）
    return "";
}

std::string Preprocessor::processInclude(const std::string &argument, const fs::path &currentPath) {
    if (argument.empty()) {
        fail("#include 缺少文件名");
    }

    std::string filename;
    bool isAngleBracket = false;

    if (argument[0] == '"') {
        // #include "file.h"
        auto endQuote = argument.find('"', 1);
        if (endQuote == std::string::npos) {
            fail("#include 引号未关闭");
        }
        filename = argument.substr(1, endQuote - 1);
        isAngleBracket = false;
    } else if (argument[0] == '<') {
        // #include <file.h>
        auto endAngle = argument.find('>', 1);
        if (endAngle == std::string::npos) {
            fail("#include 尖括号未关闭");
        }
        filename = argument.substr(1, endAngle - 1);
        isAngleBracket = true;
    } else {
        fail("#include 格式错误: " + argument);
    }

    fs::path headerPath = findHeader(filename, isAngleBracket, currentPath);
    if (headerPath.empty()) {
        fail("找不到头文件: " + filename);
    }

    // 获取规范化路径用于检测
    fs::path canonicalPath;
    try {
        canonicalPath = fs::canonical(headerPath);
    } catch (...) {
        canonicalPath = headerPath;
    }

    // 检查 #pragma once
    if (pragmaOnceFiles.count(canonicalPath)) {
        includedFiles.insert(canonicalPath);
        return "";  // 已经包含过，跳过
    }

    // 递归包含检测：防止 A 包含 B，B 又包含 A 的无限循环
    if (currentlyProcessing.count(canonicalPath)) {
        fail("检测到递归包含: " + filename);
    }

    // 记录被包含的文件（用于依赖文件生成）
    includedFiles.insert(canonicalPath);

    // 标记当前文件正在处理
    currentlyProcessing.insert(canonicalPath);

    // 读取并展开头文件
    std::ifstream in(headerPath, std::ios::binary);
    if (!in) {
        currentlyProcessing.erase(canonicalPath);
        fail("无法打开头文件: " + headerPath.string());
    }
    std::string headerSource((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string result = expandSource(headerPath, headerSource);

    // 处理完毕，移除标记
    currentlyProcessing.erase(canonicalPath);

    return result;
}

void Preprocessor::processDefine(const std::string &argument) {
    if (argument.empty()) {
        fail("#define 缺少宏名称");
    }

    Macro macro;
    std::size_t pos = 0;

    // 读取宏名称
    while (pos < argument.size() && (std::isalnum(static_cast<unsigned char>(argument[pos])) || argument[pos] == '_')) {
        macro.name += argument[pos];
        ++pos;
    }

    if (macro.name.empty()) {
        fail("#define 缺少宏名称");
    }

    // 跳过空白
    while (pos < argument.size() && std::isspace(static_cast<unsigned char>(argument[pos]))) {
        ++pos;
    }

    // 检查是否是函数宏
    if (pos < argument.size() && argument[pos] == '(') {
        macro.isFunctionLike = true;
        ++pos;  // 跳过 '('

        // 解析参数列表
        std::string param;
        while (pos < argument.size() && argument[pos] != ')') {
            if (argument[pos] == '.') {
                // 检查是否是 ... 可变参数
                if (pos + 2 < argument.size() && argument[pos + 1] == '.' && argument[pos + 2] == '.') {
                    macro.isVariadic = true;
                    pos += 3;
                    // 跳过 ... 后面的空白，直到 ')'
                    while (pos < argument.size() && std::isspace(static_cast<unsigned char>(argument[pos]))) {
                        ++pos;
                    }
                    continue;
                }
            }
            if (argument[pos] == ',') {
                macro.parameters.push_back(trim(param));
                param.clear();
            } else {
                param += argument[pos];
            }
            ++pos;
        }
        if (!param.empty()) {
            macro.parameters.push_back(trim(param));
        }
        if (pos < argument.size()) {
            ++pos;  // 跳过 ')'
        }
    }

    // 剩余部分是宏体
    while (pos < argument.size() && std::isspace(static_cast<unsigned char>(argument[pos]))) {
        ++pos;
    }
    macro.body = argument.substr(pos);

    macros[macro.name] = macro;
}

void Preprocessor::processUndef(const std::string &argument) {
    std::string name = trim(argument);
    macros.erase(name);
}

bool Preprocessor::processConditional(const std::string &directive, const std::string &argument) {
    if (directive == "if") {
        // 保存当前状态
        conditionalStack.push_back({false, skipping, !skipping});
        if (!skipping) {
            int value = evaluateCondition(argument);
            skipping = (value == 0);
            conditionalStack.back().conditionMet = (value != 0);
        }
    } else if (directive == "ifdef") {
        conditionalStack.push_back({false, skipping, !skipping});
        if (!skipping) {
            std::string name = trim(argument);
            bool defined = isDefined(name);
            skipping = !defined;
            conditionalStack.back().conditionMet = defined;
        }
    } else if (directive == "ifndef") {
        conditionalStack.push_back({false, skipping, !skipping});
        if (!skipping) {
            std::string name = trim(argument);
            bool defined = isDefined(name);
            skipping = defined;
            conditionalStack.back().conditionMet = !defined;
        }
    } else if (directive == "else") {
        if (conditionalStack.empty()) {
            fail("#else 没有匹配的 #if");
        }
        auto &state = conditionalStack.back();
        if (state.wasActive) {
            if (state.conditionMet) {
                skipping = true;
            } else {
                skipping = false;
                state.conditionMet = true;
            }
        }
    } else if (directive == "elif") {
        if (conditionalStack.empty()) {
            fail("#elif 没有匹配的 #if");
        }
        auto &state = conditionalStack.back();
        if (state.wasActive) {
            if (state.conditionMet) {
                skipping = true;
            } else {
                int value = evaluateCondition(argument);
                skipping = (value == 0);
                state.conditionMet = (value != 0);
            }
        }
    } else if (directive == "endif") {
        if (conditionalStack.empty()) {
            fail("#endif 没有匹配的 #if");
        }
        skipping = conditionalStack.back().skipping;
        conditionalStack.pop_back();
    }

    return skipping;
}

std::string Preprocessor::expandMacros(const std::string &text) {
    std::string result = text;

    // 展开预定义宏 __LINE__
    {
        const std::string lineStr = std::to_string(currentLineNumber_);
        std::size_t pos = 0;
        while ((pos = result.find("__LINE__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 8 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 8])) && result[pos + 8] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 8, lineStr);
                pos += lineStr.size();
            } else {
                pos += 8;
            }
        }
    }

    // 展开预定义宏 __FILE__
    {
        const std::string fileStr = "\"" + currentFileName_ + "\"";
        std::size_t pos = 0;
        while ((pos = result.find("__FILE__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 8 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 8])) && result[pos + 8] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 8, fileStr);
                pos += fileStr.size();
            } else {
                pos += 8;
            }
        }
    }

    // 展开预定义宏 __DATE__
    {
        std::time_t now = std::time(nullptr);
        char buf[12];
        std::strftime(buf, sizeof(buf), "%b %e %Y", std::localtime(&now));
        const std::string dateStr = std::string("\"") + buf + "\"";
        std::size_t pos = 0;
        while ((pos = result.find("__DATE__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 8 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 8])) && result[pos + 8] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 8, dateStr);
                pos += dateStr.size();
            } else {
                pos += 8;
            }
        }
    }

    // 展开预定义宏 __TIME__
    {
        std::time_t now = std::time(nullptr);
        char buf[12];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
        const std::string timeStr = std::string("\"") + buf + "\"";
        std::size_t pos = 0;
        while ((pos = result.find("__TIME__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 8 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 8])) && result[pos + 8] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 8, timeStr);
                pos += timeStr.size();
            } else {
                pos += 8;
            }
        }
    }

    // 展开预定义宏 __STDC__
    {
        std::size_t pos = 0;
        while ((pos = result.find("__STDC__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 8 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 8])) && result[pos + 8] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 8, "1");
                pos += 1;
            } else {
                pos += 8;
            }
        }
    }

    // 展开预定义宏 __STDC_VERSION__
    {
        std::size_t pos = 0;
        while ((pos = result.find("__STDC_VERSION__", pos)) != std::string::npos) {
            const bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
            const bool validEnd = (pos + 16 >= result.size() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[pos + 16])) && result[pos + 16] != '_'));
            if (validStart && validEnd) {
                result.replace(pos, 16, "199901L");
                pos += 7;
            } else {
                pos += 16;
            }
        }
    }

    // 简单的宏展开：查找宏名称并替换
    // 注意：这是一个简化的实现，不处理所有边缘情况
    bool changed = true;
    int maxIterations = 100;  // 防止无限循环

    while (changed && maxIterations > 0) {
        changed = false;
        --maxIterations;

        for (const auto &[name, macro] : macros) {
            std::size_t pos = 0;
            while ((pos = result.find(name, pos)) != std::string::npos) {
                // 检查是否是完整标识符（前后不能是字母数字或下划线）
                bool validStart = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(result[pos - 1])) && result[pos - 1] != '_'));
                bool validEnd = (pos + name.size() >= result.size() ||
                                 (!std::isalnum(static_cast<unsigned char>(result[pos + name.size()])) && result[pos + name.size()] != '_'));

                if (!validStart || !validEnd) {
                    pos += name.size();
                    continue;
                }

                if (macro.isFunctionLike) {
                    // 函数宏：需要解析参数
                    std::size_t afterName = pos + name.size();
                    while (afterName < result.size() && std::isspace(static_cast<unsigned char>(result[afterName]))) {
                        ++afterName;
                    }
                    if (afterName < result.size() && result[afterName] == '(') {
                        std::size_t argStart = afterName + 1;
                        std::vector<std::string> arguments;
                        int depth = 1;
                        std::string currentArg;
                        std::size_t i = argStart;
                        while (i < result.size() && depth > 0) {
                            if (result[i] == '(') {
                                ++depth;
                                currentArg += result[i];
                            } else if (result[i] == ')') {
                                --depth;
                                if (depth > 0) {
                                    currentArg += result[i];
                                }
                            } else if (result[i] == ',' && depth == 1) {
                                arguments.push_back(trim(currentArg));
                                currentArg.clear();
                            } else {
                                currentArg += result[i];
                            }
                            ++i;
                        }
                        if (!currentArg.empty() || !arguments.empty()) {
                            arguments.push_back(trim(currentArg));
                        }

                        if (depth == 0) {
                            // 对于可变参数宏，将多余的参数合并为 __VA_ARGS__
                            std::string vaArgs;
                            if (macro.isVariadic) {
                                std::size_t namedCount = macro.parameters.size();
                                if (arguments.size() > namedCount) {
                                    vaArgs = arguments[namedCount];
                                    for (std::size_t ai = namedCount + 1; ai < arguments.size(); ++ai) {
                                        vaArgs += "," + arguments[ai];
                                    }
                                    arguments.resize(namedCount);
                                }
                                // 添加 __VA_ARGS__ 作为最后一个参数
                                arguments.push_back(vaArgs);
                            }
                            std::string expanded = expandFunctionMacro(macro, arguments);
                            result.replace(pos, i - pos, expanded);
                            changed = true;
                            pos += expanded.size();
                            continue;
                        }
                    }
                    pos += name.size();
                } else {
                    // 对象宏
                    std::string expanded = expandObjectMacro(macro);
                    result.replace(pos, name.size(), expanded);
                    changed = true;
                    pos += expanded.size();
                }
            }
        }
    }

    return result;
}

std::string Preprocessor::expandObjectMacro(const Macro &macro) {
    return macro.body;
}

std::string Preprocessor::expandFunctionMacro(const Macro &macro, const std::vector<std::string> &arguments) {
    // 将宏体分词
    enum class TokKind { Id, Hash, HashHash, Space, Other };
    struct Tok { TokKind kind; std::string value; };

    // 预处理：展开 __VA_OPT__(content)
    std::string processedBody = macro.body;
    if (macro.isVariadic && !arguments.empty()) {
        const std::string &vaArgs = arguments.back();
        // 处理 __VA_OPT__(content)
        std::string vaOptTag = "__VA_OPT__";
        std::size_t optPos = 0;
        while ((optPos = processedBody.find(vaOptTag, optPos)) != std::string::npos) {
            // 检查后面是否有 (
            std::size_t parenPos = optPos + vaOptTag.size();
            while (parenPos < processedBody.size() && std::isspace(static_cast<unsigned char>(processedBody[parenPos]))) {
                ++parenPos;
            }
            if (parenPos < processedBody.size() && processedBody[parenPos] == '(') {
                // 找到匹配的 )
                int depth = 1;
                std::size_t contentStart = parenPos + 1;
                std::size_t k = contentStart;
                while (k < processedBody.size() && depth > 0) {
                    if (processedBody[k] == '(') ++depth;
                    else if (processedBody[k] == ')') { --depth; if (depth == 0) break; }
                    ++k;
                }
                if (depth == 0) {
                    std::string content = processedBody.substr(contentStart, k - contentStart);
                    // 如果 VA_ARGS 非空，展开为 content（其中 __VA_ARGS__ 替换为实际值）
                    // 否则展开为空
                    std::string replacement;
                    if (!vaArgs.empty()) {
                        replacement = content;
                        // 替换 content 中的 __VA_ARGS__
                        std::string vaTag = "__VA_ARGS__";
                        std::size_t vp = 0;
                        while ((vp = replacement.find(vaTag, vp)) != std::string::npos) {
                            replacement.replace(vp, vaTag.size(), vaArgs);
                            vp += vaArgs.size();
                        }
                    }
                    processedBody.replace(optPos, k - optPos + 1, replacement);
                    optPos += replacement.size();
                } else {
                    optPos = parenPos;
                }
            } else {
                optPos += vaOptTag.size();
            }
        }
    }

    std::vector<Tok> tokens;
    const std::string &body = processedBody;
    std::size_t pos = 0;
    while (pos < body.size()) {
        if (std::isspace(static_cast<unsigned char>(body[pos]))) {
            std::size_t start = pos;
            while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
                ++pos;
            }
            tokens.push_back({TokKind::Space, body.substr(start, pos - start)});
        } else if (body[pos] == '#') {
            if (pos + 1 < body.size() && body[pos + 1] == '#') {
                tokens.push_back({TokKind::HashHash, "##"});
                pos += 2;
            } else {
                tokens.push_back({TokKind::Hash, "#"});
                ++pos;
            }
        } else if (std::isalpha(static_cast<unsigned char>(body[pos])) || body[pos] == '_') {
            std::size_t start = pos;
            while (pos < body.size() && (std::isalnum(static_cast<unsigned char>(body[pos])) || body[pos] == '_')) {
                ++pos;
            }
            tokens.push_back({TokKind::Id, body.substr(start, pos - start)});
        } else {
            tokens.push_back({TokKind::Other, std::string(1, body[pos])});
            ++pos;
        }
    }

    // 辅助函数：参数名 -> 参数索引
    auto findParam = [&](const std::string &name) -> int {
        for (std::size_t i = 0; i < macro.parameters.size(); ++i) {
            if (macro.parameters[i] == name) {
                return static_cast<int>(i);
            }
        }
        // __VA_ARGS__ 映射到最后一个参数（可变参数部分）
        if (macro.isVariadic && name == "__VA_ARGS__" && !arguments.empty()) {
            return static_cast<int>(arguments.size() - 1);
        }
        return -1;
    };

    // 辅助函数：转义字符串用于字符串化
    auto escapeForStringify = [](const std::string &s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out;
    };

    // 第一步：处理字符串化运算符 #param
    // # 后面的参数名替换为 "arg_value"（不对参数值做宏展开）
    std::vector<Tok> pass1;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokKind::Hash) {
            // 跳过 # 和参数名之间的空白
            std::size_t j = i + 1;
            while (j < tokens.size() && tokens[j].kind == TokKind::Space) {
                ++j;
            }
            if (j < tokens.size() && tokens[j].kind == TokKind::Id) {
                int paramIdx = findParam(tokens[j].value);
                if (paramIdx >= 0 && paramIdx < static_cast<int>(arguments.size())) {
                    pass1.push_back({TokKind::Other, "\"" + escapeForStringify(arguments[paramIdx]) + "\""});
                    i = j;  // 跳过参数名
                    continue;
                }
            }
            // # 后面不是参数，保留
            pass1.push_back(tokens[i]);
        } else {
            pass1.push_back(tokens[i]);
        }
    }

    // 第二步：处理拼接运算符 token ## token
    // 左右 token 如果是参数名，使用原始参数值（不做宏展开）；否则使用 token 文本
    std::vector<Tok> pass2;
    for (std::size_t i = 0; i < pass1.size(); ++i) {
        if (pass1[i].kind == TokKind::HashHash) {
            // 跳过 ## 左边的空白 token
            while (!pass2.empty() && pass2.back().kind == TokKind::Space) {
                pass2.pop_back();
            }
            // ## 不应该出现在开头，但安全处理
            if (pass2.empty()) {
                continue;
            }

            // 跳过 ## 后面的空白
            std::size_t j = i + 1;
            while (j < pass1.size() && pass1[j].kind == TokKind::Space) {
                ++j;
            }

            if (j >= pass1.size()) {
                // ## 后面没有 token，删除 ##
                continue;
            }

            // 特殊处理 ##__VA_ARGS__：如果 VA_ARGS 为空，删除前面的逗号
            if (pass1[j].kind == TokKind::Id && pass1[j].value == "__VA_ARGS__" && macro.isVariadic) {
                int vaIdx = findParam("__VA_ARGS__");
                if (vaIdx >= 0 && vaIdx < static_cast<int>(arguments.size()) && arguments[vaIdx].empty()) {
                    // VA_ARGS 为空，删除 ## 和前面的 token（逗号）
                    pass2.pop_back();
                    i = j;  // 跳过 __VA_ARGS__
                    continue;
                }
            }

            // 获取左侧 token 的值（如果是参数名，使用参数值）
            std::string leftVal;
            if (pass2.back().kind == TokKind::Id) {
                int leftParamIdx = findParam(pass2.back().value);
                if (leftParamIdx >= 0 && leftParamIdx < static_cast<int>(arguments.size())) {
                    leftVal = arguments[leftParamIdx];
                } else {
                    leftVal = pass2.back().value;
                }
            } else {
                leftVal = pass2.back().value;
            }
            pass2.pop_back();

            // 获取右侧 token 的值
            const Tok &rightTok = pass1[j];
            std::string rightVal;
            if (rightTok.kind == TokKind::Id) {
                int paramIdx = findParam(rightTok.value);
                if (paramIdx >= 0 && paramIdx < static_cast<int>(arguments.size())) {
                    rightVal = arguments[paramIdx];
                } else {
                    rightVal = rightTok.value;
                }
            } else {
                rightVal = rightTok.value;
            }

            pass2.push_back({TokKind::Id, leftVal + rightVal});
            i = j;  // 跳过右侧 token
        } else {
            pass2.push_back(pass1[i]);
        }
    }

    // 第三步：替换剩余的参数名为参数值
    for (auto &tok : pass2) {
        if (tok.kind == TokKind::Id) {
            int paramIdx = findParam(tok.value);
            if (paramIdx >= 0 && paramIdx < static_cast<int>(arguments.size())) {
                tok.value = arguments[paramIdx];
            }
        }
    }

    // 拼接结果
    std::string result;
    for (const auto &tok : pass2) {
        result += tok.value;
    }
    return result;
}

void Preprocessor::processLineDirective(const std::string &argument, int &lineNum) {
    // 解析 #line 指令：#line number 或 #line number "filename"
    std::string trimmedArg = trim(argument);
    std::size_t pos = 0;

    // 解析行号
    while (pos < trimmedArg.size() && std::isdigit(static_cast<unsigned char>(trimmedArg[pos]))) {
        ++pos;
    }
    if (pos == 0) {
        return;  // 没有数字，忽略
    }

    const int newLine = std::stoi(trimmedArg.substr(0, pos));

    // 跳过空白
    while (pos < trimmedArg.size() && std::isspace(static_cast<unsigned char>(trimmedArg[pos]))) {
        ++pos;
    }

    // 检查是否有文件名
    if (pos < trimmedArg.size() && trimmedArg[pos] == '"') {
        ++pos;  // 跳过开头的引号
        const std::size_t endQuote = trimmedArg.find('"', pos);
        if (endQuote != std::string::npos) {
            currentFileName_ = trimmedArg.substr(pos, endQuote - pos);
        }
    }

    // 设置行号：下一行应该从 newLine 开始
    // lineNum 在循环顶部 ++lineNum 递增，所以设置为 newLine - 1
    lineNum = newLine - 1;
    currentLineNumber_ = newLine - 1;
}

fs::path Preprocessor::findHeader(const std::string &name, bool isAngleBracket, const fs::path &currentPath) {
    // 对于 "" 引号，先在当前文件所在目录查找
    if (!isAngleBracket) {
        fs::path currentDir = currentPath.parent_path();
        fs::path candidate = currentDir / name;
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    // 在 include 路径中查找
    for (const auto &includePath : includePaths) {
        fs::path candidate = includePath / name;
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    return fs::path();
}

int Preprocessor::evaluateCondition(const std::string &expression) {
    // 条件表达式求值（递归下降解析器）
    // 支持：defined()、数字常量、标识符、!、&&、||、==、!=、<、>、<=、>=、()

    // 先处理 defined()（在宏展开之前，否则 defined(X) 会在 X 展开后失效）
    std::string raw = trim(expression);
    std::string preprocessed;
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw.compare(i, 8, "defined(") == 0) {
            i += 8;
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
            std::string name;
            while (i < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[i])) || raw[i] == '_'))
                name += raw[i++];
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
            if (i < raw.size() && raw[i] == ')') ++i;
            preprocessed += isDefined(name) ? "1" : "0";
        } else if (raw.compare(i, 8, "defined ") == 0) {
            i += 8;
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
            std::string name;
            while (i < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[i])) || raw[i] == '_'))
                name += raw[i++];
            preprocessed += isDefined(name) ? "1" : "0";
        } else {
            preprocessed += raw[i];
            ++i;
        }
    }

    // 然后展开宏并解析
    std::string expr = expandMacros(preprocessed);
    std::size_t pos = 0;

    // 跳过空白
    auto skipWS = [&]() {
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) ++pos;
    };

    // 解析 primary 表达式
    std::function<int()> parsePrimary;
    std::function<int()> parseUnary;
    std::function<int()> parseMul;
    std::function<int()> parseAdd;
    std::function<int()> parseCompare;
    std::function<int()> parseEq;
    std::function<int()> parseAnd;
    std::function<int()> parseOr;

    parsePrimary = [&]() -> int {
        skipWS();
        if (pos >= expr.size()) return 0;

        // 括号
        if (expr[pos] == '(') {
            ++pos;
            int val = parseOr();
            skipWS();
            if (pos < expr.size() && expr[pos] == ')') ++pos;
            return val;
        }

        // defined(MACRO) 或 defined MACRO
        if (expr.compare(pos, 8, "defined(") == 0) {
            pos += 8;
            skipWS();
            std::string name;
            while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_'))
                name += expr[pos++];
            skipWS();
            if (pos < expr.size() && expr[pos] == ')') ++pos;
            return isDefined(name) ? 1 : 0;
        }
        if (expr.compare(pos, 8, "defined ") == 0) {
            pos += 8;
            skipWS();
            std::string name;
            while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_'))
                name += expr[pos++];
            return isDefined(name) ? 1 : 0;
        }

        // 数字（支持十六进制 0x...）
        if (std::isdigit(static_cast<unsigned char>(expr[pos])) ||
            (expr[pos] == '-' && pos + 1 < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos + 1])))) {
            bool neg = false;
            if (expr[pos] == '-') { neg = true; ++pos; }
            int val = 0;
            if (pos + 1 < expr.size() && expr[pos] == '0' && (expr[pos + 1] == 'x' || expr[pos + 1] == 'X')) {
                pos += 2;
                while (pos < expr.size() && std::isxdigit(static_cast<unsigned char>(expr[pos]))) {
                    val = val * 16 + (std::isdigit(static_cast<unsigned char>(expr[pos])) ? expr[pos] - '0' : std::tolower(expr[pos]) - 'a' + 10);
                    ++pos;
                }
            } else {
                while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
                    val = val * 10 + (expr[pos] - '0');
                    ++pos;
                }
            }
            return neg ? -val : val;
        }

        // 标识符（未定义的宏视为 0）
        if (std::isalpha(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_') {
            std::string name;
            while (pos < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_'))
                name += expr[pos++];
            return isDefined(name) ? 1 : 0;
        }

        return 0;
    };

    parseUnary = [&]() -> int {
        skipWS();
        if (pos < expr.size() && expr[pos] == '!') {
            ++pos;
            return parseUnary() == 0 ? 1 : 0;
        }
        if (pos < expr.size() && expr[pos] == '-') {
            ++pos;
            return -parseUnary();
        }
        if (pos < expr.size() && expr[pos] == '+') {
            ++pos;
            return parseUnary();
        }
        return parsePrimary();
    };

    parseMul = [&]() -> int {
        int left = parseUnary();
        skipWS();
        while (pos < expr.size()) {
            if (expr[pos] == '*') { ++pos; left *= parseUnary(); skipWS(); }
            else if (expr[pos] == '/') { ++pos; int r = parseUnary(); left = r != 0 ? left / r : 0; skipWS(); }
            else if (expr[pos] == '%') { ++pos; int r = parseUnary(); left = r != 0 ? left % r : 0; skipWS(); }
            else break;
        }
        return left;
    };

    parseAdd = [&]() -> int {
        int left = parseMul();
        skipWS();
        while (pos < expr.size()) {
            if (expr[pos] == '+') { ++pos; left += parseMul(); skipWS(); }
            else if (expr[pos] == '-' && (pos + 1 >= expr.size() || (!std::isdigit(static_cast<unsigned char>(expr[pos + 1]))))) {
                ++pos; left -= parseMul(); skipWS();
            }
            else break;
        }
        return left;
    };

    parseCompare = [&]() -> int {
        int left = parseAdd();
        skipWS();
        if (pos + 1 < expr.size() && expr[pos] == '<' && expr[pos + 1] == '=') { pos += 2; return left <= parseAdd() ? 1 : 0; }
        if (pos + 1 < expr.size() && expr[pos] == '>' && expr[pos + 1] == '=') { pos += 2; return left >= parseAdd() ? 1 : 0; }
        if (pos < expr.size() && expr[pos] == '<') { ++pos; return left < parseAdd() ? 1 : 0; }
        if (pos < expr.size() && expr[pos] == '>') { ++pos; return left > parseAdd() ? 1 : 0; }
        return left;
    };

    parseEq = [&]() -> int {
        int left = parseCompare();
        skipWS();
        if (pos + 1 < expr.size() && expr[pos] == '=' && expr[pos + 1] == '=') { pos += 2; return left == parseCompare() ? 1 : 0; }
        if (pos + 1 < expr.size() && expr[pos] == '!' && expr[pos + 1] == '=') { pos += 2; return left != parseCompare() ? 1 : 0; }
        return left;
    };

    parseAnd = [&]() -> int {
        int left = parseEq();
        skipWS();
        while (pos + 1 < expr.size() && expr[pos] == '&' && expr[pos + 1] == '&') {
            pos += 2;
            int right = parseEq();
            left = (left != 0 && right != 0) ? 1 : 0;
            skipWS();
        }
        return left;
    };

    parseOr = [&]() -> int {
        int left = parseAnd();
        skipWS();
        while (pos + 1 < expr.size() && expr[pos] == '|' && expr[pos + 1] == '|') {
            pos += 2;
            int right = parseAnd();
            left = (left != 0 || right != 0) ? 1 : 0;
            skipWS();
        }
        return left;
    };

    try {
        return parseOr();
    } catch (...) {
        return 0;
    }
}

bool Preprocessor::isDefined(const std::string &name) const {
    return macros.find(name) != macros.end();
}

std::string Preprocessor::trim(const std::string &str) {
    std::size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    std::size_t end = str.size();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    return str.substr(start, end - start);
}

std::vector<std::string> Preprocessor::splitArguments(const std::string &str) {
    std::vector<std::string> result;
    std::string current;
    int depth = 0;

    for (char c : str) {
        if (c == '(') {
            ++depth;
            current += c;
        } else if (c == ')') {
            --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        result.push_back(trim(current));
    }

    return result;
}
