#include "PeLinker.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::uint16_t MachineAmd64 = 0x8664;
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
    std::uint32_t iatRva = 0;
    std::uint32_t functionNameRva = 0;
    std::uint32_t dllNameRva = 0;
    std::vector<std::uint8_t> bytes;
};

std::uint32_t alignTo(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
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
        throw std::runtime_error("failed to open object file: " + path.string());
    }

    ObjectFile object;
    object.path = path;
    object.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (object.bytes.size() < 20) {
        throw std::runtime_error("object file is too small");
    }

    const std::uint16_t machine = read16(object.bytes, 0);
    if (machine != MachineAmd64) {
        throw std::runtime_error("only AMD64 COFF objects are supported");
    }

    const std::uint16_t sectionCount = read16(object.bytes, 2);
    const std::uint32_t symbolTableOffset = read32(object.bytes, 8);
    const std::uint32_t symbolCount = read32(object.bytes, 12);
    const std::uint16_t optionalHeaderSize = read16(object.bytes, 16);

    const std::size_t sectionTableOffset = 20 + optionalHeaderSize;
    for (std::uint16_t i = 0; i < sectionCount; ++i) {
        const std::size_t offset = sectionTableOffset + static_cast<std::size_t>(i) * 40;
        if (offset + 40 > object.bytes.size()) {
            throw std::runtime_error("truncated COFF section table");
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
            throw std::runtime_error("truncated COFF symbol table");
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
    throw std::runtime_error("object file does not contain a .text section");
}

std::vector<Relocation> readRelocations(const ObjectFile &object, const Section &section) {
    std::vector<Relocation> relocations;
    for (std::uint16_t i = 0; i < section.relocationCount; ++i) {
        const std::size_t offset = section.relocationPointer + static_cast<std::size_t>(i) * 10;
        if (offset + 10 > object.bytes.size()) {
            throw std::runtime_error("truncated COFF relocation table");
        }

        relocations.push_back(Relocation{
            read32(object.bytes, offset),
            read32(object.bytes, offset + 4),
            read16(object.bytes, offset + 8)});
    }
    return relocations;
}

ImportLayout buildImportLayout(std::uint32_t idataRva) {
    ImportLayout layout;
    layout.idtRva = idataRva;
    layout.idtSize = 40;

    layout.bytes.resize(40, 0);
    const std::uint32_t iltOffset = static_cast<std::uint32_t>(layout.bytes.size());
    const std::uint32_t iltRva = idataRva + iltOffset;
    append64(layout.bytes, 0);
    append64(layout.bytes, 0);

    const std::uint32_t iatOffset = static_cast<std::uint32_t>(layout.bytes.size());
    layout.iatRva = idataRva + iatOffset;
    append64(layout.bytes, 0);
    append64(layout.bytes, 0);

    const std::uint32_t functionNameOffset = static_cast<std::uint32_t>(layout.bytes.size());
    layout.functionNameRva = idataRva + functionNameOffset;
    append16(layout.bytes, 0);
    appendString(layout.bytes, "ExitProcess");

    const std::uint32_t dllNameOffset = static_cast<std::uint32_t>(layout.bytes.size());
    layout.dllNameRva = idataRva + dllNameOffset;
    appendString(layout.bytes, "kernel32.dll");

    patch32(layout.bytes, iltOffset, layout.functionNameRva);
    patch32(layout.bytes, iatOffset, layout.functionNameRva);

    patch32(layout.bytes, 0, iltRva);
    patch32(layout.bytes, 12, layout.dllNameRva);
    patch32(layout.bytes, 16, layout.iatRva);

    return layout;
}

void applyRelocations(
    const LinkedObject &linkedObject,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas,
    const std::unordered_map<std::string, std::uint32_t> &externalSymbols,
    SectionLayout &textLayout,
    std::uint32_t exitProcessThunkRva) {
    const ObjectFile &object = linkedObject.object;
    const std::size_t textSectionIndex = findTextSectionIndex(object);
    const Section &textSection = object.sections[textSectionIndex];
    for (const auto &relocation : readRelocations(object, textSection)) {
        if (relocation.type != RelAmd64Rel32) {
            throw std::runtime_error("unsupported COFF relocation type: " + std::to_string(relocation.type));
        }
        if (relocation.symbolIndex >= object.symbols.size()) {
            throw std::runtime_error("relocation references invalid symbol index");
        }
        if (linkedObject.sectionOffsets[textSectionIndex] + relocation.offset + 4 > textLayout.bytes.size()) {
            throw std::runtime_error("relocation points outside .text");
        }

        const Symbol &symbol = object.symbols[relocation.symbolIndex];
        std::uint32_t targetRva = 0;
        if (symbol.sectionNumber > 0 &&
            static_cast<std::size_t>(symbol.sectionNumber) <= object.sections.size()) {
            const Section &section = object.sections[symbol.sectionNumber - 1];
            const auto sectionRva = sectionRvas.find(section.name);
            if (sectionRva == sectionRvas.end()) {
                throw std::runtime_error("unsupported target section in relocation: " + section.name);
            }
            targetRva = sectionRva->second + linkedObject.sectionOffsets[symbol.sectionNumber - 1] + symbol.value;
        } else if (symbol.sectionNumber == UndefinedSection && symbol.name == "ExitProcess") {
            targetRva = exitProcessThunkRva;
        } else if (symbol.sectionNumber == UndefinedSection) {
            const auto found = externalSymbols.find(symbol.name);
            if (found == externalSymbols.end()) {
                throw std::runtime_error("unresolved external symbol: " + symbol.name);
            }
            targetRva = found->second;
        } else {
            throw std::runtime_error("unsupported external symbol in relocation: " + symbol.name);
        }

        const std::uint32_t relocationRva =
            sectionRvas.at(".text") + linkedObject.sectionOffsets[textSectionIndex] + relocation.offset;
        const std::int64_t relative = static_cast<std::int64_t>(targetRva) -
            static_cast<std::int64_t>(relocationRva + 4);
        if (relative < INT32_MIN || relative > INT32_MAX) {
            throw std::runtime_error("REL32 relocation out of range");
        }
        write32(
            textLayout.bytes,
            linkedObject.sectionOffsets[textSectionIndex] + relocation.offset,
            static_cast<std::uint32_t>(static_cast<std::int32_t>(relative)));
    }
}

void appendImportThunk(std::vector<std::uint8_t> &text, std::uint32_t thunkRva, std::uint32_t iatRva) {
    text.push_back(0xff);
    text.push_back(0x25);
    const std::int64_t displacement = static_cast<std::int64_t>(iatRva) -
        static_cast<std::int64_t>(thunkRva + 6);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        throw std::runtime_error("import thunk displacement out of range");
    }
    append32(text, static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));
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
        throw std::runtime_error("no object files to link");
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
                throw std::runtime_error("truncated section " + section.name + " in " + linkedObject.object.path.string());
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

std::unordered_map<std::string, std::uint32_t> collectExternalSymbols(
    const std::vector<LinkedObject> &linkedObjects,
    const std::unordered_map<std::string, std::uint32_t> &sectionRvas) {
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
                throw std::runtime_error("duplicate external symbol: " + symbol.name);
            }
        }
    }

    return symbols;
}

} // namespace

void PeLinker::linkSingleObject(const fs::path &objPath, const fs::path &exePath) {
    linkObjects({objPath}, exePath);
}

void PeLinker::linkObjects(const std::vector<fs::path> &objPaths, const fs::path &exePath) {
    std::vector<LinkedObject> linkedObjects = readLinkedObjects(objPaths);
    std::vector<SectionLayout> sections = mergeSections(linkedObjects);
    auto textLayout = std::find_if(
        sections.begin(),
        sections.end(),
        [](const SectionLayout &layout) { return layout.name == ".text"; });
    if (textLayout == sections.end()) {
        throw std::runtime_error("linked objects do not contain a .text section");
    }

    const std::uint32_t thunkRva = FirstSectionRva + static_cast<std::uint32_t>(textLayout->bytes.size());
    appendImportThunk(textLayout->bytes, thunkRva, 0);
    textLayout->virtualSize = static_cast<std::uint32_t>(textLayout->bytes.size());

    std::uint32_t nextRva = FirstSectionRva;
    std::unordered_map<std::string, std::uint32_t> sectionRvas;
    for (auto &section : sections) {
        section.rva = nextRva;
        nextRva = alignTo(nextRva + section.virtualSize, SectionAlignment);
        sectionRvas.emplace(section.name, section.rva);
    }

    const std::uint32_t idataRva = nextRva;
    ImportLayout imports = buildImportLayout(idataRva);

    const std::int64_t thunkDisplacement = static_cast<std::int64_t>(imports.iatRva) -
        static_cast<std::int64_t>(thunkRva + 6);
    write32(
        textLayout->bytes,
        textLayout->bytes.size() - 4,
        static_cast<std::uint32_t>(static_cast<std::int32_t>(thunkDisplacement)));

    const std::unordered_map<std::string, std::uint32_t> externalSymbols =
        collectExternalSymbols(linkedObjects, sectionRvas);

    const auto entry = externalSymbols.find("mainCRTStartup");
    if (entry == externalSymbols.end()) {
        throw std::runtime_error("missing entry symbol: mainCRTStartup");
    }
    const std::uint32_t entryRva = entry->second;
    for (const auto &linkedObject : linkedObjects) {
        applyRelocations(linkedObject, sectionRvas, externalSymbols, *textLayout, thunkRva);
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
    append16(file, 0x8160);
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
