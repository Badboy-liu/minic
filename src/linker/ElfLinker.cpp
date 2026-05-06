#include "ElfLinker.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
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

// ELF 常量
constexpr std::uint8_t ElfClass64 = 2;
constexpr std::uint8_t ElfDataLittleEndian = 1;
constexpr std::uint8_t ElfOsAbiSysv = 0;
constexpr std::uint16_t ElfTypeExec = 2;
constexpr std::uint16_t ElfMachineX86_64 = 62;

// 节类型
constexpr std::uint32_t ShtNull = 0;
constexpr std::uint32_t ShtProgbits = 1;
constexpr std::uint32_t ShtSymtab = 2;
constexpr std::uint32_t ShtStrtab = 3;
constexpr std::uint32_t ShtRela = 4;
constexpr std::uint32_t ShtNobits = 8;

// 节标志
constexpr std::uint64_t ShfWrite = 0x1;
constexpr std::uint64_t ShfAlloc = 0x2;
constexpr std::uint64_t ShfExecinstr = 0x4;

// 节名称索引
constexpr std::uint32_t ShnUndefined = 0;
constexpr std::uint32_t ShnAbsolute = 0xfff1;

// 符号绑定
constexpr std::uint8_t StbLocal = 0;
constexpr std::uint8_t StbGlobal = 1;
constexpr std::uint8_t StbWeak = 2;

// 符号类型
constexpr std::uint8_t SttNotype = 0;
constexpr std::uint8_t SttFunc = 2;
constexpr std::uint8_t SttSection = 3;

// 重定位类型
constexpr std::uint32_t R_X86_64_64 = 1;
constexpr std::uint32_t R_X86_64_PC32 = 2;
constexpr std::uint32_t R_X86_64_PLT32 = 4;

// PT_LOAD 段类型
constexpr std::uint32_t PtLoad = 1;
constexpr std::uint32_t PtNull = 0;

// PF_R, PF_W, PF_X
constexpr std::uint32_t PfR = 0x4;
constexpr std::uint32_t PfW = 0x2;
constexpr std::uint32_t PfX = 0x1;

// 默认基地址
constexpr std::uint64_t DefaultBaseAddr = 0x400000;
constexpr std::uint64_t PageAlignment = 0x1000;

#pragma pack(push, 1)

struct Elf64_Ehdr {
    std::uint8_t e_ident[16];
    std::uint16_t e_type;
    std::uint16_t e_machine;
    std::uint32_t e_version;
    std::uint64_t e_entry;
    std::uint64_t e_phoff;
    std::uint64_t e_shoff;
    std::uint32_t e_flags;
    std::uint16_t e_ehsize;
    std::uint16_t e_phentsize;
    std::uint16_t e_phnum;
    std::uint16_t e_shentsize;
    std::uint16_t e_shnum;
    std::uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    std::uint32_t sh_name;
    std::uint32_t sh_type;
    std::uint64_t sh_flags;
    std::uint64_t sh_addr;
    std::uint64_t sh_offset;
    std::uint64_t sh_size;
    std::uint32_t sh_link;
    std::uint32_t sh_info;
    std::uint64_t sh_addralign;
    std::uint64_t sh_entsize;
};

struct Elf64_Sym {
    std::uint32_t st_name;
    std::uint8_t st_info;
    std::uint8_t st_other;
    std::uint16_t st_shndx;
    std::uint64_t st_value;
    std::uint64_t st_size;
};

struct Elf64_Rela {
    std::uint64_t r_offset;
    std::uint64_t r_info;
    std::int64_t r_addend;
};

struct Elf64_Phdr {
    std::uint32_t p_type;
    std::uint32_t p_flags;
    std::uint64_t p_offset;
    std::uint64_t p_vaddr;
    std::uint64_t p_paddr;
    std::uint64_t p_filesz;
    std::uint64_t p_memsz;
    std::uint64_t p_align;
};

#pragma pack(pop)

// 对象文件中的节信息
struct ObjSection {
    std::string name;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t addralign = 0;
    std::vector<std::uint8_t> data;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t entsize = 0;
    std::size_t sectionIndex = 0;
};

// 对象文件中的符号
struct ObjSymbol {
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint8_t info = 0;
    std::uint16_t shndx = 0;
    std::size_t sectionIndex = 0;
    std::size_t symbolIndex = 0;
};

// 对象文件中的重定位
struct ObjRelocation {
    std::uint64_t offset = 0;
    std::uint32_t type = 0;
    std::uint32_t symbolIndex = 0;
    std::int64_t addend = 0;
    std::size_t targetSectionIndex = 0;
};

struct ObjectFile {
    fs::path path;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol> symbols;
    std::vector<std::vector<ObjRelocation>> relocations; // 每节一组重定位
    std::vector<std::uint8_t> bytes;
};

// 输出节布局
struct OutputSection {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint64_t virtualAddr = 0;
    std::uint64_t fileOffset = 0;
    std::uint64_t flags = 0;
    std::uint64_t addralign = 1;
};

struct LinkedObject {
    ObjectFile object;
    std::vector<std::uint32_t> sectionOffsets;
};

std::uint32_t alignTo(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::uint64_t alignTo64(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::string linkerDiagnostic(const std::string &detail) {
    return "linker diagnostic: " + detail;
}

std::string objectDiagnostic(const fs::path &path, const std::string &detail) {
    return linkerDiagnostic(path.string() + ": " + detail);
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

std::string countLabel(std::size_t count, const char *singular, const char *plural) {
    std::ostringstream out;
    out << count << " " << (count == 1 ? singular : plural);
    return out.str();
}

std::uint16_t read16(const std::vector<std::uint8_t> &b, std::size_t off) {
    return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

std::uint32_t read32(const std::vector<std::uint8_t> &b, std::size_t off) {
    return static_cast<std::uint32_t>(
        b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24));
}

std::uint64_t read64(const std::vector<std::uint8_t> &b, std::size_t off) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(b[off + i]) << (i * 8);
    }
    return value;
}

std::int32_t readS32(const std::vector<std::uint8_t> &b, std::size_t off) {
    return static_cast<std::int32_t>(read32(b, off));
}

std::int64_t readS64(const std::vector<std::uint8_t> &b, std::size_t off) {
    return static_cast<std::int64_t>(read64(b, off));
}

void write16(std::vector<std::uint8_t> &b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<std::uint8_t>(v & 0xff);
    b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void write32(std::vector<std::uint8_t> &b, std::size_t off, std::uint32_t v) {
    b[off] = static_cast<std::uint8_t>(v & 0xff);
    b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
    b[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xff);
    b[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xff);
}

void write64(std::vector<std::uint8_t> &b, std::size_t off, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        b[off + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xff);
    }
}

void append16(std::vector<std::uint8_t> &b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void append32(std::vector<std::uint8_t> &b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

void append64(std::vector<std::uint8_t> &b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
    }
}

std::uint8_t elf64StBind(std::uint8_t info) { return info >> 4; }
std::uint8_t elf64StType(std::uint8_t info) { return info & 0xf; }
std::uint8_t elf64StInfo(std::uint8_t bind, std::uint8_t type) {
    return static_cast<std::uint8_t>((bind << 4) | (type & 0xf));
}
std::uint32_t elf64RSym(std::uint64_t info) {
    return static_cast<std::uint32_t>(info & 0xffffffff);
}
std::uint32_t elf64RType(std::uint64_t info) {
    return static_cast<std::uint32_t>(info >> 32);
}

unsigned int objectReadWorkerCount(std::size_t taskCount, unsigned int requestedJobs) {
    if (taskCount <= 1) return 1;
    const unsigned int detected = std::thread::hardware_concurrency();
    const unsigned int fallback = 4;
    const unsigned int available = requestedJobs == 0 ? (detected == 0 ? fallback : detected) : requestedJobs;
    return static_cast<unsigned int>(std::min<std::size_t>(taskCount, available));
}

// 读取 ELF 节名称字符串表
std::string readElfString(const std::vector<std::uint8_t> &bytes, std::size_t strtabOffset, std::uint32_t nameIdx) {
    std::size_t off = strtabOffset + nameIdx;
    if (off >= bytes.size()) return "";
    std::string result;
    for (std::size_t i = off; i < bytes.size() && bytes[i] != 0; ++i) {
        result.push_back(static_cast<char>(bytes[i]));
    }
    return result;
}

ObjectFile readElfObject(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(objectDiagnostic(path, "failed to open object file"));
    }

    ObjectFile obj;
    obj.path = path;
    obj.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    if (obj.bytes.size() < sizeof(Elf64_Ehdr)) {
        throw std::runtime_error(objectDiagnostic(path, "file too small for ELF header"));
    }

    const auto &ehdr = *reinterpret_cast<const Elf64_Ehdr *>(obj.bytes.data());
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        throw std::runtime_error(objectDiagnostic(path, "not an ELF file"));
    }
    if (ehdr.e_ident[4] != ElfClass64) {
        throw std::runtime_error(objectDiagnostic(path, "not a 64-bit ELF file"));
    }
    if (ehdr.e_ident[5] != ElfDataLittleEndian) {
        throw std::runtime_error(objectDiagnostic(path, "not a little-endian ELF file"));
    }
    if (ehdr.e_type != 1) { // ET_REL
        throw std::runtime_error(objectDiagnostic(path, "not a relocatable ELF object (ET_REL expected)"));
    }
    if (ehdr.e_machine != ElfMachineX86_64) {
        throw std::runtime_error(objectDiagnostic(path, "not an x86-64 ELF object"));
    }

    const std::uint16_t shnum = ehdr.e_shnum;
    const std::uint16_t shstrndx = ehdr.e_shstrndx;
    const std::size_t shoff = ehdr.e_shoff;
    const std::uint16_t shentsize = ehdr.e_shentsize;

    if (shoff + static_cast<std::size_t>(shnum) * shentsize > obj.bytes.size()) {
        throw std::runtime_error(objectDiagnostic(path, "truncated ELF section header table"));
    }

    // 读取节头
    std::vector<Elf64_Shdr> shdrs(shnum);
    for (std::uint16_t i = 0; i < shnum; ++i) {
        const std::size_t off = shoff + static_cast<std::size_t>(i) * shentsize;
        if (off + sizeof(Elf64_Shdr) > obj.bytes.size()) {
            throw std::runtime_error(objectDiagnostic(path, "truncated section header"));
        }
        std::memcpy(&shdrs[i], obj.bytes.data() + off, sizeof(Elf64_Shdr));
    }

    // 节名称字符串表
    std::size_t shstrtabOff = 0;
    if (shstrndx < shnum) {
        shstrtabOff = static_cast<std::size_t>(shdrs[shstrndx].sh_offset);
    }

    // 查找符号表和关联的字符串表
    std::size_t symtabIdx = 0;
    std::size_t strtabIdx = 0;
    for (std::uint16_t i = 0; i < shnum; ++i) {
        std::string name = readElfString(obj.bytes, shstrtabOff, shdrs[i].sh_name);
        if (shdrs[i].sh_type == ShtSymtab && name == ".symtab") {
            symtabIdx = i;
            strtabIdx = shdrs[i].sh_link;
        }
    }

    // 读取节
    obj.sections.resize(shnum);
    obj.relocations.resize(shnum);
    for (std::uint16_t i = 0; i < shnum; ++i) {
        ObjSection &sec = obj.sections[i];
        sec.name = readElfString(obj.bytes, shstrtabOff, shdrs[i].sh_name);
        sec.type = shdrs[i].sh_type;
        sec.flags = shdrs[i].sh_flags;
        sec.addralign = shdrs[i].sh_addralign;
        sec.link = shdrs[i].sh_link;
        sec.info = shdrs[i].sh_info;
        sec.entsize = shdrs[i].sh_entsize;
        sec.sectionIndex = i;

        if (shdrs[i].sh_type == ShtNull || shdrs[i].sh_type == ShtStrtab ||
            shdrs[i].sh_type == ShtSymtab) {
            continue;
        }

        const std::size_t size = shdrs[i].sh_size;
        const std::size_t offset = shdrs[i].sh_offset;
        if (offset + size > obj.bytes.size()) {
            throw std::runtime_error(objectDiagnostic(path, "truncated section data: " + sec.name));
        }
        if (shdrs[i].sh_type != ShtNobits) {
            sec.data.assign(obj.bytes.begin() + offset, obj.bytes.begin() + offset + size);
        }
    }

    // 读取符号表
    if (symtabIdx > 0 && symtabIdx < shnum) {
        const auto &symShdr = shdrs[symtabIdx];
        const std::size_t symCount = symShdr.sh_size / sizeof(Elf64_Sym);
        const std::size_t symOff = symShdr.sh_offset;
        const std::size_t strSecOff = shdrs[strtabIdx].sh_offset;

        obj.symbols.resize(symCount);
        for (std::size_t i = 0; i < symCount; ++i) {
            const std::size_t off = symOff + i * sizeof(Elf64_Sym);
            if (off + sizeof(Elf64_Sym) > obj.bytes.size()) {
                throw std::runtime_error(objectDiagnostic(path, "truncated symbol table"));
            }
            Elf64_Sym sym;
            std::memcpy(&sym, obj.bytes.data() + off, sizeof(Elf64_Sym));
            obj.symbols[i].name = readElfString(obj.bytes, strSecOff, sym.st_name);
            obj.symbols[i].value = sym.st_value;
            obj.symbols[i].size = sym.st_size;
            obj.symbols[i].info = sym.st_info;
            obj.symbols[i].shndx = sym.st_shndx;
            obj.symbols[i].symbolIndex = i;
        }
    }

    // 读取重定位节
    for (std::uint16_t i = 0; i < shnum; ++i) {
        if (shdrs[i].sh_type != ShtRela) continue;
        const std::uint32_t targetSecIdx = shdrs[i].sh_info;
        if (targetSecIdx >= shnum) continue;
        const std::size_t relCount = shdrs[i].sh_size / sizeof(Elf64_Rela);
        const std::size_t relOff = shdrs[i].sh_offset;

        auto &rels = obj.relocations[targetSecIdx];
        for (std::size_t j = 0; j < relCount; ++j) {
            const std::size_t off = relOff + j * sizeof(Elf64_Rela);
            if (off + sizeof(Elf64_Rela) > obj.bytes.size()) {
                throw std::runtime_error(objectDiagnostic(path, "truncated relocation table"));
            }
            Elf64_Rela rela;
            std::memcpy(&rela, obj.bytes.data() + off, sizeof(Elf64_Rela));
            ObjRelocation rel;
            rel.offset = rela.r_offset;
            rel.type = elf64RType(rela.r_info);
            rel.symbolIndex = elf64RSym(rela.r_info);
            rel.addend = rela.r_addend;
            rel.targetSectionIndex = targetSecIdx;
            rels.push_back(rel);
        }
    }

    return obj;
}

std::vector<LinkedObject> readLinkedObjects(const std::vector<fs::path> &objPaths, unsigned int jobs) {
    if (objPaths.empty()) {
        throw std::runtime_error(linkerDiagnostic("no object files to link"));
    }

    std::vector<LinkedObject> linked;
    linked.reserve(objPaths.size());

    if (objectReadWorkerCount(objPaths.size(), jobs) <= 1) {
        for (const auto &path : objPaths) {
            LinkedObject lo;
            lo.object = readElfObject(path);
            lo.sectionOffsets.resize(lo.object.sections.size(), 0);
            linked.push_back(std::move(lo));
        }
        return linked;
    }

    std::vector<std::future<LinkedObject>> futures;
    for (const auto &path : objPaths) {
        futures.push_back(std::async(std::launch::async, [path]() {
            LinkedObject lo;
            lo.object = readElfObject(path);
            lo.sectionOffsets.resize(lo.object.sections.size(), 0);
            return lo;
        }));
    }
    for (auto &f : futures) {
        linked.push_back(f.get());
    }
    return linked;
}

// 收集所有全局/弱符号定义
struct GlobalSymbolDef {
    std::string name;
    std::uint64_t value = 0; // 节内偏移
    std::uint16_t shndx = 0;
    std::size_t objectIndex = 0;
    std::size_t symbolIndex = 0;
    std::uint8_t bind = 0;
    bool isFunc = false;
};

std::unordered_map<std::string, GlobalSymbolDef> collectGlobalSymbols(
    const std::vector<LinkedObject> &linked) {
    std::unordered_map<std::string, GlobalSymbolDef> globals;

    for (std::size_t oi = 0; oi < linked.size(); ++oi) {
        const auto &obj = linked[oi].object;
        for (std::size_t si = 0; si < obj.symbols.size(); ++si) {
            const auto &sym = obj.symbols[si];
            const std::uint8_t bind = elf64StBind(sym.info);
            if (bind != StbGlobal && bind != StbWeak) continue;
            if (sym.shndx == ShnUndefined) continue;
            if (sym.shndx == ShnAbsolute) continue;
            if (sym.name.empty()) continue;

            auto it = globals.find(sym.name);
            if (it == globals.end()) {
                globals[sym.name] = GlobalSymbolDef{
                    sym.name, sym.value, sym.shndx, oi, si, bind,
                    elf64StType(sym.info) == SttFunc};
            } else {
                // 优先选择强符号（GLOBAL）而非弱符号（WEAK）
                if (it->second.bind == StbWeak && bind == StbGlobal) {
                    globals[sym.name] = GlobalSymbolDef{
                        sym.name, sym.value, sym.shndx, oi, si, bind,
                        elf64StType(sym.info) == SttFunc};
                }
            }
        }
    }
    return globals;
}

// 合并同类节
struct MergedSection {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint64_t virtualAddr = 0;
    std::uint64_t fileOffset = 0;
    std::uint64_t flags = 0;
    std::uint64_t addralign = 1;
    bool isBss = false;
};

std::vector<MergedSection> mergeSections(
    std::vector<LinkedObject> &linked,
    std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::uint32_t>> &sectionMap) {
    // sectionMap[objIdx][objSecIdx] = mergedSectionIdx

    std::vector<MergedSection> merged;
    // 按名称查找或创建合并节
    auto findOrCreate = [&](const std::string &name, std::uint64_t flags, std::uint64_t align, bool isBss) -> std::size_t {
        for (std::size_t i = 0; i < merged.size(); ++i) {
            if (merged[i].name == name) return i;
        }
        merged.push_back(MergedSection{name, {}, 0, 0, flags, align, isBss});
        return merged.size() - 1;
    };

    for (std::size_t oi = 0; oi < linked.size(); ++oi) {
        auto &obj = linked[oi].object;
        sectionMap[oi].clear();
        for (std::size_t si = 0; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            if (sec.type == ShtNull || sec.type == ShtSymtab ||
                sec.type == ShtStrtab || sec.type == ShtRela) {
                continue;
            }
            if (sec.name.empty() || sec.name[0] == '.') {
                // 处理 .text, .data, .rodata, .bss
                if (sec.name != ".text" && sec.name != ".data" &&
                    sec.name != ".rodata" && sec.name != ".bss") {
                    continue;
                }
            }

            const bool isBss = (sec.type == ShtNobits);
            std::uint64_t align = sec.addralign > 0 ? sec.addralign : 1;
            std::size_t mi = findOrCreate(sec.name, sec.flags, align, isBss);
            auto &ms = merged[mi];

            // 对齐
            if (align > 1) {
                ms.data.resize(alignTo64(ms.data.size(), align), 0);
            }

            linked[oi].sectionOffsets[si] = static_cast<std::uint32_t>(ms.data.size());
            sectionMap[oi][si] = static_cast<std::uint32_t>(mi);

            if (!isBss) {
                ms.data.insert(ms.data.end(), sec.data.begin(), sec.data.end());
            }
            if (align > ms.addralign) {
                ms.addralign = align;
            }
            // 更新 flags（合并写/分配/执行标志）
            ms.flags = sec.flags;
        }
    }

    // 移除空节
    // 不移除，因为需要保持索引稳定
    return merged;
}

// 重定位追踪
struct RelocationTrace {
    fs::path objectPath;
    std::string sectionName;
    std::uint64_t offset = 0;
    std::string typeName;
    std::string targetName;
    std::uint64_t targetAddr = 0;
    std::int64_t addend = 0;
    std::int64_t computed = 0;
};

void applyRelocations(
    std::vector<LinkedObject> &linked,
    const std::vector<MergedSection> &merged,
    const std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::uint32_t>> &sectionMap,
    const std::unordered_map<std::string, GlobalSymbolDef> &globals,
    std::vector<RelocationTrace> *trace) {

    for (std::size_t oi = 0; oi < linked.size(); ++oi) {
        const auto &obj = linked[oi].object;
        for (std::size_t si = 0; si < obj.sections.size(); ++si) {
            if (obj.relocations[si].empty()) continue;
            auto secMapIt = sectionMap.find(oi);
            if (secMapIt == sectionMap.end()) continue;
            auto mergedIt = secMapIt->second.find(si);
            if (mergedIt == secMapIt->second.end()) continue;

            const std::size_t mi = mergedIt->second;
            auto &ms = const_cast<MergedSection &>(merged[mi]); // NOLINT
            const std::uint32_t secOffset = linked[oi].sectionOffsets[si];

            for (const auto &rel : obj.relocations[si]) {
                if (rel.symbolIndex >= obj.symbols.size()) {
                    throw std::runtime_error(objectDiagnostic(obj.path, "invalid symbol index in relocation"));
                }
                const auto &sym = obj.symbols[rel.symbolIndex];
                const std::uint8_t bind = elf64StBind(sym.info);

                std::uint64_t targetAddr = 0;
                std::string targetName;

                if (sym.shndx == ShnUndefined) {
                    // 外部符号
                    auto git = globals.find(sym.name);
                    if (git == globals.end()) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "unresolved symbol: " + sym.name));
                    }
                    const auto &def = git->second;
                    auto defSecMap = sectionMap.find(def.objectIndex);
                    if (defSecMap == sectionMap.end()) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "unresolved symbol (no section map): " + sym.name));
                    }
                    auto defMergedIt = defSecMap->second.find(def.shndx);
                    if (defMergedIt == defSecMap->second.end()) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "unresolved symbol (no merged section): " + sym.name));
                    }
                    const std::size_t defMi = defMergedIt->second;
                    targetAddr = merged[defMi].virtualAddr +
                        linked[def.objectIndex].sectionOffsets[def.shndx] + def.value;
                    targetName = sym.name;
                } else if (sym.shndx == ShnAbsolute) {
                    targetAddr = sym.value;
                    targetName = sym.name;
                } else if (sym.shndx > 0 && sym.shndx < obj.sections.size()) {
                    // 本地符号
                    auto secIt = secMapIt->second.find(sym.shndx);
                    if (secIt == secMapIt->second.end()) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "relocation references non-output section: " + sym.name));
                    }
                    const std::size_t symMi = secIt->second;
                    targetAddr = merged[symMi].virtualAddr +
                        linked[oi].sectionOffsets[sym.shndx] + sym.value;
                    targetName = sym.name.empty() ? ("section_" + std::to_string(sym.shndx)) : sym.name;
                } else {
                    throw std::runtime_error(
                        objectDiagnostic(obj.path, "unsupported symbol section index in relocation: " + sym.name));
                }

                const std::uint64_t patchOffset = secOffset + rel.offset;
                const std::int64_t fullTarget = static_cast<std::int64_t>(targetAddr) + rel.addend;

                switch (rel.type) {
                case R_X86_64_64: {
                    if (patchOffset + 8 > ms.data.size()) {
                        throw std::runtime_error(objectDiagnostic(obj.path, "R_X86_64_64 relocation out of bounds"));
                    }
                    write64(ms.data, patchOffset, static_cast<std::uint64_t>(fullTarget));
                    if (trace) {
                        trace->push_back({obj.path, ms.name, rel.offset, "R_X86_64_64",
                            targetName, static_cast<std::uint64_t>(fullTarget), rel.addend, fullTarget});
                    }
                    break;
                }
                case R_X86_64_PC32:
                case R_X86_64_PLT32: {
                    if (patchOffset + 4 > ms.data.size()) {
                        throw std::runtime_error(objectDiagnostic(obj.path, "R_X86_64_PC32 relocation out of bounds"));
                    }
                    const std::uint64_t pc = merged[mi].virtualAddr + patchOffset;
                    const std::int64_t rel32 = fullTarget - static_cast<std::int64_t>(pc) - 4;
                    if (rel32 < INT32_MIN || rel32 > INT32_MAX) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "R_X86_64_PC32 relocation out of 32-bit range: " + targetName));
                    }
                    write32(ms.data, patchOffset, static_cast<std::uint32_t>(static_cast<std::int32_t>(rel32)));
                    if (trace) {
                        trace->push_back({obj.path, ms.name, rel.offset,
                            rel.type == R_X86_64_PC32 ? "R_X86_64_PC32" : "R_X86_64_PLT32",
                            targetName, static_cast<std::uint64_t>(fullTarget), rel.addend, rel32});
                    }
                    break;
                }
                case 9: { // R_X86_64_GOTPCREL — 用 PC32 直接寻址（静态链接不需要 GOT）
                    if (patchOffset + 4 > ms.data.size()) {
                        throw std::runtime_error(objectDiagnostic(obj.path, "R_X86_64_GOTPCREL relocation out of bounds"));
                    }
                    const std::uint64_t pc = merged[mi].virtualAddr + patchOffset;
                    const std::int64_t rel32 = fullTarget - static_cast<std::int64_t>(pc) - 4;
                    if (rel32 < INT32_MIN || rel32 > INT32_MAX) {
                        throw std::runtime_error(
                            objectDiagnostic(obj.path, "R_X86_64_GOTPCREL relocation out of range: " + targetName));
                    }
                    write32(ms.data, patchOffset, static_cast<std::uint32_t>(static_cast<std::int32_t>(rel32)));
                    if (trace) {
                        trace->push_back({obj.path, ms.name, rel.offset, "R_X86_64_GOTPCREL",
                            targetName, static_cast<std::uint64_t>(fullTarget), rel.addend, rel32});
                    }
                    break;
                }
                default:
                    throw std::runtime_error(
                        objectDiagnostic(obj.path,
                            "unsupported relocation type: " + std::to_string(rel.type) +
                            " for symbol " + sym.name));
                }
            }
        }
    }
}

void traceInputObjects(std::ostream &out, const std::vector<LinkedObject> &linked) {
    out << "[elf-link] input objects\n";
    out << "[elf-link]   summary " << countLabel(linked.size(), "object", "objects") << '\n';
    for (const auto &lo : linked) {
        out << "[elf-link] object " << lo.object.path.string() << '\n';
        std::size_t definedCount = 0;
        std::size_t externCount = 0;
        for (const auto &sym : lo.object.symbols) {
            const std::uint8_t bind = elf64StBind(sym.info);
            if (bind != StbGlobal && bind != StbWeak) continue;
            if (sym.name.empty()) continue;
            if (sym.shndx == ShnUndefined) {
                ++externCount;
                out << "[elf-link]   extern " << sym.name << '\n';
            } else {
                ++definedCount;
                out << "[elf-link]   symbol " << sym.name << " section=" << sym.shndx
                    << " value=" << hex64(sym.value) << '\n';
            }
        }
        out << "[elf-link]   summary defined=" << definedCount << " externs=" << externCount << '\n';
    }
}

void traceMergedSections(std::ostream &out, const std::vector<MergedSection> &merged) {
    out << "[elf-link] merged sections\n";
    for (const auto &ms : merged) {
        out << "[elf-link]   " << ms.name
            << " vaddr=" << hex64(ms.virtualAddr)
            << " size=" << ms.data.size()
            << " flags=" << hex64(ms.flags) << '\n';
    }
}

void traceRelocations(std::ostream &out, const std::vector<RelocationTrace> &entries) {
    out << "[elf-link] relocations\n";
    out << "[elf-link]   summary " << countLabel(entries.size(), "relocation", "relocations") << '\n';
    for (const auto &e : entries) {
        out << "[elf-link]   " << e.objectPath.string()
            << " section=" << e.sectionName
            << " off=" << hex64(e.offset)
            << " type=" << e.typeName
            << " target=" << e.targetName
            << " target_addr=" << hex64(e.targetAddr)
            << " addend=" << e.addend
            << " computed=" << e.computed << '\n';
    }
}

} // namespace

void ElfLinker::linkObjects(
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    unsigned int jobs,
    std::ostream *trace) {

    std::vector<LinkedObject> linked = readLinkedObjects(objPaths, jobs);
    if (trace) {
        traceInputObjects(*trace, linked);
    }

    // 合并节
    std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::uint32_t>> sectionMap;
    std::vector<MergedSection> merged = mergeSections(linked, sectionMap);

    // 收集全局符号
    auto globals = collectGlobalSymbols(linked);

    // 查找 _start 入口
    auto startIt = globals.find("_start");
    if (startIt == globals.end()) {
        throw std::runtime_error(linkerDiagnostic("missing entry symbol: _start"));
    }
    const GlobalSymbolDef &startDef = startIt->second;

    // 计算虚拟地址布局
    // 段布局：[.text + .rodata] [read-only] [.data + .bss] [read-write]
    // 简化：所有可分配节放在一个大的 PT_LOAD 段中
    std::uint64_t currentVaddr = DefaultBaseAddr;

    // 按对齐排列：.text, .rodata, .data, .bss
    // 先收集可分配节，按类型排序
    std::vector<std::size_t> allocSections;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        if (merged[i].flags & ShfAlloc) {
            allocSections.push_back(i);
        }
    }

    // 排序：先代码（有执行标志），然后只读数据，然后可写数据
    std::sort(allocSections.begin(), allocSections.end(),
        [&](std::size_t a, std::size_t b) {
            const auto &sa = merged[a];
            const auto &sb = merged[b];
            // 代码段优先
            bool aExec = (sa.flags & ShfExecinstr) != 0;
            bool bExec = (sb.flags & ShfExecinstr) != 0;
            if (aExec != bExec) return aExec;
            // 只读优先于可写
            bool aWrite = (sa.flags & ShfWrite) != 0;
            bool bWrite = (sb.flags & ShfWrite) != 0;
            if (aWrite != bWrite) return !aWrite;
            return sa.name < sb.name;
        });

    // 分配虚拟地址
    for (std::size_t idx : allocSections) {
        auto &ms = merged[idx];
        if (ms.addralign > 1) {
            currentVaddr = alignTo64(currentVaddr, ms.addralign);
        }
        ms.virtualAddr = currentVaddr;
        if (ms.isBss) {
            currentVaddr += alignTo64(ms.data.size() > 0 ? ms.data.size() : 1, ms.addralign > 0 ? ms.addralign : 1);
            // BSS 没有数据，但需要占位
            if (ms.data.empty()) {
                ms.data.resize(1, 0); // 确保 BSS 有非零大小
            }
        } else {
            currentVaddr += ms.data.size();
        }
    }

    // 计算 _start 入口地址
    auto startSecMap = sectionMap.find(startDef.objectIndex);
    if (startSecMap == sectionMap.end()) {
        throw std::runtime_error(linkerDiagnostic("_start symbol references unknown section"));
    }
    auto startMergedIt = startSecMap->second.find(startDef.shndx);
    if (startMergedIt == startSecMap->second.end()) {
        throw std::runtime_error(linkerDiagnostic("_start symbol references non-output section"));
    }
    const std::uint64_t entryAddr = merged[startMergedIt->second].virtualAddr +
        linked[startDef.objectIndex].sectionOffsets[startDef.shndx] + startDef.value;

    if (trace) {
        traceMergedSections(*trace, merged);
    }

    // 应用重定位
    std::vector<RelocationTrace> relocTrace;
    applyRelocations(linked, merged, sectionMap, globals, trace ? &relocTrace : nullptr);

    if (trace) {
        traceRelocations(*trace, relocTrace);
    }

    // 构建 ELF 可执行文件
    // 计算文件布局
    const std::uint64_t ehdrSize = sizeof(Elf64_Ehdr);
    const std::uint64_t phdrSize = sizeof(Elf64_Phdr);
    // 我们需要 2 个 PT_LOAD 段：一个只读（.text+.rodata），一个可读写（.data+.bss）
    // 但实际上我们可以用一个 PT_LOAD 段
    const std::uint16_t phdrCount = 2; // 1 个 PT_LOAD + 1 个 PT_NULL
    const std::uint64_t headersSize = ehdrSize + phdrCount * phdrSize;
    const std::uint64_t firstPageEnd = alignTo64(headersSize, PageAlignment);

    // 按虚拟地址排序所有可分配节
    std::vector<std::size_t> sortedAlloc;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        if (merged[i].flags & ShfAlloc) {
            sortedAlloc.push_back(i);
        }
    }
    std::sort(sortedAlloc.begin(), sortedAlloc.end(),
        [&](std::size_t a, std::size_t b) { return merged[a].virtualAddr < merged[b].virtualAddr; });

    // 计算第一个和最后一个可分配节
    if (sortedAlloc.empty()) {
        throw std::runtime_error(linkerDiagnostic("no allocatable sections in output"));
    }

    const std::uint64_t firstVaddr = merged[sortedAlloc.front()].virtualAddr;
    const auto &lastMs = merged[sortedAlloc.back()];
    const std::uint64_t lastVaddr = lastMs.virtualAddr + (lastMs.isBss ? 0 : lastMs.data.size());
    const std::uint64_t totalMemSize = alignTo64(lastVaddr - firstVaddr, PageAlignment);

    // 计算文件中的数据大小（排除 BSS）
    std::uint64_t totalFileSize = 0;
    for (std::size_t idx : sortedAlloc) {
        const auto &ms = merged[idx];
        if (ms.isBss) continue;
        const std::uint64_t secEnd = (ms.virtualAddr - firstVaddr) + ms.data.size();
        if (secEnd > totalFileSize) totalFileSize = secEnd;
    }
    totalFileSize = alignTo64(totalFileSize, PageAlignment);

    // 计算各节的文件偏移
    std::uint64_t currentFileOff = firstPageEnd;
    for (std::size_t idx : sortedAlloc) {
        auto &ms = merged[idx];
        if (ms.isBss) continue;
        const std::uint64_t offInSegment = ms.virtualAddr - firstVaddr;
        ms.fileOffset = firstPageEnd + offInSegment;
    }

    // 构建文件
    std::vector<std::uint8_t> file;

    // ELF 头
    Elf64_Ehdr ehdr{};
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = ElfClass64;
    ehdr.e_ident[5] = ElfDataLittleEndian;
    ehdr.e_ident[6] = 1; // EV_CURRENT
    ehdr.e_ident[7] = ElfOsAbiSysv;
    ehdr.e_type = ElfTypeExec;
    ehdr.e_machine = ElfMachineX86_64;
    ehdr.e_version = 1;
    ehdr.e_entry = entryAddr;
    ehdr.e_phoff = ehdrSize;
    ehdr.e_shoff = 0; // 暂不生成节头表
    ehdr.e_flags = 0;
    ehdr.e_ehsize = static_cast<std::uint16_t>(ehdrSize);
    ehdr.e_phentsize = static_cast<std::uint16_t>(phdrSize);
    ehdr.e_phnum = phdrCount;
    ehdr.e_shentsize = 0;
    ehdr.e_shnum = 0;
    ehdr.e_shstrndx = 0;

    file.resize(sizeof(Elf64_Ehdr));
    std::memcpy(file.data(), &ehdr, sizeof(Elf64_Ehdr));

    // 程序头
    // PT_LOAD 段：覆盖从文件头到所有数据
    Elf64_Phdr loadPhdr{};
    loadPhdr.p_type = PtLoad;
    loadPhdr.p_flags = PfR | PfX; // 只读+执行（合并段的保守标志）
    loadPhdr.p_offset = 0;
    loadPhdr.p_vaddr = firstVaddr;
    loadPhdr.p_paddr = firstVaddr;
    loadPhdr.p_filesz = totalFileSize;
    loadPhdr.p_memsz = totalMemSize;
    loadPhdr.p_align = PageAlignment;

    // 检查是否有可写节
    bool hasWritable = false;
    for (std::size_t idx : sortedAlloc) {
        if (merged[idx].flags & ShfWrite) {
            hasWritable = true;
            break;
        }
    }

    if (hasWritable) {
        // 分成两个段：只读段和可读写段
        // 只读段：从文件开始到第一个可写节之前
        // 可读写段：从第一个可写节开始到结束

        std::uint64_t readOnlyEnd = 0;
        std::uint64_t readWriteStart = UINT64_MAX;
        for (std::size_t idx : sortedAlloc) {
            const auto &ms = merged[idx];
            if (ms.flags & ShfWrite) {
                if (ms.virtualAddr < readWriteStart) {
                    readWriteStart = ms.virtualAddr;
                }
            } else {
                const std::uint64_t end = ms.virtualAddr + ms.data.size();
                if (end > readOnlyEnd) readOnlyEnd = end;
            }
        }

        if (readWriteStart == UINT64_MAX) readWriteStart = readOnlyEnd;

        // 只读段
        Elf64_Phdr roPhdr{};
        roPhdr.p_type = PtLoad;
        roPhdr.p_flags = PfR;
        roPhdr.p_offset = 0;
        roPhdr.p_vaddr = firstVaddr;
        roPhdr.p_paddr = firstVaddr;
        const std::uint64_t roEnd = alignTo64(readWriteStart - firstVaddr, PageAlignment);
        roPhdr.p_filesz = std::min(roEnd, totalFileSize);
        roPhdr.p_memsz = roEnd;
        roPhdr.p_align = PageAlignment;

        // 可读写段
        const std::uint64_t rwOffset = readWriteStart - firstVaddr;
        Elf64_Phdr rwPhdr{};
        rwPhdr.p_type = PtLoad;
        rwPhdr.p_flags = PfR | PfW;
        rwPhdr.p_offset = alignTo64(rwOffset, PageAlignment);
        rwPhdr.p_vaddr = firstVaddr + alignTo64(rwOffset, PageAlignment);
        rwPhdr.p_paddr = rwPhdr.p_vaddr;
        const std::uint64_t rwFileSize = totalFileSize > alignTo64(rwOffset, PageAlignment) ?
            totalFileSize - alignTo64(rwOffset, PageAlignment) : 0;
        rwPhdr.p_filesz = rwFileSize;
        rwPhdr.p_memsz = totalMemSize - alignTo64(rwOffset, PageAlignment);
        rwPhdr.p_align = PageAlignment;

        file.resize(sizeof(Elf64_Ehdr) + 2 * sizeof(Elf64_Phdr));
        std::memcpy(file.data() + sizeof(Elf64_Ehdr), &roPhdr, sizeof(Elf64_Phdr));
        std::memcpy(file.data() + sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr), &rwPhdr, sizeof(Elf64_Phdr));
    } else {
        // 只需要一个只读段
        file.resize(sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr));
        std::memcpy(file.data() + sizeof(Elf64_Ehdr), &loadPhdr, sizeof(Elf64_Phdr));
    }

    // 填充到页边界
    file.resize(firstPageEnd, 0);

    // 写入各节数据
    for (std::size_t idx : sortedAlloc) {
        const auto &ms = merged[idx];
        if (ms.isBss) continue;
        const std::uint64_t off = ms.virtualAddr - firstVaddr;
        if (off + ms.data.size() > file.size()) {
            file.resize(off + ms.data.size(), 0);
        }
        std::copy(ms.data.begin(), ms.data.end(), file.begin() + off);
    }

    // 确保文件大小是页对齐的
    file.resize(totalFileSize, 0);

    // 构建节头表
    // 1. 构建节名字符串表 .shstrtab
    std::vector<std::uint8_t> shstrtab;
    shstrtab.push_back(0); // 索引 0 是空字符串
    std::vector<std::size_t> sectionNameOffsets;
    for (std::size_t idx : sortedAlloc) {
        sectionNameOffsets.push_back(shstrtab.size());
        const auto &name = merged[idx].name;
        shstrtab.insert(shstrtab.end(), name.begin(), name.end());
        shstrtab.push_back(0);
    }
    // 添加 .shstrtab 自身的名称
    const std::string shstrtabName = ".shstrtab";
    const std::size_t shstrtabNameOffset = shstrtab.size();
    shstrtab.insert(shstrtab.end(), shstrtabName.begin(), shstrtabName.end());
    shstrtab.push_back(0);

    // 2. 构建节头
    std::vector<Elf64_Shdr> shdrs;
    // NULL 节（索引 0）
    Elf64_Shdr nullShdr{};
    shdrs.push_back(nullShdr);

    for (std::size_t i = 0; i < sortedAlloc.size(); ++i) {
        const auto &ms = merged[sortedAlloc[i]];
        Elf64_Shdr shdr{};
        shdr.sh_name = static_cast<std::uint32_t>(sectionNameOffsets[i]);
        shdr.sh_type = ms.isBss ? 8 /* SHT_NOBITS */ : 1 /* SHT_PROGBITS */;
        shdr.sh_flags = ms.flags;
        shdr.sh_addr = ms.virtualAddr;
        shdr.sh_offset = ms.isBss ? 0 : (ms.virtualAddr - firstVaddr);
        shdr.sh_size = ms.data.size();
        shdr.sh_link = 0;
        shdr.sh_info = 0;
        shdr.sh_addralign = 1;
        shdr.sh_entsize = 0;
        shdrs.push_back(shdr);
    }

    // .shstrtab 节
    const std::size_t shstrtabIdx = shdrs.size();
    Elf64_Shdr shstrtabShdr{};
    shstrtabShdr.sh_name = static_cast<std::uint32_t>(shstrtabNameOffset);
    shstrtabShdr.sh_type = 3; // SHT_STRTAB
    shstrtabShdr.sh_flags = 0;
    shstrtabShdr.sh_addr = 0;
    shstrtabShdr.sh_offset = file.size();
    shstrtabShdr.sh_size = shstrtab.size();
    shstrtabShdr.sh_link = 0;
    shstrtabShdr.sh_info = 0;
    shstrtabShdr.sh_addralign = 1;
    shstrtabShdr.sh_entsize = 0;
    shdrs.push_back(shstrtabShdr);

    // 追加 .shstrtab 数据
    file.insert(file.end(), shstrtab.begin(), shstrtab.end());

    // 更新 ELF 头
    const std::uint64_t shoff = file.size();
    std::memcpy(file.data() + 40, &shoff, 8); // e_shoff 在偏移 40
    const std::uint16_t shnum = static_cast<std::uint16_t>(shdrs.size());
    std::memcpy(file.data() + 60, &shnum, 2); // e_shnum 在偏移 60
    const std::uint16_t shstrndx = static_cast<std::uint16_t>(shstrtabIdx);
    std::memcpy(file.data() + 62, &shstrndx, 2); // e_shstrndx 在偏移 62
    const std::uint16_t shentsize = sizeof(Elf64_Shdr);
    std::memcpy(file.data() + 58, &shentsize, 2); // e_shentsize 在偏移 58

    // 追加节头表
    const auto shdrData = reinterpret_cast<const char *>(shdrs.data());
    file.insert(file.end(), shdrData, shdrData + shdrs.size() * sizeof(Elf64_Shdr));

    // 写入文件
    if (!exePath.parent_path().empty()) {
        fs::create_directories(exePath.parent_path());
    }
    std::ofstream out(exePath, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write executable: " + exePath.string());
    }
    out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
}
