#include "PeLinker.h"

#include "CoffArchive.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::uint16_t MachineAmd64 = 0x8664;
constexpr std::uint16_t RelAmd64Addr64 = 0x0001;
constexpr std::uint16_t RelAmd64Addr32Nb = 0x0003;
constexpr std::uint16_t RelAmd64Rel32 = 0x0004;
constexpr std::uint16_t RelAmd64Section = 0x000A;
constexpr std::uint16_t RelAmd64Secrel = 0x000B;
constexpr std::uint16_t ImageRelBasedDir64 = 10;
constexpr std::int16_t UndefinedSection = 0;
constexpr std::uint8_t StorageClassExternal = 2;
constexpr std::uint32_t SectionContainsUninitializedData = 0x00000080;
constexpr std::uint32_t ImageScnCntInitializedData = 0x00000040;
constexpr std::uint32_t ImageScnMemRead = 0x40000000;
constexpr std::uint32_t ImageScnMemWrite = 0x80000000;

// PE 数据目录索引
constexpr int DataDirExportTable = 0;
constexpr int DataDirImportTable = 1;
constexpr int DataDirResourceTable = 2;
constexpr int DataDirExceptionTable = 3;
constexpr int DataDirCertificateTable = 4;
constexpr int DataDirBaseRelocationTable = 5;
constexpr int DataDirDebug = 6;
constexpr int DataDirArchitecture = 7;
constexpr int DataDirGlobalPtr = 8;
constexpr int DataDirTlsTable = 9;
constexpr int DataDirLoadConfigTable = 10;
constexpr int DataDirBoundImport = 11;
constexpr int DataDirIat = 12;
constexpr int DataDirDelayImportDescriptor = 13;
constexpr int DataDirClrRuntimeHeader = 14;
constexpr int DataDirReserved = 15;

constexpr std::uint32_t FileAlignment = 0x200;
constexpr std::uint32_t SectionAlignment = 0x1000;
constexpr std::uint64_t ImageBase = 0x140000000;
constexpr std::uint32_t FirstSectionRva = 0x1000;

struct Section {
    std::string name;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t rawPointer = 0;
    std::uint32_t relocationPointer = 0;
    std::uint16_t relocationCount = 0;
    std::uint32_t characteristics = 0;
};

struct Symbol {
    std::string name;
    std::uint32_t value = 0;
    std::int16_t sectionNumber = 0;
    std::uint8_t storageClass = 0;
};

struct Relocation {
    std::uint32_t offset = 0;
    std::uint32_t symbolIndex = 0;
    std::uint16_t type = 0;
};

struct ObjectFile {
    fs::path path;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<std::uint8_t> bytes;
};

struct LinkedObject {
    ObjectFile object;
    std::vector<std::uint32_t> sectionOffsets;
};

struct SectionLayout {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::uint32_t virtualSize = 0;
    std::uint32_t rva = 0;
    std::uint32_t rawPointer = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
    bool isUninitializedData = false;
};

struct ImportLayout {
    std::uint32_t idtRva = 0;
    std::uint32_t idtSize = 0;
    std::vector<std::uint8_t> bytes;
    struct Symbol {
        std::string symbolName;
        std::string importName;
        std::string dllName;
        std::string sourceName;
        std::uint32_t thunkRva = 0;
        std::uint32_t iatRva = 0;
    };
    struct Group {
        std::string dllName;
        std::vector<Symbol> symbols;
    };
    std::vector<Group> groups;
};

struct BaseRelocationSite {
    std::string sectionName;
    std::uint32_t slotRva = 0;
};

struct BaseRelocationLayout {
    std::uint32_t rva = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> pageRvas;
    std::vector<std::uint16_t> entryCounts;
    std::size_t siteCount = 0;
};

// TLS（线程本地存储）布局
struct TlsLayout {
    std::uint32_t directoryRva = 0;        // IMAGE_TLS_DIRECTORY 的 RVA
    std::uint32_t directorySize = 0;       // IMAGE_TLS_DIRECTORY 的大小
    std::uint32_t rawDataRva = 0;          // TLS 原始数据的 RVA
    std::uint32_t rawDataSize = 0;         // TLS 原始数据的大小
    std::uint32_t indexRva = 0;            // TLS 索引变量的 RVA
    std::uint32_t callbacksRva = 0;        // TLS 回调数组的 RVA
    std::uint32_t zeroFillSize = 0;        // 零填充大小
    std::vector<std::uint8_t> rawData;     // TLS 原始数据内容
    std::vector<std::uint64_t> callbacks;  // TLS 回调函数 RVA 列表
    std::vector<std::uint8_t> bytes;       // 完整的 TLS 目录二进制数据
};

// 延迟加载导入描述符
struct DelayLoadDescriptor {
    std::uint32_t attributes = 0;                    // 属性（1 = RvaBased）
    std::uint32_t dllNameRva = 0;                    // DLL 名称的 RVA
    std::uint32_t moduleHandleRva = 0;               // 模块句柄的 RVA
    std::uint32_t importAddressTableRva = 0;         // 延迟导入地址表 RVA
    std::uint32_t importNameTableRva = 0;            // 延迟导入名称表 RVA
    std::uint32_t boundImportAddressTableRva = 0;    // 绑定导入地址表 RVA
    std::uint32_t unloadInformationTableRva = 0;     // 卸载信息表 RVA
    std::uint32_t timeDateStamp = 0;                 // 时间戳
};

struct DelayImportLayout {
    std::uint32_t rva = 0;                           // 延迟导入表的 RVA
    std::uint32_t size = 0;                          // 延迟导入表的大小
    std::vector<DelayLoadDescriptor> descriptors;    // 描述符列表
    struct Symbol {
        std::string symbolName;
        std::string importName;
        std::string dllName;
        std::string sourceName;
        std::uint32_t thunkRva = 0;                  // thunk 代码的 RVA
        std::uint32_t iatRva = 0;                    // IAT 条目的 RVA
    };
    struct Group {
        std::string dllName;
        std::vector<Symbol> symbols;
    };
    std::vector<Group> groups;
    std::vector<std::uint8_t> bytes;                 // 完整的延迟导入二进制数据
};

// PE 导出表布局
struct ExportLayout {
    std::uint32_t rva = 0;
    std::uint32_t size = 0;
    std::vector<std::uint8_t> bytes;
    struct Entry {
        std::string name;
        std::uint32_t functionRva = 0;
    };
    std::vector<Entry> entries;
};

struct ResolvedSymbolTraceEntry {
    fs::path objectPath;
    std::string sectionName;
    std::string name;
    std::string importDllName;
    std::string importSourceName;
    std::uint32_t rva = 0;
    std::uint32_t importIatRva = 0;
    bool imported = false;
};

struct ImportSpec {
    std::string symbolName;
    std::string importName;
    std::string dllName;
    std::string sourceName;
};

struct ResolvedImportEntry {
    std::string symbolName;
    std::string importName;
    std::string dllName;
    std::string sourceName;
};

struct RelocationTraceEntry {
    fs::path objectPath;
    std::string sourceSectionName;
    std::uint32_t offset = 0;
    std::string typeName;
    std::string targetName;
    std::uint32_t targetRva = 0;
    std::int64_t addend = 0;
    std::int32_t relative = 0;
    std::uint64_t storedValue = 0;
    bool hasStoredValue = false;
};

std::uint32_t alignTo(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

unsigned int objectReadWorkerCount(std::size_t taskCount, unsigned int requestedJobs) {
    if (taskCount <= 1) {
        return 1;
    }

    const unsigned int detected = std::thread::hardware_concurrency();
    const unsigned int fallback = 4;
    const unsigned int available = requestedJobs == 0 ? (detected == 0 ? fallback : detected) : requestedJobs;
    return static_cast<unsigned int>(std::min<std::size_t>(taskCount, available));
}

std::string linkerDiagnostic(const std::string &detail);

bool isRecognizedSection(const std::string &name) {
    return name == ".text" || name == ".data" || name == ".rdata" || name == ".bss" || name == ".tls"
        || name == ".debug_str" || name == ".debug_abbrev" || name == ".debug_line" || name == ".debug_info"
        || name == ".pdata" || name == ".xdata";
}

bool isTraceableSymbol(const Symbol &symbol) {
    return !symbol.name.empty() && symbol.name[0] != '.' && symbol.storageClass == StorageClassExternal;
}

const std::vector<ImportSpec> &builtinImportCatalog() {
    static const std::vector<ImportSpec> imports = {
        {"ExitProcess", "ExitProcess", "kernel32.dll", "builtin"},
        {"fn_GetCurrentProcessId", "GetCurrentProcessId", "kernel32.dll", "builtin"},
        {"fn_puts", "puts", "msvcrt.dll", "builtin"},
        {"fn_putchar", "putchar", "msvcrt.dll", "builtin"},
        {"fn_printf", "printf", "msvcrt.dll", "builtin"},
        {"fmod", "fmod", "msvcrt.dll", "builtin"},
    };
    return imports;
}

fs::path importCatalogPath() {
    if (const char *overridePath = std::getenv("MINIC_IMPORT_CATALOG")) {
        if (overridePath[0] != '\0') {
            return fs::path(overridePath);
        }
    }
    return fs::current_path() / "config" / "import_catalog.txt";
}

std::string trimCopy(const std::string &value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::vector<ImportSpec> loadFileImportCatalog() {
    const fs::path catalogPath = importCatalogPath();
    if (!fs::exists(catalogPath)) {
        return {};
    }

    std::ifstream input(catalogPath);
    if (!input) {
        throw std::runtime_error(linkerDiagnostic("failed to open import catalog: " + catalogPath.string()));
    }

    std::vector<ImportSpec> imports;
    std::unordered_map<std::string, std::size_t> seenLines;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string trimmed = trimCopy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t separator = trimmed.find('|', start);
            if (separator == std::string::npos) {
                fields.push_back(trimCopy(trimmed.substr(start)));
                break;
            }
            fields.push_back(trimCopy(trimmed.substr(start, separator - start)));
            start = separator + 1;
        }

        if (fields.size() != 3 || fields[0].empty() || fields[1].empty() || fields[2].empty()) {
            throw std::runtime_error(
                linkerDiagnostic(
                    "invalid import catalog row " + catalogPath.string() + ":" + std::to_string(lineNumber) +
                    "; expected symbol|dll|import"));
        }

        const auto [it, inserted] = seenLines.emplace(fields[0], lineNumber);
        if (!inserted) {
            throw std::runtime_error(
                linkerDiagnostic(
                    "duplicate import catalog symbol " + fields[0] + " in " + catalogPath.string() +
                    " at lines " + std::to_string(it->second) + " and " + std::to_string(lineNumber)));
        }

        imports.push_back(ImportSpec{fields[0], fields[2], fields[1], "file"});
    }

    return imports;
}

std::vector<ImportSpec> loadImportCatalog() {
    std::vector<ImportSpec> merged = builtinImportCatalog();
    const std::vector<ImportSpec> fileCatalog = loadFileImportCatalog();
    std::unordered_map<std::string, bool> builtinNames;
    for (const auto &entry : builtinImportCatalog()) {
        builtinNames.emplace(entry.symbolName, true);
    }
    for (const auto &entry : fileCatalog) {
        if (builtinNames.find(entry.symbolName) != builtinNames.end()) {
            throw std::runtime_error(
                linkerDiagnostic(
                    "file-backed import catalog may not override built-in symbol: " + entry.symbolName));
        }
        merged.push_back(entry);
    }
    return merged;
}

const ImportSpec *resolveImportSymbol(const std::string &symbolName, const std::vector<ImportSpec> &catalog) {
    const auto found = std::find_if(
        catalog.begin(),
        catalog.end(),
        [&](const ImportSpec &spec) { return symbolName == spec.symbolName; });
    if (found == catalog.end()) {
        return nullptr;
    }
    return &*found;
}

bool isImportedSymbolName(const std::string &symbolName) {
    return resolveImportSymbol(symbolName, loadImportCatalog()) != nullptr;
}

// 延迟加载导入目录路径
fs::path delayImportCatalogPath() {
    if (const char *overridePath = std::getenv("MINIC_DELAY_IMPORT_CATALOG")) {
        if (overridePath[0] != '\0') {
            return fs::path(overridePath);
        }
    }
    return fs::current_path() / "config" / "delay_import_catalog.txt";
}

// 加载延迟加载导入目录
std::vector<ImportSpec> loadDelayImportCatalog() {
    const fs::path catalogPath = delayImportCatalogPath();
    if (!fs::exists(catalogPath)) {
        return {};
    }

    std::ifstream input(catalogPath);
    if (!input) {
        throw std::runtime_error(linkerDiagnostic("failed to open delay import catalog: " + catalogPath.string()));
    }

    std::vector<ImportSpec> imports;
    std::unordered_map<std::string, std::size_t> seenLines;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string trimmed = trimCopy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true) {
            const std::size_t separator = trimmed.find('|', start);
            if (separator == std::string::npos) {
                fields.push_back(trimCopy(trimmed.substr(start)));
                break;
            }
            fields.push_back(trimCopy(trimmed.substr(start, separator - start)));
            start = separator + 1;
        }

        if (fields.size() != 3 || fields[0].empty() || fields[1].empty() || fields[2].empty()) {
            throw std::runtime_error(
                linkerDiagnostic(
                    "invalid delay import catalog row " + catalogPath.string() + ":" + std::to_string(lineNumber) +
                    "; expected symbol|dll|import"));
        }

        const auto [it, inserted] = seenLines.emplace(fields[0], lineNumber);
        if (!inserted) {
            throw std::runtime_error(
                linkerDiagnostic(
                    "duplicate delay import catalog symbol " + fields[0] + " in " + catalogPath.string() +
                    " at lines " + std::to_string(it->second) + " and " + std::to_string(lineNumber)));
        }

        imports.push_back(ImportSpec{fields[0], fields[2], fields[1], "delay_file"});
    }

    return imports;
}

// 收集延迟加载导入
std::vector<ResolvedImportEntry> collectDelayImports(
    const std::vector<LinkedObject> &linkedObjects,
    const std::unordered_map<std::string, bool> &definedSymbolNames,
    const std::vector<ImportSpec> &delayCatalog) {
    std::unordered_map<std::string, bool> unresolvedExternals;
    for (const auto &linkedObject : linkedObjects) {
        for (const auto &symbol : linkedObject.object.symbols) {
            if (!isTraceableSymbol(symbol) || symbol.sectionNumber != UndefinedSection) {
                continue;
            }
            if (definedSymbolNames.find(symbol.name) != definedSymbolNames.end()) {
                continue;
            }
            unresolvedExternals.emplace(symbol.name, true);
        }
    }

    std::vector<ResolvedImportEntry> imports;
    for (const auto &[symbolName, _] : unresolvedExternals) {
        const ImportSpec *resolved = resolveImportSymbol(symbolName, delayCatalog);
        if (resolved == nullptr) {
            continue;
        }
        imports.push_back(
            ResolvedImportEntry{
                resolved->symbolName,
                resolved->importName,
                resolved->dllName,
                resolved->sourceName});
    }
    std::sort(
        imports.begin(),
        imports.end(),
        [](const ResolvedImportEntry &left, const ResolvedImportEntry &right) {
            if (left.dllName != right.dllName) {
                return left.dllName < right.dllName;
            }
            return left.symbolName < right.symbolName;
        });
    return imports;
}

// 分组延迟加载导入
std::vector<DelayImportLayout::Group> groupDelayImports(const std::vector<ResolvedImportEntry> &resolvedImports) {
    std::vector<DelayImportLayout::Group> groups;
    for (const auto &entry : resolvedImports) {
        auto group = std::find_if(
            groups.begin(),
            groups.end(),
            [&](const DelayImportLayout::Group &candidate) { return candidate.dllName == entry.dllName; });
        if (group == groups.end()) {
            groups.push_back(DelayImportLayout::Group{entry.dllName, {}});
            group = std::prev(groups.end());
        }
        group->symbols.push_back(
            DelayImportLayout::Symbol{
                entry.symbolName,
                entry.importName,
                entry.dllName,
                entry.sourceName,
                0,
                0});
    }
    return groups;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

std::string linkerDiagnostic(const std::string &detail) {
    return "linker diagnostic: " + detail;
}

std::string objectDiagnostic(const fs::path &path, const std::string &detail) {
    return linkerDiagnostic(path.string() + ": " + detail);
}

std::string countLabel(std::size_t count, const char *singular, const char *plural) {
    std::ostringstream out;
    out << count << " " << (count == 1 ? singular : plural);
    return out.str();
}

std::string targetDescription(const Symbol &symbol, std::int32_t addend) {
    if (symbol.sectionNumber == UndefinedSection) {
        return "import " + symbol.name;
    }
    if (addend == 0) {
        return symbol.name;
    }
    if (addend > 0) {
        return symbol.name + " + " + std::to_string(addend);
    }
    return symbol.name + " - " + std::to_string(-addend);
}

std::string targetDescription64(const Symbol &symbol, std::int64_t addend) {
    if (symbol.sectionNumber == UndefinedSection) {
        return "import " + symbol.name;
    }
    if (addend == 0) {
        return symbol.name;
    }
    if (addend > 0) {
        return symbol.name + " + " + std::to_string(addend);
    }
    return symbol.name + " - " + std::to_string(-addend);
}

std::uint16_t read16(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("truncated binary while reading u16");
    }
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t read32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("truncated binary while reading u32");
    }
    return static_cast<std::uint32_t>(
        bytes[offset] |
        (bytes[offset + 1] << 8) |
        (bytes[offset + 2] << 16) |
        (bytes[offset + 3] << 24));
}

std::int16_t readS16(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::int16_t>(read16(bytes, offset));
}

std::uint64_t read64(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("truncated binary while reading u64");
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    }
    return value;
}

std::int32_t readS32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::int32_t>(read32(bytes, offset));
}

std::int64_t readS64(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::int64_t>(read64(bytes, offset));
}

void write16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void write32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

void write64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xff);
    }
}

void append16(std::vector<std::uint8_t> &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void append64(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

void appendString(std::vector<std::uint8_t> &bytes, const std::string &text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
}

void patch32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("internal linker error: patch outside buffer");
    }
    write32(bytes, offset, value);
}

void patch16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("internal linker error: patch outside buffer");
    }
    bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void patch64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("internal linker error: patch outside buffer");
    }
    write64(bytes, offset, value);
}

std::string readFixedName(const std::vector<std::uint8_t> &bytes, std::size_t offset, std::size_t size) {
    std::string result;
    for (std::size_t i = 0; i < size && offset + i < bytes.size(); ++i) {
        if (bytes[offset + i] == 0) {
            break;
        }
        result.push_back(static_cast<char>(bytes[offset + i]));
    }
    return result;
}

std::string readStringTableName(
    const std::vector<std::uint8_t> &bytes,
    std::size_t stringTableOffset,
    std::uint32_t nameOffset) {
    const std::size_t offset = stringTableOffset + nameOffset;
    if (offset >= bytes.size()) {
        throw std::runtime_error("invalid COFF string table offset");
    }

    std::string result;
    for (std::size_t i = offset; i < bytes.size() && bytes[i] != 0; ++i) {
        result.push_back(static_cast<char>(bytes[i]));
    }
    return result;
}

std::string readSymbolName(
    const std::vector<std::uint8_t> &bytes,
    std::size_t symbolOffset,
    std::size_t stringTableOffset) {
    if (read32(bytes, symbolOffset) == 0) {
        return readStringTableName(bytes, stringTableOffset, read32(bytes, symbolOffset + 4));
    }
    return readFixedName(bytes, symbolOffset, 8);
}

ObjectFile readObjectFromBytes(std::vector<std::uint8_t> bytes, const fs::path &path) {
    ObjectFile object;
    object.path = path;
    object.bytes = std::move(bytes);
    if (object.bytes.size() < 20) {
        throw std::runtime_error(objectDiagnostic(path, "object file is too small"));
    }

    const std::uint16_t machine = read16(object.bytes, 0);
    if (machine != MachineAmd64) {
        throw std::runtime_error(objectDiagnostic(path, "only AMD64 COFF objects are supported"));
    }

    const std::uint16_t sectionCount = read16(object.bytes, 2);
    const std::uint32_t symbolTableOffset = read32(object.bytes, 8);
    const std::uint32_t symbolCount = read32(object.bytes, 12);
    const std::uint16_t optionalHeaderSize = read16(object.bytes, 16);

    const std::size_t sectionTableOffset = 20 + optionalHeaderSize;
    for (std::uint16_t i = 0; i < sectionCount; ++i) {
        const std::size_t offset = sectionTableOffset + static_cast<std::size_t>(i) * 40;
        if (offset + 40 > object.bytes.size()) {
            throw std::runtime_error(objectDiagnostic(path, "truncated COFF section table"));
        }

        Section section;
        section.name = readFixedName(object.bytes, offset, 8);
        section.virtualSize = read32(object.bytes, offset + 8);
        section.rawSize = read32(object.bytes, offset + 16);
        section.rawPointer = read32(object.bytes, offset + 20);
        section.relocationPointer = read32(object.bytes, offset + 24);
        section.relocationCount = read16(object.bytes, offset + 32);
        section.characteristics = read32(object.bytes, offset + 36);
        object.sections.push_back(section);
    }

    const std::size_t stringTableOffset = symbolTableOffset + static_cast<std::size_t>(symbolCount) * 18;
    std::uint32_t index = 0;
    while (index < symbolCount) {
        const std::size_t offset = symbolTableOffset + static_cast<std::size_t>(index) * 18;
        if (offset + 18 > object.bytes.size()) {
            throw std::runtime_error(objectDiagnostic(path, "truncated COFF symbol table"));
        }

        Symbol symbol;
        symbol.name = readSymbolName(object.bytes, offset, stringTableOffset);
        symbol.value = read32(object.bytes, offset + 8);
        symbol.sectionNumber = readS16(object.bytes, offset + 12);
        symbol.storageClass = object.bytes[offset + 16];
        object.symbols.push_back(symbol);

        const std::uint8_t auxCount = object.bytes[offset + 17];
        for (std::uint8_t i = 0; i < auxCount; ++i) {
            object.symbols.push_back(Symbol{});
        }
        index += 1 + auxCount;
    }

    return object;
}

ObjectFile readObject(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(objectDiagnostic(path, "failed to open object file"));
    }

    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return readObjectFromBytes(std::move(bytes), path);
}

std::size_t findTextSectionIndex(const ObjectFile &object) {
    for (std::size_t i = 0; i < object.sections.size(); ++i) {
        if (object.sections[i].name == ".text") {
            return i;
        }
    }
    throw std::runtime_error(objectDiagnostic(object.path, "object file does not contain a .text section"));
}

std::vector<Relocation> readRelocations(const ObjectFile &object, const Section &section) {
    std::vector<Relocation> relocations;
    for (std::uint16_t i = 0; i < section.relocationCount; ++i) {
        const std::size_t offset = section.relocationPointer + static_cast<std::size_t>(i) * 10;
        if (offset + 10 > object.bytes.size()) {
            throw std::runtime_error(
                objectDiagnostic(object.path, "truncated COFF relocation table in section " + section.name));
        }

        relocations.push_back(Relocation{
            read32(object.bytes, offset),
            read32(object.bytes, offset + 4),
            read16(object.bytes, offset + 8)});
    }
    return relocations;
}

std::vector<ResolvedImportEntry> collectResolvedImports(
    const std::vector<LinkedObject> &linkedObjects,
    const std::unordered_map<std::string, bool> &definedSymbolNames,
    const std::vector<ImportSpec> &catalog) {
    std::unordered_map<std::string, bool> unresolvedExternals;
    for (const auto &linkedObject : linkedObjects) {
        for (const auto &symbol : linkedObject.object.symbols) {
            if (!isTraceableSymbol(symbol) || symbol.sectionNumber != UndefinedSection) {
                continue;
            }
            if (definedSymbolNames.find(symbol.name) != definedSymbolNames.end()) {
                continue;
            }
            unresolvedExternals.emplace(symbol.name, true);
        }
    }

    std::vector<ResolvedImportEntry> imports;
    for (const auto &[symbolName, _] : unresolvedExternals) {
        const ImportSpec *resolved = resolveImportSymbol(symbolName, catalog);
        if (resolved == nullptr) {
            continue;
        }
        imports.push_back(
            ResolvedImportEntry{
                resolved->symbolName,
                resolved->importName,
                resolved->dllName,
                resolved->sourceName});
    }
    std::sort(
        imports.begin(),
        imports.end(),
        [](const ResolvedImportEntry &left, const ResolvedImportEntry &right) {
            if (left.dllName != right.dllName) {
                return left.dllName < right.dllName;
            }
            return left.symbolName < right.symbolName;
        });
    return imports;
}

std::vector<ImportLayout::Group> groupResolvedImports(const std::vector<ResolvedImportEntry> &resolvedImports) {
    std::vector<ImportLayout::Group> groups;
    for (const auto &entry : resolvedImports) {
        auto group = std::find_if(
            groups.begin(),
            groups.end(),
            [&](const ImportLayout::Group &candidate) { return candidate.dllName == entry.dllName; });
        if (group == groups.end()) {
            groups.push_back(ImportLayout::Group{entry.dllName, {}});
            group = std::prev(groups.end());
        }
        group->symbols.push_back(
            ImportLayout::Symbol{
                entry.symbolName,
                entry.importName,
                entry.dllName,
                entry.sourceName,
                0,
                0});
    }
    return groups;
}

void appendImportThunkPlaceholder(std::vector<std::uint8_t> &text) {
    text.push_back(0xff);
    text.push_back(0x25);
    append32(text, 0);
}

void patchImportThunkDisplacement(
    std::vector<std::uint8_t> &text,
    std::uint32_t thunkOffset,
    std::uint32_t thunkRva,
    std::uint32_t iatRva) {
    const std::int64_t displacement = static_cast<std::int64_t>(iatRva) -
        static_cast<std::int64_t>(thunkRva + 6);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        throw std::runtime_error("import thunk displacement out of range");
    }
    patch32(
        text,
        thunkOffset + 2,
        static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));
}

ImportLayout buildImportLayout(
    std::uint32_t idataRva,
    std::vector<ImportLayout::Group> groups) {
    ImportLayout layout;
    layout.idtRva = idataRva;
    layout.idtSize = static_cast<std::uint32_t>((groups.size() + 1) * 20);
    layout.groups = std::move(groups);
    layout.bytes.resize(layout.idtSize, 0);

    std::vector<std::size_t> descriptorOffsets;
    descriptorOffsets.reserve(layout.groups.size());
    std::unordered_map<std::string, std::size_t> iltEntryOffsets;
    std::unordered_map<std::string, std::size_t> iatEntryOffsets;
    std::unordered_map<std::string, std::size_t> dllNameFieldOffsets;

    for (std::size_t groupIndex = 0; groupIndex < layout.groups.size(); ++groupIndex) {
        descriptorOffsets.push_back(groupIndex * 20);
        auto &group = layout.groups[groupIndex];
        const std::uint32_t iltRva = idataRva + static_cast<std::uint32_t>(layout.bytes.size());
        for (const auto &symbol : group.symbols) {
            iltEntryOffsets.emplace(symbol.symbolName, layout.bytes.size());
            append64(layout.bytes, 0);
        }
        append64(layout.bytes, 0);
        patch32(layout.bytes, descriptorOffsets[groupIndex], iltRva);
    }

    for (std::size_t groupIndex = 0; groupIndex < layout.groups.size(); ++groupIndex) {
        auto &group = layout.groups[groupIndex];
        const std::uint32_t iatRva = idataRva + static_cast<std::uint32_t>(layout.bytes.size());
        for (auto &symbol : group.symbols) {
            symbol.iatRva = idataRva + static_cast<std::uint32_t>(layout.bytes.size());
            iatEntryOffsets.emplace(symbol.symbolName, layout.bytes.size());
            append64(layout.bytes, 0);
        }
        append64(layout.bytes, 0);
        patch32(layout.bytes, descriptorOffsets[groupIndex] + 16, iatRva);
    }

    for (auto &group : layout.groups) {
        for (auto &symbol : group.symbols) {
            const std::uint32_t hintNameRva = idataRva + static_cast<std::uint32_t>(layout.bytes.size());
            patch64(layout.bytes, iltEntryOffsets.at(symbol.symbolName), hintNameRva);
            patch64(layout.bytes, iatEntryOffsets.at(symbol.symbolName), hintNameRva);
            append16(layout.bytes, 0);
            appendString(layout.bytes, symbol.importName);
            if ((layout.bytes.size() & 1u) != 0) {
                layout.bytes.push_back(0);
            }
        }
    }

    for (std::size_t groupIndex = 0; groupIndex < layout.groups.size(); ++groupIndex) {
        auto &group = layout.groups[groupIndex];
        const std::uint32_t dllNameRva = idataRva + static_cast<std::uint32_t>(layout.bytes.size());
        appendString(layout.bytes, group.dllName);
        patch32(layout.bytes, descriptorOffsets[groupIndex] + 12, dllNameRva);
    }

    return layout;
}

// 构建 PE 导出表布局
ExportLayout buildExportLayout(
    std::uint32_t edataRva,
    const std::string &dllName,
    std::vector<ExportLayout::Entry> entries) {
    ExportLayout layout;
    layout.rva = edataRva;
    layout.entries = std::move(entries);

    // 按名称字母排序（PE 规范要求）
    std::sort(layout.entries.begin(), layout.entries.end(),
        [](const ExportLayout::Entry &a, const ExportLayout::Entry &b) {
            return a.name < b.name;
        });

    const std::uint32_t numEntries = static_cast<std::uint32_t>(layout.entries.size());

    // IMAGE_EXPORT_DIRECTORY = 40 字节
    constexpr std::uint32_t dirSize = 40;
    // EAT = numEntries * 4
    const std::uint32_t eatSize = numEntries * 4;
    // Name Pointer Table = numEntries * 4
    const std::uint32_t nptSize = numEntries * 4;
    // Ordinal Table = numEntries * 2
    const std::uint32_t ordSize = numEntries * 2;

    // 计算各部分的 RVA 偏移
    const std::uint32_t eatOffset = dirSize;
    const std::uint32_t nptOffset = eatOffset + eatSize;
    const std::uint32_t ordOffset = nptOffset + nptSize;
    const std::uint32_t dllNameOffset = ordOffset + ordSize;

    // 预留空间：目录 + EAT + NPT + Ordinal + DLL Name
    layout.bytes.resize(dirSize + eatSize + nptSize + ordSize, 0);
    appendString(layout.bytes, dllName);
    // 对齐到 4 字节
    while (layout.bytes.size() % 4 != 0) layout.bytes.push_back(0);

    layout.size = static_cast<std::uint32_t>(layout.bytes.size());

    // 填充 IMAGE_EXPORT_DIRECTORY
    // Characteristics (0)
    append32(layout.bytes, 0);  // 已在 resize 中预留，用 patch
    patch32(layout.bytes, 0, 0);  // Characteristics
    patch32(layout.bytes, 4, 0);  // TimeDateStamp
    patch32(layout.bytes, 8, 0);  // MajorVersion/MinorVersion
    patch32(layout.bytes, 12, edataRva + dllNameOffset);  // Name RVA
    patch32(layout.bytes, 16, 0);  // OrdinalBase
    patch32(layout.bytes, 20, numEntries);  // NumberOfFunctions
    patch32(layout.bytes, 24, numEntries);  // NumberOfNames
    patch32(layout.bytes, 28, edataRva + eatOffset);  // AddressOfFunctions
    patch32(layout.bytes, 32, edataRva + nptOffset);  // AddressOfNames
    patch32(layout.bytes, 36, edataRva + ordOffset);  // AddressOfNameOrdinals

    // 填充 EAT、Name Pointer Table、Ordinal Table
    for (std::uint32_t i = 0; i < numEntries; ++i) {
        // EAT: 函数 RVA
        patch32(layout.bytes, eatOffset + i * 4, layout.entries[i].functionRva);
        // NPT: 名称 RVA（名称字符串紧跟在 ordinal 表之后）
        const std::uint32_t nameOffset = dllNameOffset + static_cast<std::uint32_t>(dllName.size() + 1);
        // 需要计算每个名称的偏移
        // 重新计算：名称字符串从 dllNameOffset + dllName.size() + 1 开始
    }

    // 重新实现：先构建名称字符串区域，再填充表格
    layout.bytes.clear();
    layout.bytes.resize(dirSize + eatSize + nptSize + ordSize, 0);

    // 构建名称字符串区域
    std::vector<std::uint32_t> nameOffsets;
    std::uint32_t nameRegionStart = dirSize + eatSize + nptSize + ordSize;
    // DLL Name
    const std::uint32_t dllNameStrOffset = nameRegionStart;
    for (char c : dllName) layout.bytes.push_back(static_cast<std::uint8_t>(c));
    layout.bytes.push_back(0);
    // 导出名称
    for (const auto &entry : layout.entries) {
        nameOffsets.push_back(static_cast<std::uint32_t>(layout.bytes.size()));
        for (char c : entry.name) layout.bytes.push_back(static_cast<std::uint8_t>(c));
        layout.bytes.push_back(0);
    }
    // 对齐到 4 字节
    while (layout.bytes.size() % 4 != 0) layout.bytes.push_back(0);

    layout.size = static_cast<std::uint32_t>(layout.bytes.size());

    // 填充 IMAGE_EXPORT_DIRECTORY
    patch32(layout.bytes, 0, 0);  // Characteristics
    patch32(layout.bytes, 4, 0);  // TimeDateStamp
    patch32(layout.bytes, 8, 0);  // MajorVersion/MinorVersion
    patch32(layout.bytes, 12, edataRva + dllNameStrOffset);  // Name RVA
    patch32(layout.bytes, 16, 0);  // OrdinalBase
    patch32(layout.bytes, 20, numEntries);  // NumberOfFunctions
    patch32(layout.bytes, 24, numEntries);  // NumberOfNames
    patch32(layout.bytes, 28, edataRva + eatOffset);  // AddressOfFunctions
    patch32(layout.bytes, 32, edataRva + nptOffset);  // AddressOfNames
    patch32(layout.bytes, 36, edataRva + ordOffset);  // AddressOfNameOrdinals

    // 填充 EAT、NPT、Ordinal Table
    for (std::uint32_t i = 0; i < numEntries; ++i) {
        patch32(layout.bytes, eatOffset + i * 4, layout.entries[i].functionRva);
        patch32(layout.bytes, nptOffset + i * 4, edataRva + nameOffsets[i]);
        patch16(layout.bytes, ordOffset + i * 2, static_cast<std::uint16_t>(i));
    }

    return layout;
}

std::uint32_t resolveRelocationTargetRva(
    const LinkedObject &linkedObject,
    const Symbol &symbol,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    const std::unordered_map<std::string, std::uint32_t> &externalSymbols) {
    if (symbol.sectionNumber > 0 &&
        static_cast<std::size_t>(symbol.sectionNumber) <= linkedObject.object.sections.size()) {
        const Section &section = linkedObject.object.sections[symbol.sectionNumber - 1];
        const auto sectionRva = sectionRvas.find(section.name);
        if (sectionRva == sectionRvas.end()) {
            throw std::runtime_error(
                objectDiagnostic(
                    linkedObject.object.path,
                    "unsupported target section in relocation: " + section.name + " for symbol " + symbol.name));
        }
        return sectionRva->second + linkedObject.sectionOffsets[symbol.sectionNumber - 1] + symbol.value;
    }
    if (symbol.sectionNumber == UndefinedSection) {
        const auto found = externalSymbols.find(symbol.name);
        if (found == externalSymbols.end()) {
            throw std::runtime_error(
                objectDiagnostic(linkedObject.object.path, "unresolved external symbol: " + symbol.name));
        }
        return found->second;
    }
    throw std::runtime_error(
        objectDiagnostic(linkedObject.object.path, "unsupported external symbol in relocation: " + symbol.name));
}

void applyRelocations(
    const LinkedObject &linkedObject,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    const std::unordered_map<std::string, std::uint32_t> &externalSymbols,
    SectionLayout &textLayout,
    std::vector<RelocationTraceEntry> *traceEntries,
    std::vector<BaseRelocationSite> *baseRelocationSites) {
    const ObjectFile &object = linkedObject.object;
    const std::size_t textSectionIndex = findTextSectionIndex(object);
    const Section &textSection = object.sections[textSectionIndex];
    for (const auto &relocation : readRelocations(object, textSection)) {
        if (relocation.type != RelAmd64Rel32 && relocation.type != RelAmd64Addr64) {
            throw std::runtime_error(
                objectDiagnostic(
                    object.path,
                    "unsupported COFF relocation type: " + std::to_string(relocation.type) + " in section .text"));
        }
        if (relocation.symbolIndex >= object.symbols.size()) {
            throw std::runtime_error(objectDiagnostic(object.path, "relocation references invalid symbol index"));
        }

        const Symbol &symbol = object.symbols[relocation.symbolIndex];
        const std::uint32_t targetRva = resolveRelocationTargetRva(
            linkedObject,
            symbol,
            sectionRvas,
            externalSymbols);
        if (relocation.type == RelAmd64Rel32) {
            if (linkedObject.sectionOffsets[textSectionIndex] + relocation.offset + 4 > textLayout.bytes.size()) {
                throw std::runtime_error(objectDiagnostic(object.path, "REL32 relocation points outside .text"));
            }

            const std::uint32_t relocationRva =
                sectionRvas.at(".text") + linkedObject.sectionOffsets[textSectionIndex] + relocation.offset;
            const std::int32_t addend = readS32(
                textLayout.bytes,
                linkedObject.sectionOffsets[textSectionIndex] + relocation.offset);
            const std::int64_t relative = static_cast<std::int64_t>(targetRva) +
                static_cast<std::int64_t>(addend) -
                static_cast<std::int64_t>(relocationRva + 4);
            if (relative < INT32_MIN || relative > INT32_MAX) {
                throw std::runtime_error(objectDiagnostic(object.path, "REL32 relocation out of range"));
            }
            if (traceEntries) {
                const std::int64_t effectiveTarget =
                    static_cast<std::int64_t>(targetRva) + static_cast<std::int64_t>(addend);
                traceEntries->push_back(RelocationTraceEntry{
                    linkedObject.object.path,
                    ".text",
                    relocation.offset,
                    "REL32",
                    targetDescription(symbol, addend),
                    static_cast<std::uint32_t>(effectiveTarget),
                    addend,
                    static_cast<std::int32_t>(relative),
                    0,
                    false});
            }
            write32(
                textLayout.bytes,
                linkedObject.sectionOffsets[textSectionIndex] + relocation.offset,
                static_cast<std::uint32_t>(static_cast<std::int32_t>(relative)));
            continue;
        }

        if (linkedObject.sectionOffsets[textSectionIndex] + relocation.offset + 8 > textLayout.bytes.size()) {
            throw std::runtime_error(objectDiagnostic(object.path, "ADDR64 relocation points outside .text"));
        }
        const std::int64_t addend = readS64(
            textLayout.bytes,
            linkedObject.sectionOffsets[textSectionIndex] + relocation.offset);
        const std::uint64_t storedValue = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(ImageBase) +
            static_cast<std::int64_t>(targetRva) +
            addend);
        if (traceEntries) {
            const std::int64_t effectiveTarget =
                static_cast<std::int64_t>(targetRva) + addend;
            traceEntries->push_back(RelocationTraceEntry{
                linkedObject.object.path,
                ".text",
                relocation.offset,
                "ADDR64",
                targetDescription64(symbol, addend),
                static_cast<std::uint32_t>(effectiveTarget),
                addend,
                0,
                storedValue,
                true});
        }
        write64(
            textLayout.bytes,
            linkedObject.sectionOffsets[textSectionIndex] + relocation.offset,
            storedValue);
        if (baseRelocationSites) {
            baseRelocationSites->push_back(BaseRelocationSite{
                ".text",
                sectionRvas.at(".text") + linkedObject.sectionOffsets[textSectionIndex] + relocation.offset});
        }
    }
}

void applyInitializedDataRelocations(
    const LinkedObject &linkedObject,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    const std::unordered_map<std::string, std::uint32_t> &externalSymbols,
    std::vector<SectionLayout> &layouts,
    std::vector<RelocationTraceEntry> *traceEntries,
    std::vector<BaseRelocationSite> *baseRelocationSites) {
    for (std::size_t sectionIndex = 0; sectionIndex < linkedObject.object.sections.size(); ++sectionIndex) {
        const Section &section = linkedObject.object.sections[sectionIndex];
        if (section.name == ".text" || section.relocationCount == 0 || section.rawSize == 0) {
            continue;
        }
        auto layoutIt = std::find_if(
            layouts.begin(),
            layouts.end(),
            [&](const SectionLayout &layout) { return layout.name == section.name; });
        if (layoutIt == layouts.end()) {
            continue;
        }
        if (layoutIt->isUninitializedData) {
            throw std::runtime_error(
                objectDiagnostic(
                    linkedObject.object.path,
                    "unsupported relocation against uninitialized output section: " + section.name));
        }

        const std::uint32_t mergedSectionOffset = linkedObject.sectionOffsets[sectionIndex];
        for (const auto &relocation : readRelocations(linkedObject.object, section)) {
            if (relocation.type != RelAmd64Addr64 && relocation.type != RelAmd64Addr32Nb) {
                throw std::runtime_error(
                    objectDiagnostic(
                        linkedObject.object.path,
                        "unsupported COFF relocation type in " + section.name + ": " +
                        std::to_string(relocation.type)));
            }
            if (relocation.symbolIndex >= linkedObject.object.symbols.size()) {
                throw std::runtime_error(
                    objectDiagnostic(linkedObject.object.path, "relocation references invalid symbol index"));
            }
            if (relocation.type == RelAmd64Addr32Nb) {
                // ADDR32NB：存储 32 位 RVA（不加 ImageBase）
                if (mergedSectionOffset + relocation.offset + 4 > layoutIt->bytes.size()) {
                    throw std::runtime_error(
                        objectDiagnostic(
                            linkedObject.object.path,
                            "ADDR32NB relocation points outside merged section: " + section.name));
                }
                const Symbol &symbol = linkedObject.object.symbols[relocation.symbolIndex];
                const std::uint32_t targetRva = resolveRelocationTargetRva(
                    linkedObject, symbol, sectionRvas, externalSymbols);
                const std::int32_t addend = readS32(layoutIt->bytes, mergedSectionOffset + relocation.offset);
                const std::uint32_t storedValue = targetRva + addend;
                write32(layoutIt->bytes, mergedSectionOffset + relocation.offset, storedValue);
                continue;
            }
            if (mergedSectionOffset + relocation.offset + 8 > layoutIt->bytes.size()) {
                throw std::runtime_error(
                    objectDiagnostic(
                        linkedObject.object.path,
                        "ADDR64 relocation points outside merged section: " + section.name));
            }

            const Symbol &symbol = linkedObject.object.symbols[relocation.symbolIndex];
            const std::uint32_t targetRva = resolveRelocationTargetRva(
                linkedObject,
                symbol,
                sectionRvas,
                externalSymbols);
            const std::int64_t addend = readS64(layoutIt->bytes, mergedSectionOffset + relocation.offset);
            const std::uint64_t storedValue = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(ImageBase) +
                static_cast<std::int64_t>(targetRva) +
                addend);

            if (traceEntries) {
                const std::int64_t effectiveTarget =
                    static_cast<std::int64_t>(targetRva) + addend;
                traceEntries->push_back(RelocationTraceEntry{
                    linkedObject.object.path,
                    section.name,
                    relocation.offset,
                    "ADDR64",
                    targetDescription64(symbol, addend),
                    static_cast<std::uint32_t>(effectiveTarget),
                    addend,
                    0,
                    storedValue,
                    true});
            }

            write64(layoutIt->bytes, mergedSectionOffset + relocation.offset, storedValue);
            if (baseRelocationSites) {
                baseRelocationSites->push_back(BaseRelocationSite{
                    section.name,
                    layoutIt->rva + mergedSectionOffset + relocation.offset});
            }
        }
    }
}

void writeBytes(std::vector<std::uint8_t> &file, std::size_t offset, const std::vector<std::uint8_t> &bytes) {
    if (offset + bytes.size() > file.size()) {
        throw std::runtime_error("internal linker error: write outside file");
    }
    std::copy(bytes.begin(), bytes.end(), file.begin() + offset);
}

BaseRelocationLayout buildBaseRelocationLayout(std::uint32_t relocRva, std::vector<BaseRelocationSite> sites) {
    BaseRelocationLayout layout;
    layout.rva = relocRva;
    if (sites.empty()) {
        return layout;
    }

    std::sort(
        sites.begin(),
        sites.end(),
        [](const BaseRelocationSite &left, const BaseRelocationSite &right) { return left.slotRva < right.slotRva; });
    sites.erase(
        std::unique(
            sites.begin(),
            sites.end(),
            [](const BaseRelocationSite &left, const BaseRelocationSite &right) {
                return left.slotRva == right.slotRva;
            }),
        sites.end());
    layout.siteCount = sites.size();

    std::size_t index = 0;
    while (index < sites.size()) {
        const std::uint32_t pageRva = sites[index].slotRva & ~0x0FFFu;
        std::vector<std::uint16_t> entries;
        while (index < sites.size() && (sites[index].slotRva & ~0x0FFFu) == pageRva) {
            entries.push_back(static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(ImageRelBasedDir64) << 12) | (sites[index].slotRva & 0x0FFFu)));
            ++index;
        }

        layout.pageRvas.push_back(pageRva);
        layout.entryCounts.push_back(static_cast<std::uint16_t>(entries.size()));
        const std::size_t blockOffset = layout.bytes.size();
        append32(layout.bytes, pageRva);
        append32(layout.bytes, 0);
        for (const std::uint16_t entry : entries) {
            append16(layout.bytes, entry);
        }
        while (layout.bytes.size() % 4 != 0) {
            append16(layout.bytes, 0);
        }
        patch32(layout.bytes, blockOffset + 4, static_cast<std::uint32_t>(layout.bytes.size() - blockOffset));
    }

    return layout;
}

void writeSectionName(std::vector<std::uint8_t> &file, std::size_t offset, const char *name) {
    for (std::size_t i = 0; i < 8 && name[i] != 0; ++i) {
        file[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

// 构建 TLS 布局
// TLS 目录结构（x64）：
//   StartAddressOfRawData  : 8 字节（VA）
//   EndAddressOfRawData    : 8 字节（VA）
//   AddressOfIndex         : 8 字节（VA）
//   AddressOfCallBacks     : 8 字节（VA）
//   SizeOfZeroFill         : 4 字节
//   Characteristics        : 4 字节
TlsLayout buildTlsLayout(
    std::uint32_t tlsRva,
    const SectionLayout *tlsSection,
    std::uint64_t imageBase) {
    TlsLayout layout;

    if (!tlsSection || tlsSection->bytes.empty()) {
        return layout;
    }

    // TLS 目录位于 .rdata 中（在 .tls 数据之前）
    // 目录大小 = 4 * 8 + 2 * 4 = 40 字节
    constexpr std::uint32_t TlsDirectorySize = 40;
    layout.directoryRva = tlsRva;
    layout.directorySize = TlsDirectorySize;

    // TLS 原始数据紧跟在目录之后
    layout.rawDataRva = tlsRva + TlsDirectorySize;
    layout.rawDataSize = static_cast<std::uint32_t>(tlsSection->bytes.size());
    layout.rawData = tlsSection->bytes;

    // TLS 索引变量（4 字节）紧跟在原始数据之后
    layout.indexRva = layout.rawDataRva + layout.rawDataSize;

    // TLS 回调数组（null 终止）紧跟在索引之后
    layout.callbacksRva = layout.indexRva + 4;
    layout.callbacks = {0}; // null 终止的回调数组

    // 构建二进制数据
    layout.bytes.clear();
    // StartAddressOfRawData (VA)
    append64(layout.bytes, imageBase + layout.rawDataRva);
    // EndAddressOfRawData (VA)
    append64(layout.bytes, imageBase + layout.rawDataRva + layout.rawDataSize);
    // AddressOfIndex (VA)
    append64(layout.bytes, imageBase + layout.indexRva);
    // AddressOfCallBacks (VA)
    append64(layout.bytes, imageBase + layout.callbacksRva);
    // SizeOfZeroFill
    append32(layout.bytes, layout.zeroFillSize);
    // Characteristics
    append32(layout.bytes, 0);

    return layout;
}

// 构建延迟加载导入布局
// 每个 IMAGE_DELAYLOAD_DESCRIPTOR 结构（x64）：
//   Attributes                   : 4 字节
//   DllNameRVA                   : 4 字节
//   ModuleHandleRVA              : 4 字节
//   ImportAddressTableRVA        : 4 字节
//   ImportNameTableRVA           : 4 字节
//   BoundImportAddressTableRVA   : 4 字节
//   UnloadInformationTableRVA    : 4 字节
//   TimeDateStamp                : 4 字节
// 共 32 字节
DelayImportLayout buildDelayImportLayout(
    std::uint32_t delayImportRva,
    std::vector<DelayImportLayout::Group> groups) {
    DelayImportLayout layout;
    layout.rva = delayImportRva;
    layout.groups = std::move(groups);

    // 如果没有延迟加载导入，直接返回空布局
    if (layout.groups.empty()) {
        return layout;
    }

    constexpr std::uint32_t DescriptorSize = 32;

    // 计算所有需要的空间
    // 1. 描述符表（含 null 终止符）
    const std::uint32_t descriptorCount = static_cast<std::uint32_t>(layout.groups.size());
    const std::uint32_t descriptorTableSize = (descriptorCount + 1) * DescriptorSize;

    // 计算各部分的偏移
    std::uint32_t currentOffset = descriptorTableSize;

    // 2. DLL 名称字符串
    std::vector<std::uint32_t> dllNameOffsets;
    for (const auto &group : layout.groups) {
        dllNameOffsets.push_back(currentOffset);
        currentOffset += static_cast<std::uint32_t>(group.dllName.size() + 1);
    }

    // 3. 模块句柄（每个 8 字节）
    std::vector<std::uint32_t> moduleHandleOffsets;
    for (std::size_t i = 0; i < layout.groups.size(); ++i) {
        moduleHandleOffsets.push_back(currentOffset);
        currentOffset += 8;
    }

    // 4. IAT 和 INT（每个符号 8 字节 + null 终止）
    std::vector<std::uint32_t> iatOffsets;
    std::vector<std::uint32_t> intOffsets;
    for (auto &group : layout.groups) {
        iatOffsets.push_back(currentOffset);
        for (auto &symbol : group.symbols) {
            symbol.iatRva = delayImportRva + currentOffset;
            currentOffset += 8;
        }
        currentOffset += 8; // null 终止

        intOffsets.push_back(currentOffset);
        for (auto &symbol : group.symbols) {
            currentOffset += 8;
        }
        currentOffset += 8; // null 终止
    }

    // 5. Hint/Name 表
    std::vector<std::vector<std::uint32_t>> hintNameOffsets;
    for (auto &group : layout.groups) {
        std::vector<std::uint32_t> offsets;
        for (auto &symbol : group.symbols) {
            offsets.push_back(currentOffset);
            // hint (2) + name + null + padding
            currentOffset += 2 + static_cast<std::uint32_t>(symbol.importName.size()) + 1;
            if (currentOffset % 2 != 0) {
                ++currentOffset;
            }
        }
        hintNameOffsets.push_back(std::move(offsets));
    }

    layout.size = currentOffset;
    layout.bytes.resize(currentOffset, 0);

    // 写入描述符表
    for (std::size_t i = 0; i < layout.groups.size(); ++i) {
        const std::uint32_t descOffset = static_cast<std::uint32_t>(i) * DescriptorSize;
        DelayLoadDescriptor desc;
        desc.attributes = 1; // RvaBased
        desc.dllNameRva = delayImportRva + dllNameOffsets[i];
        desc.moduleHandleRva = delayImportRva + moduleHandleOffsets[i];
        desc.importAddressTableRva = delayImportRva + iatOffsets[i];
        desc.importNameTableRva = delayImportRva + intOffsets[i];
        desc.boundImportAddressTableRva = 0;
        desc.unloadInformationTableRva = 0;
        desc.timeDateStamp = 0; // -1 表示未绑定

        write32(layout.bytes, descOffset, desc.attributes);
        write32(layout.bytes, descOffset + 4, desc.dllNameRva);
        write32(layout.bytes, descOffset + 8, desc.moduleHandleRva);
        write32(layout.bytes, descOffset + 12, desc.importAddressTableRva);
        write32(layout.bytes, descOffset + 16, desc.importNameTableRva);
        write32(layout.bytes, descOffset + 20, desc.boundImportAddressTableRva);
        write32(layout.bytes, descOffset + 24, desc.unloadInformationTableRva);
        write32(layout.bytes, descOffset + 28, desc.timeDateStamp);

        layout.descriptors.push_back(desc);
    }

    // 写入 DLL 名称
    for (std::size_t i = 0; i < layout.groups.size(); ++i) {
        std::memcpy(&layout.bytes[dllNameOffsets[i]],
                     layout.groups[i].dllName.c_str(),
                     layout.groups[i].dllName.size() + 1);
    }

    // 写入 IAT 和 INT（初始时 INT 指向 Hint/Name，IAT 也指向 Hint/Name）
    for (std::size_t gi = 0; gi < layout.groups.size(); ++gi) {
        const auto &group = layout.groups[gi];
        for (std::size_t si = 0; si < group.symbols.size(); ++si) {
            const std::uint32_t hintNameRva = delayImportRva + hintNameOffsets[gi][si];
            write64(layout.bytes, iatOffsets[gi] + si * 8, hintNameRva);
            write64(layout.bytes, intOffsets[gi] + si * 8, hintNameRva);
        }
        // null 终止
        write64(layout.bytes, iatOffsets[gi] + group.symbols.size() * 8, 0);
        write64(layout.bytes, intOffsets[gi] + group.symbols.size() * 8, 0);
    }

    // 写入 Hint/Name 表
    for (std::size_t gi = 0; gi < layout.groups.size(); ++gi) {
        for (std::size_t si = 0; si < layout.groups[gi].symbols.size(); ++si) {
            const auto &symbol = layout.groups[gi].symbols[si];
            const std::uint32_t offset = hintNameOffsets[gi][si];
            write16(layout.bytes, offset, 0); // hint
            std::memcpy(&layout.bytes[offset + 2],
                        symbol.importName.c_str(),
                        symbol.importName.size() + 1);
        }
    }

    return layout;
}

// 为延迟加载导入生成 thunk 代码
// thunk 代码格式（x64）：
//   push <iat_index>        ; 68 xx xx xx xx
//   jmp [__imp_LoadModule]  ; FF 25 xx xx xx xx
void appendDelayLoadThunkPlaceholder(std::vector<std::uint8_t> &text) {
    // push <index> - 占位符，稍后填充
    text.push_back(0x68);
    append32(text, 0);
    // jmp [rip + disp32] - 占位符，稍后填充
    text.push_back(0xFF);
    text.push_back(0x25);
    append32(text, 0);
}

bool isLibraryFile(const fs::path &path) {
    const std::string ext = path.extension().string();
    return ext == ".lib" || ext == ".a";
}

std::vector<LinkedObject> readLinkedObjects(const std::vector<fs::path> &objPaths, unsigned int jobs) {
    if (objPaths.empty()) {
        throw std::runtime_error(linkerDiagnostic("no object files to link"));
    }

    // 展开 .lib/.a 文件为内联 .obj 对象
    struct ObjEntry {
        fs::path path;
        std::vector<std::uint8_t> bytes;  // 非空表示从库中提取的成员
    };
    std::vector<ObjEntry> entries;
    for (const auto &path : objPaths) {
        if (isLibraryFile(path)) {
            auto members = CoffArchive::read(path);
            for (const auto &member : members) {
                ObjEntry entry;
                entry.path = path;
                entry.bytes = member.data;
                entries.push_back(std::move(entry));
            }
        } else {
            entries.push_back({path, {}});
        }
    }

    auto readEntry = [](const ObjEntry &entry) -> LinkedObject {
        LinkedObject linkedObject;
        if (entry.bytes.empty()) {
            linkedObject.object = readObject(entry.path);
        } else {
            linkedObject.object = readObjectFromBytes(
                std::vector<std::uint8_t>(entry.bytes), entry.path);
        }
        linkedObject.sectionOffsets.resize(linkedObject.object.sections.size(), 0);
        return linkedObject;
    };

    std::vector<LinkedObject> linkedObjects;
    linkedObjects.reserve(entries.size());
    if (objectReadWorkerCount(entries.size(), jobs) == 1) {
        for (const auto &entry : entries) {
            linkedObjects.push_back(readEntry(entry));
        }
        return linkedObjects;
    }

    std::vector<std::future<LinkedObject>> futures;
    futures.reserve(entries.size());
    for (const auto &entry : entries) {
        futures.push_back(std::async(std::launch::async, [&readEntry, entry]() {
            return readEntry(entry);
        }));
    }
    for (auto &future : futures) {
        linkedObjects.push_back(future.get());
    }
    return linkedObjects;
}

std::vector<SectionLayout> mergeSections(std::vector<LinkedObject> &linkedObjects) {
    std::vector<SectionLayout> layouts = {
        {".text", {}, 0, 0, 0, 0, 0x60000020, false},
        {".data", {}, 0, 0, 0, 0, 0xc0000040, false},
        {".rdata", {}, 0, 0, 0, 0, 0x40000040, false},
        {".bss", {}, 0, 0, 0, 0, 0xc0000080, true},
        {".tls", {}, 0, 0, 0, 0,
            ImageScnCntInitializedData | ImageScnMemRead | ImageScnMemWrite, false},
        // PE 异常处理表
        {".pdata", {}, 0, 0, 0, 0, 0x40000040, false},  // initialized, read
        {".xdata", {}, 0, 0, 0, 0, 0x40000040, false},  // initialized, read
        // DWARF 调试信息
        {".debug_str",     {}, 0, 0, 0, 0, 0x42000040, false},  // initialized, read, discardable
        {".debug_abbrev",  {}, 0, 0, 0, 0, 0x42000040, false},
        {".debug_line",    {}, 0, 0, 0, 0, 0x42000040, false},
        {".debug_info",    {}, 0, 0, 0, 0, 0x42000040, false}
    };

    for (auto &linkedObject : linkedObjects) {
        for (std::size_t i = 0; i < linkedObject.object.sections.size(); ++i) {
            const Section &section = linkedObject.object.sections[i];
            auto found = std::find_if(
                layouts.begin(),
                layouts.end(),
                [&](const SectionLayout &layout) { return layout.name == section.name; });
            if (found == layouts.end()) {
                continue;
            }
            const std::uint32_t sectionSize = std::max(section.virtualSize, section.rawSize);
            linkedObject.sectionOffsets[i] = found->virtualSize;
            found->virtualSize += sectionSize;

            if (section.rawSize == 0 ||
                (section.characteristics & SectionContainsUninitializedData) != 0) {
                continue;
            }
            if (section.rawPointer + section.rawSize > linkedObject.object.bytes.size()) {
                throw std::runtime_error(
                    objectDiagnostic(linkedObject.object.path, "truncated section " + section.name));
            }
            found->bytes.insert(
                found->bytes.end(),
                linkedObject.object.bytes.begin() + section.rawPointer,
                linkedObject.object.bytes.begin() + section.rawPointer + section.rawSize);
        }
    }

    layouts.erase(
        std::remove_if(
            layouts.begin(),
            layouts.end(),
            [](const SectionLayout &layout) { return layout.virtualSize == 0; }),
        layouts.end());
    return layouts;
}

std::unordered_map<std::string, bool> collectDefinedExternalNames(const std::vector<LinkedObject> &linkedObjects) {
    std::unordered_map<std::string, bool> names;
    for (const auto &linkedObject : linkedObjects) {
        for (const auto &symbol : linkedObject.object.symbols) {
            if (!isTraceableSymbol(symbol) || symbol.sectionNumber <= 0) {
                continue;
            }
            names.emplace(symbol.name, true);
        }
    }
    return names;
}

std::unordered_map<std::string, std::uint32_t> collectExternalSymbols(
    const std::vector<LinkedObject> &linkedObjects,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    std::vector<ResolvedSymbolTraceEntry> *traceEntries) {
    std::unordered_map<std::string, std::uint32_t> symbols;

    for (const auto &linkedObject : linkedObjects) {
        for (const auto &symbol : linkedObject.object.symbols) {
            if (symbol.name.empty() || symbol.name[0] == '.') {
                continue;
            }
            if (symbol.storageClass != StorageClassExternal) {
                continue;
            }
            if (symbol.sectionNumber <= 0 ||
                static_cast<std::size_t>(symbol.sectionNumber) > linkedObject.object.sections.size()) {
                continue;
            }
            const Section &section = linkedObject.object.sections[symbol.sectionNumber - 1];
            const auto sectionRva = sectionRvas.find(section.name);
            if (sectionRva == sectionRvas.end()) {
                continue;
            }
            const std::uint32_t rva =
                sectionRva->second + linkedObject.sectionOffsets[symbol.sectionNumber - 1] + symbol.value;
            const auto [it, inserted] = symbols.emplace(symbol.name, rva);
            if (!inserted) {
                throw std::runtime_error(
                    linkerDiagnostic(
                        "duplicate external symbol: " + symbol.name + " (again in " +
                        linkedObject.object.path.string() + ")"));
            }
            if (traceEntries) {
                traceEntries->push_back(ResolvedSymbolTraceEntry{
                    linkedObject.object.path,
                    section.name,
                    symbol.name,
                    "",
                    "",
                    rva,
                    0,
                    false});
            }
        }
    }

    return symbols;
}

void traceInputObjects(std::ostream &out, const std::vector<LinkedObject> &linkedObjects) {
    out << "[link] input objects\n";
    out << "[link]   summary " << countLabel(linkedObjects.size(), "object", "objects") << '\n';
    for (const auto &linkedObject : linkedObjects) {
        out << "[link] object " << linkedObject.object.path.string() << '\n';
        std::size_t recognizedSectionCount = 0;
        std::size_t externCount = 0;
        std::size_t definedSymbolCount = 0;
        for (const auto &section : linkedObject.object.sections) {
            if (!isRecognizedSection(section.name)) {
                continue;
            }
            ++recognizedSectionCount;
            out << "[link]   section " << section.name
                << " size=" << std::max(section.virtualSize, section.rawSize)
                << " raw=" << section.rawSize
                << " relocations=" << section.relocationCount << '\n';
        }
        for (const auto &symbol : linkedObject.object.symbols) {
            if (!isTraceableSymbol(symbol)) {
                continue;
            }
            if (symbol.sectionNumber == UndefinedSection) {
                ++externCount;
                out << "[link]   extern " << symbol.name << '\n';
                continue;
            }
            if (symbol.sectionNumber <= 0 ||
                static_cast<std::size_t>(symbol.sectionNumber) > linkedObject.object.sections.size()) {
                continue;
            }
            const Section &section = linkedObject.object.sections[symbol.sectionNumber - 1];
            if (!isRecognizedSection(section.name)) {
                continue;
            }
            ++definedSymbolCount;
            out << "[link]   symbol " << symbol.name
                << " section=" << section.name
                << " value=" << hex32(symbol.value) << '\n';
        }
        out << "[link]   summary sections=" << recognizedSectionCount
            << " defined_symbols=" << definedSymbolCount
            << " externs=" << externCount << '\n';
    }
}

void traceMergedSections(
    std::ostream &out,
    const std::vector<SectionLayout> &sections,
    std::uint32_t idataRva,
    std::uint32_t idataVirtualSize,
    std::uint32_t idataRawPointer,
    std::uint32_t idataRawSize,
    const BaseRelocationLayout &baseRelocations,
    std::uint32_t relocRawPointer,
    std::uint32_t relocRawSize) {
    out << "[link] merged sections\n";
    std::uint32_t totalVirtualSize = idataVirtualSize + static_cast<std::uint32_t>(baseRelocations.bytes.size());
    std::uint32_t totalRawSize = idataRawSize + relocRawSize;
    for (const auto &section : sections) {
        totalVirtualSize += section.virtualSize;
        totalRawSize += section.rawSize;
        out << "[link]   " << section.name
            << " rva=" << hex32(section.rva)
            << " vsize=" << section.virtualSize
            << " raw_size=" << section.rawSize
            << " raw_ptr=" << hex32(section.rawPointer)
            << " uninitialized=" << (section.isUninitializedData ? "yes" : "no") << '\n';
    }
    out << "[link]   .idata"
        << " rva=" << hex32(idataRva)
        << " vsize=" << idataVirtualSize
        << " raw_size=" << idataRawSize
        << " raw_ptr=" << hex32(idataRawPointer)
        << " uninitialized=no\n";
    std::size_t totalSections = sections.size() + 1;
    if (!baseRelocations.bytes.empty()) {
        out << "[link]   .reloc"
            << " rva=" << hex32(baseRelocations.rva)
            << " vsize=" << static_cast<std::uint32_t>(baseRelocations.bytes.size())
            << " raw_size=" << relocRawSize
            << " raw_ptr=" << hex32(relocRawPointer)
            << " uninitialized=no\n";
        ++totalSections;
    }
    out << "[link]   summary sections=" << totalSections
        << " total_vsize=" << totalVirtualSize
        << " total_raw_size=" << totalRawSize << '\n';
}

void traceResolvedSymbols(
    std::ostream &out,
    std::vector<ResolvedSymbolTraceEntry> entries,
    const ImportLayout &imports) {
    for (const auto &group : imports.groups) {
        for (const auto &symbol : group.symbols) {
            entries.push_back(ResolvedSymbolTraceEntry{
                {},
                ".idata",
                symbol.symbolName,
                symbol.dllName,
                symbol.sourceName,
                symbol.thunkRva,
                symbol.iatRva,
                true});
        }
    }
    std::sort(
        entries.begin(),
        entries.end(),
        [](const ResolvedSymbolTraceEntry &left, const ResolvedSymbolTraceEntry &right) {
            return left.name < right.name;
        });

    out << "[link] resolved symbols\n";
    out << "[link]   summary " << countLabel(entries.size(), "symbol", "symbols") << '\n';
    for (const auto &entry : entries) {
        out << "[link]   " << entry.name
            << " -> " << hex32(entry.rva);
        if (entry.imported) {
            out << " import_thunk"
                << " dll=" << entry.importDllName
                << " source=" << entry.importSourceName
                << " iat=" << hex32(entry.importIatRva);
        } else {
            out << " from " << entry.objectPath.string() << ":" << entry.sectionName;
        }
        out << '\n';
    }
}

void traceImports(std::ostream &out, const ImportLayout &imports) {
    out << "[link] imports\n";
    if (imports.groups.empty()) {
        out << "[link]   none\n";
        return;
    }
    std::size_t importCount = 0;
    for (const auto &group : imports.groups) {
        importCount += group.symbols.size();
    }
    out << "[link]   summary groups=" << imports.groups.size()
        << " imported_symbols=" << importCount << '\n';
    for (const auto &group : imports.groups) {
        out << "[link]   dll " << group.dllName << ": ";
        for (std::size_t i = 0; i < group.symbols.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << group.symbols[i].symbolName
                << " (source=" << group.symbols[i].sourceName << ")";
        }
        out << '\n';
    }
}

void traceRelocations(std::ostream &out, const std::vector<RelocationTraceEntry> &entries) {
    out << "[link] relocations\n";
    out << "[link]   summary " << countLabel(entries.size(), "relocation", "relocations") << '\n';
    for (const auto &entry : entries) {
        out << "[link]   " << entry.objectPath.string()
            << " section=" << entry.sourceSectionName
            << " off=" << hex32(entry.offset)
            << " type=" << entry.typeName
            << " target=" << entry.targetName
            << " target_rva=" << hex32(entry.targetRva)
            << " addend=" << entry.addend;
        if (entry.hasStoredValue) {
            out << " stored=0x" << std::hex << std::uppercase << entry.storedValue << std::dec;
        } else {
            out << " relative=" << entry.relative;
        }
        out << '\n';
    }
}

void traceBaseRelocations(std::ostream &out, const BaseRelocationLayout &layout) {
    out << "[link] base relocations\n";
    if (layout.bytes.empty()) {
        out << "[link]   none\n";
        return;
    }
    out << "[link]   summary " << countLabel(layout.siteCount, "site", "sites")
        << " blocks=" << layout.pageRvas.size() << '\n';
    for (std::size_t i = 0; i < layout.pageRvas.size(); ++i) {
        out << "[link]   page=" << hex32(layout.pageRvas[i])
            << " entries=" << layout.entryCounts[i] << '\n';
    }
}

} // namespace

void PeLinker::linkSingleObject(
    const fs::path &objPath,
    const fs::path &exePath,
    unsigned int jobs,
    std::ostream *trace) {
    linkObjects({objPath}, exePath, jobs, trace, nullptr);
}

void PeLinker::linkObjects(
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    unsigned int jobs,
    std::ostream *trace,
    const LinkerInvocation *invocation) {
    std::vector<LinkedObject> linkedObjects = readLinkedObjects(objPaths, jobs);
    if (trace) {
        traceInputObjects(*trace, linkedObjects);
    }
    std::vector<SectionLayout> sections = mergeSections(linkedObjects);
    auto textLayout = std::find_if(
        sections.begin(),
        sections.end(),
        [](const SectionLayout &layout) { return layout.name == ".text"; });
    if (textLayout == sections.end()) {
        throw std::runtime_error(linkerDiagnostic("linked objects do not contain a .text section"));
    }

    const std::vector<ImportSpec> importCatalog = loadImportCatalog();
    const std::unordered_map<std::string, bool> definedSymbolNames =
        collectDefinedExternalNames(linkedObjects);
    const std::vector<ResolvedImportEntry> resolvedImports =
        collectResolvedImports(linkedObjects, definedSymbolNames, importCatalog);
    std::vector<ImportLayout::Group> requestedImports =
        groupResolvedImports(resolvedImports);
    for (auto &group : requestedImports) {
        for (auto &symbol : group.symbols) {
            const std::uint32_t thunkOffset = static_cast<std::uint32_t>(textLayout->bytes.size());
            appendImportThunkPlaceholder(textLayout->bytes);
            symbol.thunkRva = FirstSectionRva + thunkOffset;
        }
    }
    textLayout->virtualSize = static_cast<std::uint32_t>(textLayout->bytes.size());

    std::uint32_t nextRva = FirstSectionRva;
    std::unordered_map<std::string, std::uint32_t> sectionRvas;
    for (auto &section : sections) {
        section.rva = nextRva;
        nextRva = alignTo(nextRva + section.virtualSize, SectionAlignment);
        sectionRvas.emplace(section.name, section.rva);
    }

    // 检查是否存在 .tls 节
    auto tlsSectionIt = std::find_if(
        sections.begin(),
        sections.end(),
        [](const SectionLayout &layout) { return layout.name == ".tls"; });
    const bool hasTls = (tlsSectionIt != sections.end() && !tlsSectionIt->bytes.empty());

    // 如果存在 .tls 节，构建 TLS 布局并追加到 .rdata
    TlsLayout tlsLayout;
    if (hasTls) {
        auto rdataIt = std::find_if(
            sections.begin(),
            sections.end(),
            [](const SectionLayout &layout) { return layout.name == ".rdata"; });
        if (rdataIt == sections.end()) {
            throw std::runtime_error(linkerDiagnostic("TLS section requires .rdata section"));
        }

        const std::uint32_t tlsDirStartRva = rdataIt->rva + static_cast<std::uint32_t>(rdataIt->bytes.size());
        tlsLayout = buildTlsLayout(tlsDirStartRva, &*tlsSectionIt, ImageBase);

        // 追加 TLS 目录到 .rdata
        rdataIt->bytes.insert(rdataIt->bytes.end(), tlsLayout.bytes.begin(), tlsLayout.bytes.end());
        // 追加 TLS 原始数据
        rdataIt->bytes.insert(rdataIt->bytes.end(), tlsLayout.rawData.begin(), tlsLayout.rawData.end());
        // 追加 TLS 索引（4 字节）
        rdataIt->bytes.resize(rdataIt->bytes.size() + 4, 0);
        // 追加 TLS 回调数组
        for (const auto &cb : tlsLayout.callbacks) {
            append64(rdataIt->bytes, cb);
        }

        rdataIt->virtualSize = static_cast<std::uint32_t>(rdataIt->bytes.size());
        if (trace) {
            *trace << "[link] TLS directory at rva=" << hex32(tlsLayout.directoryRva)
                   << " size=" << tlsLayout.directorySize
                   << " raw_data_rva=" << hex32(tlsLayout.rawDataRva)
                   << " raw_data_size=" << tlsLayout.rawDataSize << '\n';
        }
    }

    std::vector<ResolvedSymbolTraceEntry> resolvedSymbolTraceEntries;
    const std::unordered_map<std::string, std::uint32_t> definedSymbols =
        collectExternalSymbols(
            linkedObjects,
            sectionRvas,
            trace ? &resolvedSymbolTraceEntries : nullptr);

    const std::uint32_t idataRva = nextRva;
    ImportLayout imports = buildImportLayout(idataRva, std::move(requestedImports));
    std::unordered_map<std::string, std::uint32_t> externalSymbols = definedSymbols;
    for (const auto &group : imports.groups) {
        for (const auto &symbol : group.symbols) {
            const auto [it, inserted] = externalSymbols.emplace(symbol.symbolName, symbol.thunkRva);
            if (!inserted) {
                throw std::runtime_error(
                    linkerDiagnostic(
                        "duplicate external symbol: " + symbol.symbolName +
                        " (import thunk collision for " + symbol.dllName + ")"));
            }
            const std::uint32_t thunkOffset = symbol.thunkRva - sectionRvas.at(".text");
            patchImportThunkDisplacement(textLayout->bytes, thunkOffset, symbol.thunkRva, symbol.iatRva);
        }
    }

    // 处理延迟加载导入
    const std::vector<ImportSpec> delayImportCatalog = loadDelayImportCatalog();
    const std::vector<ResolvedImportEntry> resolvedDelayImports =
        collectDelayImports(linkedObjects, definedSymbolNames, delayImportCatalog);
    std::vector<DelayImportLayout::Group> requestedDelayImports =
        groupDelayImports(resolvedDelayImports);

    // 为延迟加载导入生成 thunk 代码
    for (auto &group : requestedDelayImports) {
        for (auto &symbol : group.symbols) {
            const std::uint32_t thunkOffset = static_cast<std::uint32_t>(textLayout->bytes.size());
            appendDelayLoadThunkPlaceholder(textLayout->bytes);
            symbol.thunkRva = FirstSectionRva + thunkOffset;
        }
    }
    textLayout->virtualSize = static_cast<std::uint32_t>(textLayout->bytes.size());

    // 构建延迟加载导入布局
    const std::uint32_t delayImportRva = alignTo(
        idataRva + static_cast<std::uint32_t>(imports.bytes.size()),
        SectionAlignment);
    DelayImportLayout delayImports = buildDelayImportLayout(delayImportRva, std::move(requestedDelayImports));

    // 将延迟加载导入符号添加到外部符号表
    for (const auto &group : delayImports.groups) {
        for (const auto &symbol : group.symbols) {
            const auto [it, inserted] = externalSymbols.emplace(symbol.symbolName, symbol.thunkRva);
            if (!inserted) {
                throw std::runtime_error(
                    linkerDiagnostic(
                        "duplicate external symbol: " + symbol.symbolName +
                        " (delay import thunk collision for " + symbol.dllName + ")"));
            }
        }
    }

    if (trace && !delayImports.groups.empty()) {
        *trace << "[link] delay imports\n";
        *trace << "[link]   summary groups=" << delayImports.groups.size() << '\n';
        for (const auto &group : delayImports.groups) {
            *trace << "[link]   dll " << group.dllName << ": ";
            for (std::size_t i = 0; i < group.symbols.size(); ++i) {
                if (i > 0) {
                    *trace << ", ";
                }
                *trace << group.symbols[i].symbolName;
            }
            *trace << '\n';
        }
    }

    const auto entry = externalSymbols.find("mainCRTStartup");
    if (entry == externalSymbols.end()) {
        throw std::runtime_error(
            linkerDiagnostic("missing entry symbol: mainCRTStartup; provide a translation unit that defines main"));
    }
    const std::uint32_t entryRva = entry->second;
    std::vector<RelocationTraceEntry> relocationTraceEntries;
    std::vector<BaseRelocationSite> baseRelocationSites;
    for (const auto &linkedObject : linkedObjects) {
        applyRelocations(
            linkedObject,
            sectionRvas,
            externalSymbols,
            *textLayout,
            trace ? &relocationTraceEntries : nullptr,
            &baseRelocationSites);
        applyInitializedDataRelocations(
            linkedObject,
            sectionRvas,
            externalSymbols,
            sections,
            trace ? &relocationTraceEntries : nullptr,
            &baseRelocationSites);
    }

    const BaseRelocationLayout baseRelocations = buildBaseRelocationLayout(
        alignTo(delayImportRva + static_cast<std::uint32_t>(delayImports.bytes.size()), SectionAlignment),
        baseRelocationSites);
    const bool hasBaseRelocations = !baseRelocations.bytes.empty();
    const bool hasDelayImports = !delayImports.groups.empty();

    std::uint32_t sectionCount = static_cast<std::uint32_t>(
        sections.size() + 1 + (hasBaseRelocations ? 1 : 0));
    const std::uint32_t headersSize = alignTo(0x80 + 4 + 20 + 0xf0 + sectionCount * 40, FileAlignment);
    std::uint32_t nextRawPointer = headersSize;
    for (auto &section : sections) {
        if (section.isUninitializedData) {
            section.rawPointer = nextRawPointer;
            section.rawSize = 0;
            continue;
        }
        section.rawPointer = nextRawPointer;
        section.rawSize = alignTo(static_cast<std::uint32_t>(section.bytes.size()), FileAlignment);
        nextRawPointer += section.rawSize;
    }
    const std::uint32_t idataVirtualSize = static_cast<std::uint32_t>(imports.bytes.size());
    const std::uint32_t idataRawSize = alignTo(idataVirtualSize, FileAlignment);
    const std::uint32_t idataRawPointer = nextRawPointer;
    nextRawPointer += idataRawSize;

    // 延迟导入节
    const std::uint32_t delayImportVirtualSize = static_cast<std::uint32_t>(delayImports.bytes.size());
    const std::uint32_t delayImportRawSize = hasDelayImports ? alignTo(delayImportVirtualSize, FileAlignment) : 0;
    const std::uint32_t delayImportRawPointer = nextRawPointer;
    if (hasDelayImports) {
        nextRawPointer += delayImportRawSize;
    }

    const std::uint32_t relocVirtualSize = static_cast<std::uint32_t>(baseRelocations.bytes.size());
    const std::uint32_t relocRawSize = hasBaseRelocations ? alignTo(relocVirtualSize, FileAlignment) : 0;
    const std::uint32_t relocRawPointer = nextRawPointer;
    if (hasBaseRelocations) {
        nextRawPointer += relocRawSize;
    }

    // 构建导出表布局
    ExportLayout exportLayout;
    bool hasExports = false;
    if (invocation && !invocation->exports.empty()) {
        const std::uint32_t edataRva = hasBaseRelocations
            ? alignTo(baseRelocations.rva + relocVirtualSize, SectionAlignment)
            : alignTo(delayImportRva + static_cast<std::uint32_t>(delayImports.bytes.size()), SectionAlignment);
        std::vector<ExportLayout::Entry> exportEntries;
        for (const auto &exp : invocation->exports) {
            const auto it = externalSymbols.find(exp.symbolName);
            if (it == externalSymbols.end()) {
                throw std::runtime_error(
                    linkerDiagnostic("unresolved export symbol: " + exp.symbolName));
            }
            exportEntries.push_back({exp.name, it->second});
        }
        exportLayout = buildExportLayout(edataRva, "minic_output.dll", std::move(exportEntries));
        hasExports = !exportLayout.entries.empty();
        if (hasExports) {
            sectionCount += 1;
        }
    }

    // 导出表节
    const std::uint32_t edataVirtualSize = hasExports ? exportLayout.size : 0;
    const std::uint32_t edataRawSize = hasExports ? alignTo(edataVirtualSize, FileAlignment) : 0;
    const std::uint32_t edataRawPointer = nextRawPointer;
    if (hasExports) {
        nextRawPointer += edataRawSize;
    }

    // 计算最终镜像大小
    std::uint32_t finalImageEnd = idataRva + idataVirtualSize;
    if (hasDelayImports) {
        finalImageEnd = delayImports.rva + delayImportVirtualSize;
    }
    if (hasBaseRelocations) {
        finalImageEnd = baseRelocations.rva + relocVirtualSize;
    }
    if (hasExports) {
        finalImageEnd = exportLayout.rva + edataVirtualSize;
    }
    const std::uint32_t sizeOfImage = alignTo(finalImageEnd, SectionAlignment);
    const std::uint32_t fileSize = nextRawPointer;

    if (trace) {
        traceMergedSections(
            *trace,
            sections,
            idataRva,
            idataVirtualSize,
            idataRawPointer,
            idataRawSize,
            baseRelocations,
            relocRawPointer,
            relocRawSize);
        traceImports(*trace, imports);
        traceResolvedSymbols(*trace, resolvedSymbolTraceEntries, imports);
        traceRelocations(*trace, relocationTraceEntries);
        traceBaseRelocations(*trace, baseRelocations);
        if (hasExports) {
            *trace << "[link] exports\n";
            *trace << "[link]   summary " << exportLayout.entries.size() << " entries\n";
            for (const auto &entry : exportLayout.entries) {
                *trace << "[link]   " << entry.name << " -> rva=0x" << std::hex << entry.functionRva << std::dec << '\n';
            }
        }
    }

    std::vector<std::uint8_t> file(0x80, 0);
    file[0] = 'M';
    file[1] = 'Z';
    write32(file, 0x3c, 0x80);

    file.push_back('P');
    file.push_back('E');
    file.push_back(0);
    file.push_back(0);

    append16(file, MachineAmd64);
    append16(file, static_cast<std::uint16_t>(sectionCount));
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append16(file, 0xf0);
    append16(file, 0x0022);

    append16(file, 0x20b);
    file.push_back(0);
    file.push_back(0);
    std::uint32_t initializedDataSize = idataRawSize;
    std::uint32_t uninitializedDataSize = 0;
    for (const auto &section : sections) {
        if (section.isUninitializedData) {
            uninitializedDataSize += section.virtualSize;
        } else if (section.name != ".text") {
            initializedDataSize += section.rawSize;
        }
    }

    append32(file, sections[0].rawSize);
    append32(file, initializedDataSize);
    append32(file, uninitializedDataSize);
    append32(file, entryRva);
    append32(file, sectionRvas.at(".text"));
    append64(file, ImageBase);
    append32(file, SectionAlignment);
    append32(file, FileAlignment);
    append16(file, 6);
    append16(file, 0);
    append16(file, 0);
    append16(file, 0);
    append16(file, 6);
    append16(file, 0);
    append32(file, 0);
    append32(file, sizeOfImage);
    append32(file, headersSize);
    append32(file, 0);
    append16(file, 3);
    append16(file, hasBaseRelocations ? 0x8140 : 0x8100);
    append64(file, 0x100000);
    append64(file, 0x1000);
    append64(file, 0x100000);
    append64(file, 0x1000);
    append32(file, 0);
    append32(file, 16);
    // Data Directory 索引 0: Export Table
    append32(file, hasExports ? exportLayout.rva : 0);
    append32(file, hasExports ? edataVirtualSize : 0);
    // Data Directory 索引 1: Import Table
    append32(file, imports.idtRva);
    append32(file, imports.idtSize);
    // Data Directory 索引 2: Resource Table
    append32(file, 0);
    append32(file, 0);

    // Data Directory 索引 3: Exception Table（.pdata）
    auto pdataIt = std::find_if(sections.begin(), sections.end(),
        [](const SectionLayout &s) { return s.name == ".pdata"; });
    if (pdataIt != sections.end() && pdataIt->virtualSize > 0) {
        append32(file, pdataIt->rva);
        append32(file, pdataIt->virtualSize);
    } else {
        append32(file, 0);
        append32(file, 0);
    }

    // Data Directory 索引 4: Certificate Table
    append32(file, 0);
    append32(file, 0);
    // Data Directory 索引 5: Base Relocation Table
    append32(file, hasBaseRelocations ? baseRelocations.rva : 0);
    append32(file, hasBaseRelocations ? relocVirtualSize : 0);

    // Data Directory 索引 6-8（Debug, Architecture, GlobalPtr）
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);

    // Data Directory 索引 9: TLS Table
    append32(file, hasTls ? tlsLayout.directoryRva : 0);
    append32(file, hasTls ? tlsLayout.directorySize : 0);

    // Data Directory 索引 10-12（Load Config, Bound Import, IAT）
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);

    // Data Directory 索引 13: Delay Import Descriptor
    append32(file, hasDelayImports ? delayImports.rva : 0);
    append32(file, hasDelayImports ? delayImports.size : 0);

    // Data Directory 索引 14-15（CLR Runtime Header, Reserved）
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);
    append32(file, 0);

    for (const auto &section : sections) {
        const std::size_t header = file.size();
        file.resize(file.size() + 40, 0);
        writeSectionName(file, header, section.name.c_str());
        write32(file, header + 8, section.virtualSize);
        write32(file, header + 12, section.rva);
        write32(file, header + 16, section.rawSize);
        write32(file, header + 20, section.rawPointer);
        write32(file, header + 36, section.characteristics);
    }

    const std::size_t idataHeader = file.size();
    file.resize(file.size() + 40, 0);
    writeSectionName(file, idataHeader, ".idata");
    write32(file, idataHeader + 8, idataVirtualSize);
    write32(file, idataHeader + 12, idataRva);
    write32(file, idataHeader + 16, idataRawSize);
    write32(file, idataHeader + 20, idataRawPointer);
    write32(file, idataHeader + 36, 0xc0000040);

    if (hasDelayImports) {
        const std::size_t delayHeader = file.size();
        file.resize(file.size() + 40, 0);
        writeSectionName(file, delayHeader, ".didat");
        write32(file, delayHeader + 8, delayImportVirtualSize);
        write32(file, delayHeader + 12, delayImports.rva);
        write32(file, delayHeader + 16, delayImportRawSize);
        write32(file, delayHeader + 20, delayImportRawPointer);
        write32(file, delayHeader + 36, 0xc0000040);
    }

    if (hasBaseRelocations) {
        const std::size_t relocHeader = file.size();
        file.resize(file.size() + 40, 0);
        writeSectionName(file, relocHeader, ".reloc");
        write32(file, relocHeader + 8, relocVirtualSize);
        write32(file, relocHeader + 12, baseRelocations.rva);
        write32(file, relocHeader + 16, relocRawSize);
        write32(file, relocHeader + 20, relocRawPointer);
        write32(file, relocHeader + 36, 0x42000040);
    }

    if (hasExports) {
        const std::size_t edataHeader = file.size();
        file.resize(file.size() + 40, 0);
        writeSectionName(file, edataHeader, ".edata");
        write32(file, edataHeader + 8, edataVirtualSize);
        write32(file, edataHeader + 12, exportLayout.rva);
        write32(file, edataHeader + 16, edataRawSize);
        write32(file, edataHeader + 20, edataRawPointer);
        write32(file, edataHeader + 36, 0x40000040);  // IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ
    }

    if (file.size() > headersSize) {
        throw std::runtime_error("internal linker error: PE headers exceed declared header size");
    }
    file.resize(fileSize, 0);

    for (const auto &section : sections) {
        writeBytes(file, section.rawPointer, section.bytes);
    }
    writeBytes(file, idataRawPointer, imports.bytes);
    if (hasDelayImports) {
        writeBytes(file, delayImportRawPointer, delayImports.bytes);
    }
    if (hasBaseRelocations) {
        writeBytes(file, relocRawPointer, baseRelocations.bytes);
    }
    if (hasExports) {
        writeBytes(file, edataRawPointer, exportLayout.bytes);
    }

    if (!exePath.parent_path().empty()) {
        fs::create_directories(exePath.parent_path());
    }

    std::ofstream out(exePath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write executable: " + exePath.string());
    }
    out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
}
