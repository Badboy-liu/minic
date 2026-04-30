#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t MzSignature = 0x5A4D;
constexpr std::uint32_t PeSignature = 0x00004550;
constexpr std::uint16_t Pe32PlusMagic = 0x20B;
constexpr std::uint32_t DirectoryEntryBaseReloc = 5;
constexpr std::uint16_t ImageRelBasedDir64 = 10;

struct SectionHeader {
    std::string name;
    std::uint32_t virtualSize = 0;
    std::uint32_t virtualAddress = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t rawPointer = 0;
};

struct ProbeConfig {
    std::string exePath;
    bool expectReloc = false;
    bool expectRelocSpecified = false;
    std::uint64_t delta = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sites;
};

std::uint16_t read16(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("truncated 16-bit read");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t read32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("truncated 32-bit read");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t read64(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) {
        throw std::runtime_error("truncated 64-bit read");
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

std::vector<std::uint8_t> readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path);
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::uint64_t parseInteger(const std::string &text) {
    std::size_t consumed = 0;
    const std::uint64_t value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
        throw std::runtime_error("invalid integer: " + text);
    }
    return value;
}

ProbeConfig parseArgs(int argc, char **argv) {
    if (argc < 2) {
        throw std::runtime_error(
            "usage: pe_reloc_probe <exe> --expect-reloc yes|no [--delta 0x100000] [--site 0x2000=0x3000]");
    }

    ProbeConfig config;
    config.exePath = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--expect-reloc") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --expect-reloc");
            }
            const std::string value = argv[++i];
            if (value == "yes") {
                config.expectReloc = true;
            } else if (value == "no") {
                config.expectReloc = false;
            } else {
                throw std::runtime_error("invalid --expect-reloc value: " + value);
            }
            config.expectRelocSpecified = true;
            continue;
        }
        if (arg == "--delta") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --delta");
            }
            config.delta = parseInteger(argv[++i]);
            continue;
        }
        if (arg == "--site") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for --site");
            }
            const std::string spec = argv[++i];
            const std::size_t equals = spec.find('=');
            if (equals == std::string::npos) {
                throw std::runtime_error("invalid --site format: " + spec);
            }
            config.sites.emplace_back(
                static_cast<std::uint32_t>(parseInteger(spec.substr(0, equals))),
                static_cast<std::uint32_t>(parseInteger(spec.substr(equals + 1))));
            continue;
        }
        throw std::runtime_error("unknown argument: " + arg);
    }

    if (!config.expectRelocSpecified) {
        throw std::runtime_error("--expect-reloc is required");
    }
    if (!config.expectReloc && (!config.sites.empty() || config.delta != 0)) {
        throw std::runtime_error("--delta/--site require --expect-reloc yes");
    }
    return config;
}

std::size_t rvaToFileOffset(const std::vector<SectionHeader> &sections, std::uint32_t rva) {
    for (const auto &section : sections) {
        const std::uint32_t extent = std::max(section.virtualSize, section.rawSize);
        if (rva >= section.virtualAddress && rva < section.virtualAddress + extent) {
            const std::uint32_t delta = rva - section.virtualAddress;
            if (delta >= section.rawSize) {
                throw std::runtime_error("RVA points beyond section raw data");
            }
            return static_cast<std::size_t>(section.rawPointer + delta);
        }
    }
    throw std::runtime_error("RVA does not map to a file section");
}

} // namespace

int main(int argc, char **argv) {
    try {
        const ProbeConfig config = parseArgs(argc, argv);
        const std::vector<std::uint8_t> bytes = readFile(config.exePath);

        if (read16(bytes, 0) != MzSignature) {
            throw std::runtime_error("missing MZ signature");
        }
        const std::uint32_t peOffset = read32(bytes, 0x3C);
        if (read32(bytes, peOffset) != PeSignature) {
            throw std::runtime_error("missing PE signature");
        }

        const std::size_t coffOffset = peOffset + 4;
        const std::uint16_t sectionCount = read16(bytes, coffOffset + 2);
        const std::uint16_t optionalHeaderSize = read16(bytes, coffOffset + 16);
        const std::size_t optionalHeaderOffset = coffOffset + 20;
        if (read16(bytes, optionalHeaderOffset) != Pe32PlusMagic) {
            throw std::runtime_error("expected PE32+ optional header");
        }

        const std::uint64_t imageBase = read64(bytes, optionalHeaderOffset + 24);
        const std::uint32_t numberOfRvaAndSizes = read32(bytes, optionalHeaderOffset + 108);
        if (numberOfRvaAndSizes <= DirectoryEntryBaseReloc) {
            throw std::runtime_error("PE optional header does not expose base relocation directory");
        }
        const std::size_t dataDirectoryOffset = optionalHeaderOffset + 112;
        const std::uint32_t baseRelocRva = read32(bytes, dataDirectoryOffset + DirectoryEntryBaseReloc * 8);
        const std::uint32_t baseRelocSize = read32(bytes, dataDirectoryOffset + DirectoryEntryBaseReloc * 8 + 4);

        std::vector<SectionHeader> sections;
        sections.reserve(sectionCount);
        std::size_t sectionHeaderOffset = optionalHeaderOffset + optionalHeaderSize;
        for (std::uint16_t i = 0; i < sectionCount; ++i) {
            const std::size_t offset = sectionHeaderOffset + static_cast<std::size_t>(i) * 40;
            std::string name;
            for (int j = 0; j < 8 && bytes[offset + j] != 0; ++j) {
                name.push_back(static_cast<char>(bytes[offset + j]));
            }
            sections.push_back(SectionHeader{
                name,
                read32(bytes, offset + 8),
                read32(bytes, offset + 12),
                read32(bytes, offset + 16),
                read32(bytes, offset + 20)});
        }

        auto relocSectionIt = std::find_if(
            sections.begin(),
            sections.end(),
            [](const SectionHeader &section) { return section.name == ".reloc"; });
        const bool hasRelocSection = relocSectionIt != sections.end() && relocSectionIt->rawSize > 0;
        const bool hasRelocDirectory = baseRelocRva != 0 && baseRelocSize != 0;

        if (!config.expectReloc) {
            if (hasRelocDirectory || hasRelocSection) {
                throw std::runtime_error("unexpected base relocation data for image that should not need rebasing");
            }
            std::cout << "no reloc expected and none found\n";
            return 0;
        }

        if (!hasRelocDirectory) {
            throw std::runtime_error("missing PE base relocation directory");
        }
        if (!hasRelocSection) {
            throw std::runtime_error("missing .reloc section");
        }

        const std::size_t relocOffset = rvaToFileOffset(sections, baseRelocRva);
        if (relocOffset + baseRelocSize > bytes.size()) {
            throw std::runtime_error(".reloc directory points outside file");
        }

        std::vector<std::uint32_t> dir64Sites;
        std::size_t cursor = relocOffset;
        const std::size_t relocEnd = relocOffset + baseRelocSize;
        while (cursor < relocEnd) {
            if (cursor + 8 > relocEnd) {
                throw std::runtime_error("truncated IMAGE_BASE_RELOCATION block");
            }
            const std::uint32_t pageRva = read32(bytes, cursor);
            const std::uint32_t blockSize = read32(bytes, cursor + 4);
            if (blockSize < 8 || cursor + blockSize > relocEnd) {
                throw std::runtime_error("invalid IMAGE_BASE_RELOCATION block size");
            }
            const std::size_t entryBytes = blockSize - 8;
            if (entryBytes % 2 != 0) {
                throw std::runtime_error("unaligned IMAGE_BASE_RELOCATION entries");
            }
            for (std::size_t entryOffset = cursor + 8; entryOffset < cursor + blockSize; entryOffset += 2) {
                const std::uint16_t entry = read16(bytes, entryOffset);
                const std::uint16_t type = static_cast<std::uint16_t>(entry >> 12);
                const std::uint16_t pageOffset = static_cast<std::uint16_t>(entry & 0x0FFFu);
                if (type == 0) {
                    continue;
                }
                if (type != ImageRelBasedDir64) {
                    throw std::runtime_error("unexpected base relocation type: " + std::to_string(type));
                }
                dir64Sites.push_back(pageRva + pageOffset);
            }
            cursor += blockSize;
        }

        std::sort(dir64Sites.begin(), dir64Sites.end());
        dir64Sites.erase(std::unique(dir64Sites.begin(), dir64Sites.end()), dir64Sites.end());
        std::unordered_set<std::uint32_t> siteSet(dir64Sites.begin(), dir64Sites.end());

        for (const auto &[slotRva, targetRva] : config.sites) {
            if (siteSet.find(slotRva) == siteSet.end()) {
                throw std::runtime_error("missing DIR64 relocation site at RVA 0x" + std::to_string(slotRva));
            }
            const std::size_t slotOffset = rvaToFileOffset(sections, slotRva);
            const std::uint64_t original = read64(bytes, slotOffset);
            const std::uint64_t expectedOriginal = imageBase + targetRva;
            if (original != expectedOriginal) {
                throw std::runtime_error("unexpected original VA at slot RVA");
            }
            const std::uint64_t rebased = original + config.delta;
            const std::uint64_t expectedRebased = imageBase + config.delta + targetRva;
            if (rebased != expectedRebased) {
                throw std::runtime_error("rebased VA mismatch at slot RVA");
            }
        }

        std::cout << "reloc ok: sites=" << dir64Sites.size() << '\n';
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
