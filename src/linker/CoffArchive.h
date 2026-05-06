#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// COFF 归档文件（.lib）读取器
// 格式：!<arch>\n + 成员表 + 符号索引 + 对象文件
class CoffArchive {
public:
    struct Member {
        std::string name;
        std::vector<std::uint8_t> data;  // 原始 COFF 对象数据
    };

    // 读取 .lib 文件，返回所有对象成员
    static std::vector<Member> read(const std::filesystem::path &libPath);

    // 从 .lib 中提取满足未定义符号的对象文件
    static std::vector<Member> extractNeeded(
        const std::filesystem::path &libPath,
        const std::unordered_map<std::string, bool> &undefinedSymbols);

    // 创建 .lib 文件（从 .obj 文件列表）
    static void write(
        const std::filesystem::path &libPath,
        const std::vector<std::filesystem::path> &objPaths);

private:
    static std::vector<Member> parseArchive(const std::vector<std::uint8_t> &data);
    static std::vector<std::string> parseSymbolIndex(const std::vector<std::uint8_t> &data, std::size_t offset);
};
