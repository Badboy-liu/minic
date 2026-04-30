#include "CoffObjectWriter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t CoffMachineAmd64 = 0x8664;
constexpr std::uint8_t CoffStorageClassExternal = 2;
constexpr std::uint8_t CoffStorageClassStatic = 3;
constexpr std::uint16_t RelocAmd64Rel32 = 0x0004;
constexpr std::uint16_t RelocAmd64Addr64 = 0x0001;

struct SectionLayout {
    std::size_t rawDataOffset = 0;
    std::size_t relocationOffset = 0;
    std::vector<std::size_t> relocationIndexes;
};

void append16(std::vector<std::uint8_t> &out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

void appendBytes(std::vector<std::uint8_t> &out, const std::vector<std::uint8_t> &bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void patch16(std::vector<std::uint8_t> &out, std::size_t offset, std::uint16_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xff);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void patch32(std::vector<std::uint8_t> &out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xff);
    out[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    out[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

std::uint16_t relocationType(ObjectRelocationKind kind) {
    switch (kind) {
    case ObjectRelocationKind::Rel32:
        return RelocAmd64Rel32;
    case ObjectRelocationKind::Addr64:
        return RelocAmd64Addr64;
    }
    throw std::runtime_error("unsupported COFF relocation kind");
}

std::uint8_t symbolStorageClass(ObjectSymbolBinding binding) {
    switch (binding) {
    case ObjectSymbolBinding::Local:
        return CoffStorageClassStatic;
    case ObjectSymbolBinding::Global:
    case ObjectSymbolBinding::Undefined:
        return CoffStorageClassExternal;
    }
    throw std::runtime_error("unsupported COFF symbol binding");
}

std::array<std::uint8_t, 8> encodeName(
    const std::string &name,
    std::vector<std::uint8_t> &stringTableBytes,
    bool allowLongName) {
    std::array<std::uint8_t, 8> encoded{};
    if (name.size() <= 8) {
        std::memcpy(encoded.data(), name.data(), name.size());
        return encoded;
    }
    if (!allowLongName) {
        throw std::runtime_error("COFF section name is too long: " + name);
    }

    const std::uint32_t stringOffset = static_cast<std::uint32_t>(4 + stringTableBytes.size());
    encoded[4] = static_cast<std::uint8_t>(stringOffset & 0xff);
    encoded[5] = static_cast<std::uint8_t>((stringOffset >> 8) & 0xff);
    encoded[6] = static_cast<std::uint8_t>((stringOffset >> 16) & 0xff);
    encoded[7] = static_cast<std::uint8_t>((stringOffset >> 24) & 0xff);

    stringTableBytes.insert(stringTableBytes.end(), name.begin(), name.end());
    stringTableBytes.push_back(0);
    return encoded;
}

std::unordered_map<std::string, std::uint32_t> buildSymbolIndexMap(const ObjectFileModel &model) {
    std::unordered_map<std::string, std::uint32_t> indexes;
    for (std::uint32_t i = 0; i < model.symbols.size(); ++i) {
        indexes.emplace(model.symbols[i].name, i);
    }
    return indexes;
}

} // namespace

std::vector<std::uint8_t> CoffObjectWriter::writeAmd64(const ObjectFileModel &model) {
    if (model.sections.empty()) {
        throw std::runtime_error("cannot write COFF object without sections");
    }

    const auto symbolIndexes = buildSymbolIndexMap(model);
    std::vector<SectionLayout> layouts(model.sections.size());

    std::size_t fileOffset = 20 + model.sections.size() * 40;
    for (std::size_t i = 0; i < model.sections.size(); ++i) {
        layouts[i].rawDataOffset = fileOffset;
        if (!model.sections[i].isBss) {
            fileOffset += model.sections[i].bytes.size();
        }
    }

    for (std::size_t i = 0; i < model.relocations.size(); ++i) {
        const auto &relocation = model.relocations[i];
        if (relocation.sectionIndex <= 0 || relocation.sectionIndex > static_cast<int>(model.sections.size())) {
            throw std::runtime_error("COFF relocation references invalid section index");
        }
        layouts[static_cast<std::size_t>(relocation.sectionIndex - 1)].relocationIndexes.push_back(i);
    }

    for (auto &layout : layouts) {
        layout.relocationOffset = fileOffset;
        fileOffset += layout.relocationIndexes.size() * 10;
    }

    const std::uint32_t symbolTableOffset = static_cast<std::uint32_t>(fileOffset);
    const std::uint32_t symbolCount = static_cast<std::uint32_t>(model.symbols.size());

    std::vector<std::uint8_t> file;
    file.reserve(symbolTableOffset + symbolCount * 18 + 64);

    append16(file, CoffMachineAmd64);
    append16(file, static_cast<std::uint16_t>(model.sections.size()));
    append32(file, 0);
    append32(file, symbolTableOffset);
    append32(file, symbolCount);
    append16(file, 0);
    append16(file, 0);

    const std::size_t sectionTableOffset = file.size();
    file.resize(sectionTableOffset + model.sections.size() * 40, 0);

    for (std::size_t i = 0; i < model.sections.size(); ++i) {
        const auto &section = model.sections[i];
        if (!section.isBss) {
            appendBytes(file, section.bytes);
        }
    }

    for (std::size_t i = 0; i < model.sections.size(); ++i) {
        const auto &layout = layouts[i];
        for (const std::size_t relocationIndex : layout.relocationIndexes) {
            const auto &relocation = model.relocations[relocationIndex];
            const auto foundSymbol = symbolIndexes.find(relocation.targetSymbol);
            if (foundSymbol == symbolIndexes.end()) {
                throw std::runtime_error("COFF relocation references unknown symbol: " + relocation.targetSymbol);
            }
            append32(file, relocation.offset);
            append32(file, foundSymbol->second);
            append16(file, relocationType(relocation.kind));
        }
    }

    std::vector<std::uint8_t> stringTableBytes;
    for (std::size_t i = 0; i < model.sections.size(); ++i) {
        const auto &section = model.sections[i];
        const auto encodedName = encodeName(section.name, stringTableBytes, false);
        const std::size_t offset = sectionTableOffset + i * 40;
        std::copy(encodedName.begin(), encodedName.end(), file.begin() + static_cast<std::ptrdiff_t>(offset));
        patch32(file, offset + 8, section.isBss ? section.virtualSize : static_cast<std::uint32_t>(section.bytes.size()));
        patch32(file, offset + 16, section.isBss ? 0 : static_cast<std::uint32_t>(section.bytes.size()));
        patch32(file, offset + 20, section.isBss ? 0 : static_cast<std::uint32_t>(layouts[i].rawDataOffset));
        patch32(file, offset + 24, layouts[i].relocationIndexes.empty() ? 0 : static_cast<std::uint32_t>(layouts[i].relocationOffset));
        patch16(file, offset + 32, static_cast<std::uint16_t>(layouts[i].relocationIndexes.size()));
        patch32(file, offset + 36, section.characteristics);
    }

    for (const auto &symbol : model.symbols) {
        const auto encodedName = encodeName(symbol.name, stringTableBytes, true);
        file.insert(file.end(), encodedName.begin(), encodedName.end());
        append32(file, symbol.value);
        append16(file, symbol.binding == ObjectSymbolBinding::Undefined ? 0 : static_cast<std::uint16_t>(symbol.sectionIndex));
        append16(file, 0);
        file.push_back(symbolStorageClass(symbol.binding));
        file.push_back(0);
    }

    append32(file, static_cast<std::uint32_t>(4 + stringTableBytes.size()));
    appendBytes(file, stringTableBytes);

    return file;
}
