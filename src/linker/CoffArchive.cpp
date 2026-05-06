#include "CoffArchive.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {
constexpr char ArchiveMagic[] = "!<arch>\n";
constexpr std::size_t MagicLen = 8;
constexpr std::size_t HeaderSize = 60;  // 每个成员头 60 字节
constexpr char EndMarker[] = "`\n";

// 从成员头中解析名称
std::string parseName(const std::uint8_t *header) {
    // 名称字段在偏移 0，长度 16 字节，以 '/' 结尾
    std::string name(reinterpret_cast<const char *>(header), 16);
    auto end = name.find('/');
    if (end != std::string::npos) {
        name = name.substr(0, end);
    }
    // 去除尾部空格
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name;
}

// 从成员头中解析大小
std::uint32_t parseSize(const std::uint8_t *header) {
    // 大小字段在偏移 48，长度 10 字节
    std::string sizeStr(reinterpret_cast<const char *>(header + 48), 10);
    auto end = sizeStr.find(' ');
    if (end != std::string::npos) {
        sizeStr = sizeStr.substr(0, end);
    }
    return static_cast<std::uint32_t>(std::stoul(sizeStr));
}

// 读取 2 字节小端
std::uint16_t readU16(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
}

// 读取 4 字节小端
std::uint32_t readU32(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}
}  // namespace

std::vector<CoffArchive::Member> CoffArchive::read(const fs::path &libPath) {
    std::ifstream file(libPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open library file: " + libPath.string());
    }

    // 读取整个文件
    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char *>(data.data()), fileSize);

    return parseArchive(data);
}

std::vector<CoffArchive::Member> CoffArchive::parseArchive(const std::vector<std::uint8_t> &data) {
    if (data.size() < MagicLen) {
        throw std::runtime_error("not a COFF archive file (too small)");
    }
    if (std::memcmp(data.data(), ArchiveMagic, MagicLen) != 0) {
        throw std::runtime_error("not a COFF archive file (bad magic)");
    }

    std::vector<Member> members;
    std::size_t pos = MagicLen;

    while (pos + HeaderSize <= data.size()) {
        const std::uint8_t *header = data.data() + pos;

        // 检查结束标记
        if (header[0] == '`' && header[1] == '\n') {
            break;
        }

        std::string name = parseName(header);
        std::uint32_t size = parseSize(header);
        std::size_t dataOffset = pos + HeaderSize;

        if (dataOffset + size > data.size()) {
            break;  // 无效成员，停止解析
        }

        // 跳过特殊成员（符号索引、长名称等）
        bool isSpecial = (name == "/" || name == "//" || name.empty());

        if (!isSpecial) {
            Member member;
            member.name = name;
            member.data.assign(data.data() + dataOffset, data.data() + dataOffset + size);
            members.push_back(std::move(member));
        }

        // 移动到下一个成员（2 字节对齐）
        pos = dataOffset + size;
        if (pos % 2 != 0) {
            ++pos;
        }
    }

    return members;
}

std::vector<std::string> CoffArchive::parseSymbolIndex(const std::vector<std::uint8_t> &data, std::size_t offset) {
    std::vector<std::string> symbols;
    if (offset + 4 > data.size()) return symbols;

    std::uint32_t count = readU32(data.data() + offset);
    offset += 4;

    // 偏移表（每个 4 字节）
    std::vector<std::uint32_t> offsets(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (offset + 4 > data.size()) return symbols;
        offsets[i] = readU32(data.data() + offset);
        offset += 4;
    }

    // 符号名称（以 null 结尾的字符串）
    for (std::uint32_t i = 0; i < count; ++i) {
        if (offset >= data.size()) break;
        const char *start = reinterpret_cast<const char *>(data.data() + offset);
        const char *end = start;
        while (end < reinterpret_cast<const char *>(data.data() + data.size()) && *end != '\0') {
            ++end;
        }
        symbols.emplace_back(start, static_cast<std::size_t>(end - start));
        offset = static_cast<std::size_t>(end - reinterpret_cast<const char *>(data.data())) + 1;
    }

    return symbols;
}

std::vector<CoffArchive::Member> CoffArchive::extractNeeded(
    const fs::path &libPath,
    const std::unordered_map<std::string, bool> &undefinedSymbols) {
    if (undefinedSymbols.empty()) {
        return {};
    }

    // 读取整个文件
    std::ifstream file(libPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open library file: " + libPath.string());
    }

    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char *>(data.data()), fileSize);

    if (data.size() < MagicLen || std::memcmp(data.data(), ArchiveMagic, MagicLen) != 0) {
        throw std::runtime_error("not a COFF archive file: " + libPath.string());
    }

    // 查找第一个成员（符号索引）"/" 或 "//"
    // 第一个成员通常是 "/" - 第一链接器成员，包含符号索引
    std::size_t pos = MagicLen;
    if (pos + HeaderSize > data.size()) return {};

    const std::uint8_t *firstHeader = data.data() + pos;
    std::string firstName = parseName(firstHeader);
    std::uint32_t firstSize = parseSize(firstHeader);
    std::size_t firstDataOffset = pos + HeaderSize;

    // 解析符号索引
    std::vector<std::string> symbolNames;
    std::vector<std::uint32_t> memberOffsets;

    if (firstName == "/" && firstDataOffset + firstSize <= data.size()) {
        // 第一链接器成员格式：
        // 4 bytes: 符号数量
        // N * 4 bytes: 成员偏移
        // null-terminated strings: 符号名
        std::size_t idx = firstDataOffset;
        if (idx + 4 <= firstDataOffset + firstSize) {
            std::uint32_t symCount = readU32(data.data() + idx);
            idx += 4;

            memberOffsets.resize(symCount);
            for (std::uint32_t i = 0; i < symCount; ++i) {
                if (idx + 4 > firstDataOffset + firstSize) break;
                memberOffsets[i] = readU32(data.data() + idx);
                idx += 4;
            }

            for (std::uint32_t i = 0; i < symCount; ++i) {
                if (idx >= firstDataOffset + firstSize) break;
                const char *start = reinterpret_cast<const char *>(data.data() + idx);
                const char *end = start;
                while (end < reinterpret_cast<const char *>(data.data() + firstDataOffset + firstSize) && *end != '\0') {
                    ++end;
                }
                symbolNames.emplace_back(start, static_cast<std::size_t>(end - start));
                idx = static_cast<std::size_t>(end - reinterpret_cast<const char *>(data.data())) + 1;
            }
        }
    }

    // 找出需要提取的成员偏移
    std::unordered_map<std::uint32_t, bool> neededOffsets;
    for (std::size_t i = 0; i < symbolNames.size() && i < memberOffsets.size(); ++i) {
        if (undefinedSymbols.count(symbolNames[i])) {
            neededOffsets[memberOffsets[i]] = true;
        }
    }

    if (neededOffsets.empty()) {
        return {};
    }

    // 解析归档并提取需要的成员
    std::vector<Member> result;
    pos = MagicLen;

    while (pos + HeaderSize <= data.size()) {
        const std::uint8_t *header = data.data() + pos;
        if (header[0] == '`' && header[1] == '\n') break;

        std::string name = parseName(header);
        std::uint32_t size = parseSize(header);
        std::size_t dataOffset = pos + HeaderSize;

        if (dataOffset + size > data.size()) break;

        bool isSpecial = (name == "/" || name == "//" || name.empty());

        if (!isSpecial && neededOffsets.count(static_cast<std::uint32_t>(pos))) {
            Member member;
            member.name = name;
            member.data.assign(data.data() + dataOffset, data.data() + dataOffset + size);
            result.push_back(std::move(member));
        }

        pos = dataOffset + size;
        if (pos % 2 != 0) ++pos;
    }

    return result;
}

namespace {
// 从 COFF 对象中提取导出符号名
std::vector<std::string> extractExportedSymbols(const std::vector<std::uint8_t> &objData) {
    std::vector<std::string> symbols;
    if (objData.size() < 20) return symbols;

    std::uint32_t symbolTableOffset = readU32(objData.data() + 8);
    std::uint32_t symbolCount = readU32(objData.data() + 12);
    std::size_t stringTableOffset = symbolTableOffset + static_cast<std::size_t>(symbolCount) * 18;

    std::uint32_t index = 0;
    while (index < symbolCount) {
        std::size_t offset = symbolTableOffset + static_cast<std::size_t>(index) * 18;
        if (offset + 18 > objData.size()) break;

        std::int16_t sectionNumber = static_cast<std::int16_t>(readU16(objData.data() + offset + 12));
        std::uint8_t storageClass = objData[offset + 16];

        // 只导出外部定义符号（sectionNumber > 0 表示已定义）
        if (storageClass == 2 && sectionNumber > 0) {
            std::string name;
            if (readU32(objData.data() + offset) == 0) {
                // 使用字符串表
                std::uint32_t strOffset = readU32(objData.data() + offset + 4);
                if (stringTableOffset + strOffset < objData.size()) {
                    const char *start = reinterpret_cast<const char *>(objData.data() + stringTableOffset + strOffset);
                    const char *end = start;
                    while (end < reinterpret_cast<const char *>(objData.data() + objData.size()) && *end != '\0') {
                        ++end;
                    }
                    name.assign(start, static_cast<std::size_t>(end - start));
                }
            } else {
                // 内联名称（8 字节）
                const char *start = reinterpret_cast<const char *>(objData.data() + offset);
                const char *end = start;
                while (end < start + 8 && *end != '\0') {
                    ++end;
                }
                name.assign(start, static_cast<std::size_t>(end - start));
            }
            if (!name.empty()) {
                symbols.push_back(name);
            }
        }

        std::uint8_t auxCount = objData[offset + 17];
        index += 1 + auxCount;
    }

    return symbols;
}

void writeU32LE(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void appendString(std::vector<std::uint8_t> &out, const std::string &s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back('\0');
}

std::string formatArchiveHeader(const std::string &name, std::uint32_t size) {
    // 60 字节的成员头：名称(16) + 日期(12) + uid(6) + gid(6) + mode(8) + 大小(10) + 结尾(2)
    std::string header(60, ' ');
    // 名称
    std::string nameField = name + "/";
    if (nameField.size() > 16) nameField = nameField.substr(0, 16);
    std::copy(nameField.begin(), nameField.end(), header.begin());
    // 日期
    std::string dateStr = "0";
    std::copy(dateStr.begin(), dateStr.end(), header.begin() + 16);
    // uid/gid
    header[28] = '0';
    header[34] = '0';
    // mode
    header[40] = '0';
    // 大小
    std::string sizeStr = std::to_string(size);
    std::copy(sizeStr.begin(), sizeStr.end(), header.begin() + 48);
    // 结尾签名
    header[58] = '`';
    header[59] = '\n';
    return header;
}
}  // namespace

void CoffArchive::write(
    const fs::path &libPath,
    const std::vector<fs::path> &objPaths) {
    // 读取所有 .obj 文件
    struct ObjEntry {
        std::string name;
        std::vector<std::uint8_t> data;
        std::vector<std::string> symbols;
    };

    std::vector<ObjEntry> entries;
    for (const auto &objPath : objPaths) {
        std::ifstream in(objPath, std::ios::binary);
        if (!in) {
            throw std::runtime_error("cannot open object file: " + objPath.string());
        }
        ObjEntry entry;
        entry.name = objPath.filename().string();
        entry.data = {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        entry.symbols = extractExportedSymbols(entry.data);
        entries.push_back(std::move(entry));
    }

    // 构建归档
    std::vector<std::uint8_t> archive;

    // 1. 魔术签名
    appendString(archive, "!<arch>\n");

    // 2. 第一链接器成员 "/" - 符号索引
    // 统计所有符号
    std::vector<std::pair<std::string, std::uint32_t>> symbolIndex;  // (name, memberOffset)
    // 计算每个成员的偏移（跳过 "/" 和 "//" 成员）
    // 先计算 "/" 成员的大小
    std::uint32_t symbolCount = 0;
    for (const auto &entry : entries) {
        symbolCount += static_cast<std::uint32_t>(entry.symbols.size());
    }

    // "/" 成员数据：4字节符号数 + N*4字节偏移 + null-terminated字符串
    std::vector<std::uint8_t> linkerMemberData;
    writeU32LE(linkerMemberData, symbolCount);
    // 偏移表占位（稍后填充）
    std::size_t offsetTableStart = linkerMemberData.size();
    linkerMemberData.resize(offsetTableStart + symbolCount * 4);
    // 符号名
    for (const auto &entry : entries) {
        for (const auto &sym : entry.symbols) {
            appendString(linkerMemberData, sym);
        }
    }

    // "/" 成员头
    std::string linkerHeader = formatArchiveHeader("/", static_cast<std::uint32_t>(linkerMemberData.size()));
    archive.insert(archive.end(), linkerHeader.begin(), linkerHeader.end());
    std::size_t linkerDataStart = archive.size();
    archive.insert(archive.end(), linkerMemberData.begin(), linkerMemberData.end());
    if (archive.size() % 2 != 0) archive.push_back('\n');

    // 3. 对象文件成员
    // 先计算每个成员的偏移，然后回填符号索引
    std::size_t firstMemberOffset = archive.size();
    std::vector<std::size_t> memberOffsets;
    std::size_t currentOffset = firstMemberOffset;
    for (const auto &entry : entries) {
        memberOffsets.push_back(currentOffset);
        currentOffset += HeaderSize + entry.data.size();
        if ((HeaderSize + entry.data.size()) % 2 != 0) ++currentOffset;
    }

    // 回填符号索引中的偏移
    std::size_t symIdx = 0;
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
        for (std::size_t si = 0; si < entries[ei].symbols.size(); ++si) {
            std::size_t pos = offsetTableStart + symIdx * 4;
            std::uint32_t off = static_cast<std::uint32_t>(memberOffsets[ei]);
            linkerMemberData[pos] = static_cast<std::uint8_t>(off);
            linkerMemberData[pos + 1] = static_cast<std::uint8_t>(off >> 8);
            linkerMemberData[pos + 2] = static_cast<std::uint8_t>(off >> 16);
            linkerMemberData[pos + 3] = static_cast<std::uint8_t>(off >> 24);
            ++symIdx;
        }
    }

    // 重新构建 "/" 成员（因为偏移已更新）
    // 清除旧的 "/" 成员数据
    archive.erase(archive.begin() + MagicLen, archive.begin() + static_cast<std::ptrdiff_t>(linkerDataStart + linkerMemberData.size()));
    // 重新写入
    std::vector<std::uint8_t> newArchive;
    appendString(newArchive, "!<arch>\n");

    // 重新构建 linkerMemberData
    linkerMemberData.clear();
    writeU32LE(linkerMemberData, symbolCount);
    linkerMemberData.resize(4 + symbolCount * 4);
    symIdx = 0;
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
        for (std::size_t si = 0; si < entries[ei].symbols.size(); ++si) {
            std::size_t pos = 4 + symIdx * 4;
            std::uint32_t off = static_cast<std::uint32_t>(memberOffsets[ei]);
            linkerMemberData[pos] = static_cast<std::uint8_t>(off);
            linkerMemberData[pos + 1] = static_cast<std::uint8_t>(off >> 8);
            linkerMemberData[pos + 2] = static_cast<std::uint8_t>(off >> 16);
            linkerMemberData[pos + 3] = static_cast<std::uint8_t>(off >> 24);
            ++symIdx;
        }
    }
    for (const auto &entry : entries) {
        for (const auto &sym : entry.symbols) {
            appendString(linkerMemberData, sym);
        }
    }

    std::string newLinkerHeader = formatArchiveHeader("/", static_cast<std::uint32_t>(linkerMemberData.size()));
    newArchive.insert(newArchive.end(), newLinkerHeader.begin(), newLinkerHeader.end());
    newArchive.insert(newArchive.end(), linkerMemberData.begin(), linkerMemberData.end());
    if (newArchive.size() % 2 != 0) newArchive.push_back('\n');

    // 写入对象文件成员
    for (const auto &entry : entries) {
        std::string memberHeader = formatArchiveHeader(entry.name, static_cast<std::uint32_t>(entry.data.size()));
        newArchive.insert(newArchive.end(), memberHeader.begin(), memberHeader.end());
        newArchive.insert(newArchive.end(), entry.data.begin(), entry.data.end());
        if (newArchive.size() % 2 != 0) newArchive.push_back('\n');
    }

    // 写入文件
    std::ofstream out(libPath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot create library file: " + libPath.string());
    }
    out.write(reinterpret_cast<const char *>(newArchive.data()), static_cast<std::streamsize>(newArchive.size()));
}
