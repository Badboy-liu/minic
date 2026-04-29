#include "PeLinker.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::uint16_t MachineAmd64 = 0x8664;
constexpr std::uint16_t RelAmd64Addr64 = 0x0001;
constexpr std::uint16_t RelAmd64Rel32 = 0x0004;
constexpr std::int16_t UndefinedSection = 0;
constexpr std::uint8_t StorageClassExternal = 2;
constexpr std::uint32_t SectionContainsUninitializedData = 0x00000080;

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
        std::uint32_t thunkRva = 0;
        std::uint32_t iatRva = 0;
    };
    struct Group {
        std::string dllName;
        std::vector<Symbol> symbols;
    };
    std::vector<Group> groups;
};

struct ResolvedSymbolTraceEntry {
    fs::path objectPath;
    std::string sectionName;
    std::string name;
    std::string importDllName;
    std::uint32_t rva = 0;
    std::uint32_t importIatRva = 0;
    bool imported = false;
};

struct ImportSpec {
    const char *symbolName;
    const char *importName;
    const char *dllName;
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

bool isRecognizedSection(const std::string &name) {
    return name == ".text" || name == ".data" || name == ".rdata" || name == ".bss";
}

bool isTraceableSymbol(const Symbol &symbol) {
    return !symbol.name.empty() && symbol.name[0] != '.' && symbol.storageClass == StorageClassExternal;
}

const std::vector<ImportSpec> &importCatalog() {
    static const std::vector<ImportSpec> imports = {
        {"ExitProcess", "ExitProcess", "kernel32.dll"},
        {"fn_GetCurrentProcessId", "GetCurrentProcessId", "kernel32.dll"},
        {"fn_puts", "puts", "msvcrt.dll"},
    };
    return imports;
}

const ImportSpec *findImportSpec(const std::string &symbolName) {
    const auto &catalog = importCatalog();
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
    return findImportSpec(symbolName) != nullptr;
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
    if (symbol.sectionNumber == UndefinedSection && isImportedSymbolName(symbol.name)) {
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
    if (symbol.sectionNumber == UndefinedSection && isImportedSymbolName(symbol.name)) {
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

ObjectFile readObject(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(objectDiagnostic(path, "failed to open object file"));
    }

    ObjectFile object;
    object.path = path;
    object.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

std::vector<ImportLayout::Group> buildRequestedImports(
    const std::vector<LinkedObject> &linkedObjects,
    const std::unordered_map<std::string, bool> &definedSymbolNames) {
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

    std::vector<ImportLayout::Group> groups;
    for (const auto &spec : importCatalog()) {
        const auto unresolved = unresolvedExternals.find(spec.symbolName);
        if (unresolved == unresolvedExternals.end()) {
            continue;
        }
        auto group = std::find_if(
            groups.begin(),
            groups.end(),
            [&](const ImportLayout::Group &entry) { return entry.dllName == spec.dllName; });
        if (group == groups.end()) {
            groups.push_back(ImportLayout::Group{spec.dllName, {}});
            group = std::prev(groups.end());
        }
        group->symbols.push_back(ImportLayout::Symbol{spec.symbolName, spec.importName, spec.dllName, 0, 0});
        unresolvedExternals.erase(unresolved);
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
    std::vector<RelocationTraceEntry> *traceEntries) {
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
    }
}

void applyInitializedDataRelocations(
    const LinkedObject &linkedObject,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    const std::unordered_map<std::string, std::uint32_t> &externalSymbols,
    std::vector<SectionLayout> &layouts,
    std::vector<RelocationTraceEntry> *traceEntries) {
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
            if (relocation.type != RelAmd64Addr64) {
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
        }
    }
}

void writeBytes(std::vector<std::uint8_t> &file, std::size_t offset, const std::vector<std::uint8_t> &bytes) {
    if (offset + bytes.size() > file.size()) {
        throw std::runtime_error("internal linker error: write outside file");
    }
    std::copy(bytes.begin(), bytes.end(), file.begin() + offset);
}

void writeSectionName(std::vector<std::uint8_t> &file, std::size_t offset, const char *name) {
    for (std::size_t i = 0; i < 8 && name[i] != 0; ++i) {
        file[offset + i] = static_cast<std::uint8_t>(name[i]);
    }
}

std::vector<LinkedObject> readLinkedObjects(const std::vector<fs::path> &objPaths) {
    if (objPaths.empty()) {
        throw std::runtime_error(linkerDiagnostic("no object files to link"));
    }

    std::vector<LinkedObject> linkedObjects;
    for (const auto &objPath : objPaths) {
        LinkedObject linkedObject;
        linkedObject.object = readObject(objPath);
        linkedObject.sectionOffsets.resize(linkedObject.object.sections.size(), 0);
        linkedObjects.push_back(std::move(linkedObject));
    }
    return linkedObjects;
}

std::vector<SectionLayout> mergeSections(std::vector<LinkedObject> &linkedObjects) {
    std::vector<SectionLayout> layouts = {
        {".text", {}, 0, 0, 0, 0, 0x60000020, false},
        {".data", {}, 0, 0, 0, 0, 0xc0000040, false},
        {".rdata", {}, 0, 0, 0, 0, 0x40000040, false},
        {".bss", {}, 0, 0, 0, 0, 0xc0000080, true}
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
    std::uint32_t idataRawSize) {
    out << "[link] merged sections\n";
    std::uint32_t totalVirtualSize = idataVirtualSize;
    std::uint32_t totalRawSize = idataRawSize;
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
    out << "[link]   summary sections=" << (sections.size() + 1)
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
            out << group.symbols[i].symbolName;
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

} // namespace

void PeLinker::linkSingleObject(const fs::path &objPath, const fs::path &exePath, std::ostream *trace) {
    linkObjects({objPath}, exePath, trace);
}

void PeLinker::linkObjects(
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    std::ostream *trace) {
    std::vector<LinkedObject> linkedObjects = readLinkedObjects(objPaths);
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

    const std::unordered_map<std::string, bool> definedSymbolNames =
        collectDefinedExternalNames(linkedObjects);
    std::vector<ImportLayout::Group> requestedImports =
        buildRequestedImports(linkedObjects, definedSymbolNames);
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

    const auto entry = externalSymbols.find("mainCRTStartup");
    if (entry == externalSymbols.end()) {
        throw std::runtime_error(
            linkerDiagnostic("missing entry symbol: mainCRTStartup; provide a translation unit that defines main"));
    }
    const std::uint32_t entryRva = entry->second;
    std::vector<RelocationTraceEntry> relocationTraceEntries;
    for (const auto &linkedObject : linkedObjects) {
        applyRelocations(
            linkedObject,
            sectionRvas,
            externalSymbols,
            *textLayout,
            trace ? &relocationTraceEntries : nullptr);
        applyInitializedDataRelocations(
            linkedObject,
            sectionRvas,
            externalSymbols,
            sections,
            trace ? &relocationTraceEntries : nullptr);
    }

    const std::uint32_t sectionCount = static_cast<std::uint32_t>(sections.size() + 1);
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
    const std::uint32_t sizeOfImage = alignTo(idataRva + idataVirtualSize, SectionAlignment);
    const std::uint32_t fileSize = idataRawPointer + idataRawSize;

    if (trace) {
        traceMergedSections(*trace, sections, idataRva, idataVirtualSize, idataRawPointer, idataRawSize);
        traceImports(*trace, imports);
        traceResolvedSymbols(*trace, resolvedSymbolTraceEntries, imports);
        traceRelocations(*trace, relocationTraceEntries);
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
    // The current teaching linker does not emit a .reloc table yet, so
    // data-side absolute addresses must keep the preferred image base.
    append16(file, 0x8100);
    append64(file, 0x100000);
    append64(file, 0x1000);
    append64(file, 0x100000);
    append64(file, 0x1000);
    append32(file, 0);
    append32(file, 16);
    append32(file, 0);
    append32(file, 0);
    append32(file, imports.idtRva);
    append32(file, imports.idtSize);
    for (int i = 2; i < 16; ++i) {
        append32(file, 0);
        append32(file, 0);
    }

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

    if (file.size() > headersSize) {
        throw std::runtime_error("internal linker error: PE headers exceed declared header size");
    }
    file.resize(fileSize, 0);

    for (const auto &section : sections) {
        writeBytes(file, section.rawPointer, section.bytes);
    }
    writeBytes(file, idataRawPointer, imports.bytes);

    if (!exePath.parent_path().empty()) {
        fs::create_directories(exePath.parent_path());
    }

    std::ofstream out(exePath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write executable: " + exePath.string());
    }
    out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
}
