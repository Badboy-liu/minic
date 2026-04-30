#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ObjectSectionKind {
    Text,
    Data,
    ReadOnlyData,
    Bss
};

enum class ObjectRelocationKind {
    Rel32,
    Addr64
};

enum class ObjectSymbolBinding {
    Local,
    Global,
    Undefined
};

struct ObjectSection {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::uint32_t characteristics = 0;
    std::uint32_t alignment = 1;
    std::uint32_t virtualSize = 0;
    bool isBss = false;
    ObjectSectionKind kind = ObjectSectionKind::Text;
};

struct ObjectSymbol {
    std::string name;
    int sectionIndex = 0;
    std::uint32_t value = 0;
    ObjectSymbolBinding binding = ObjectSymbolBinding::Local;
};

struct ObjectRelocation {
    int sectionIndex = 0;
    std::uint32_t offset = 0;
    std::string targetSymbol;
    ObjectRelocationKind kind = ObjectRelocationKind::Rel32;
};

struct ObjectFileModel {
    std::vector<ObjectSection> sections;
    std::vector<ObjectSymbol> symbols;
    std::vector<ObjectRelocation> relocations;
};
