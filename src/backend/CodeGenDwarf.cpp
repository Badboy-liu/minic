#include "CodeGenerator.h"

#include <cstdio>

// LEB128 编码辅助
std::string CodeGenerator::encodeULEB128(int value) {
    std::string result;
    unsigned int v = static_cast<unsigned int>(value);
    do {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if (v != 0) byte |= 0x80;
        result += "0x";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x", byte);
        result += buf;
        result += ",";
    } while (v != 0);
    return result;
}

std::string CodeGenerator::encodeSLEB128(int value) {
    std::string result;
    int v = value;
    bool more = true;
    while (more) {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if ((v == 0 && (byte & 0x40) == 0) || (v == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        result += "0x";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x", byte);
        result += buf;
        result += ",";
    }
    return result;
}

void CodeGenerator::emitDwarfSections() {
    if (debugFuncInfos.empty()) return;
    emitDebugStrSection();
    emitDebugAbbrevSection();
    emitDebugLineSection();
    emitDebugInfoSection();
}

void CodeGenerator::emitDebugStrSection() {
    emitLine("section .debug_str");
    // 文件名
    std::string producer = "minic compiler";
    emitLine("    db \"" + producer + "\",0");
    std::string fileName = sourceFileName.empty() ? "input.c" : sourceFileName;
    emitLine("    db \"" + fileName + "\",0");
}

void CodeGenerator::emitDebugAbbrevSection() {
    emitLine("section .debug_abbrev");
    // 编译单元 DIE (tag=0x11 DW_TAG_compile_unit, children=yes)
    emitLine("    ; abbreviation 1: DW_TAG_compile_unit");
    emitLine("    db 1");  // abbreviation code
    emitLine("    db 0x11");  // DW_TAG_compile_unit
    emitLine("    db 1");  // DW_CHILDREN_yes
    // DW_AT_producer (DW_FORM_strp)
    emitLine("    db 0x25");  // DW_AT_producer
    emitLine("    db 0x0e");  // DW_FORM_strp
    // DW_AT_language (DW_FORM_data1)
    emitLine("    db 0x13");  // DW_AT_language
    emitLine("    db 0x0b");  // DW_FORM_data1
    // DW_AT_name (DW_FORM_strp)
    emitLine("    db 0x03");  // DW_AT_name
    emitLine("    db 0x0e");  // DW_FORM_strp
    // DW_AT_stmt_list (DW_FORM_sec_offset)
    emitLine("    db 0x10");  // DW_AT_stmt_list
    emitLine("    db 0x17");  // DW_FORM_sec_offset
    // 结束属性
    emitLine("    db 0,0");

    // 子程序 DIE (tag=0x2e DW_TAG_subprogram, children=yes)
    emitLine("    ; abbreviation 2: DW_TAG_subprogram");
    emitLine("    db 2");
    emitLine("    db 0x2e");  // DW_TAG_subprogram
    emitLine("    db 1");  // DW_CHILDREN_yes
    // DW_AT_name (DW_FORM_strp)
    emitLine("    db 0x03");
    emitLine("    db 0x0e");
    // DW_AT_low_pc (DW_FORM_addr)
    emitLine("    db 0x11");  // DW_AT_low_pc
    emitLine("    db 0x01");  // DW_FORM_addr
    // DW_AT_high_pc (DW_FORM_data4)
    emitLine("    db 0x12");  // DW_AT_high_pc
    emitLine("    db 0x06");  // DW_FORM_data4
    emitLine("    db 0,0");

    // 结束子程序 DIE
    emitLine("    ; abbreviation 3: (null - end of children)");
    emitLine("    db 0");

    // 结束编译单元 DIE
    emitLine("    db 0");
}

void CodeGenerator::emitDebugInfoSection() {
    emitLine("section .debug_info");

    // 编译单元头
    emitLine("    ; compilation unit header");
    // unit_length (4字节，后面填写)
    std::string lengthLabel = makeLabel("debug_info_length");
    emitLine("    dd " + lengthLabel + " - $ - 4");
    emitLine(lengthLabel + ":");
    // version
    emitLine("    dw 4");
    // debug_abbrev_offset
    emitLine("    dd 0");
    // address size
    emitLine("    db 8");

    // 编译单元 DIE (abbreviation 1)
    emitLine("    ; compile unit DIE");
    emitLine("    db 1");  // abbreviation code
    // DW_AT_producer -> .debug_str 偏移 0
    emitLine("    dd 0");
    // DW_AT_language -> C99 (0x0c)
    emitLine("    db 0x0c");
    // DW_AT_name -> .debug_str 偏移
    std::string producer = "minic compiler";
    std::string fileName = sourceFileName.empty() ? "input.c" : sourceFileName;
    int nameOffset = static_cast<int>(producer.size()) + 1;  // 跳过 producer 字符串
    emitLine("    dd " + std::to_string(nameOffset));
    // DW_AT_stmt_list -> .debug_line 节偏移（用0表示从头开始）
    emitLine("    dd 0");

    // 子程序 DIE
    for (const auto &fi : debugFuncInfos) {
        emitLine("    ; subprogram DIE: " + fi.name);
        emitLine("    db 2");  // abbreviation code
        // DW_AT_name
        int funcNameOffset = nameOffset + static_cast<int>(fileName.size()) + 1;  // 文件名 + null
        emitLine("    dd " + std::to_string(funcNameOffset));
        // DW_AT_low_pc
        emitLine("    dq " + fi.startLabel);
        // DW_AT_high_pc (size)
        emitLine("    dd " + fi.endLabel + " - " + fi.startLabel);
    }

    // 结束编译单元子节点
    emitLine("    db 0");
}

void CodeGenerator::emitDebugLineSection() {
    emitLine("section .debug_line");

    // 行号程序头
    emitLine("    ; line number program header");
    std::string headerEnd = makeLabel("debug_line_header_end");
    emitLine("    dd " + headerEnd + " - $ - 4");  // unit_length
    emitLine(headerEnd + ":");
    emitLine("    dw 4");   // version
    emitLine("    dd 0");   // header_length (简化，填0)
    emitLine("    db 1");   // minimum_instruction_length
    emitLine("    db 1");   // default_is_stmt
    emitLine("    db 0");   // line_base
    emitLine("    db 1");   // line_range
    emitLine("    db 10");  // opcode_base (标准: 10个标准操作码)
    // 标准操作码长度表
    emitLine("    db 0,1,1,1,1,0,0,0,1,0");  // 对应 opcode 1-10 的参数数量

    // 包含目录表（空，用0终止）
    emitLine("    db 0");
    // 文件名表
    std::string lineFileName = sourceFileName.empty() ? "input.c" : sourceFileName;
    emitLine("    db \"" + lineFileName + "\",0,1,0,0");  // 文件名,目录索引,最后修改时间,大小
    emitLine("    db 0");  // 文件名表结束

    // 行号程序
    emitLine("    ; line number program");

    // DW_LNS_extended_op: 设置初始地址
    emitLine("    db 0,9,2");  // extended op, length=9, DW_LNE_set_address
    emitLine("    dq " + debugFuncInfos[0].startLabel);

    bool isStmt = true;
    int currentLine = 1;
    int currentAddr = 0;

    for (const auto &entry : debugLineEntries) {
        // 先输出特殊操作码设置行号
        int lineDelta = entry.line - currentLine;

        // 检查是否需要切换函数（设置新地址）
        bool needNewAddress = false;
        for (const auto &fi : debugFuncInfos) {
            if (entry.label == fi.startLabel) {
                needNewAddress = true;
                break;
            }
        }

        if (needNewAddress) {
            // DW_LNS_extended_op: 设置新地址
            emitLine("    db 0,9,2");
            emitLine("    dq " + entry.label);
            currentAddr = 0;
        }

        if (lineDelta != 0) {
            // DW_LNS_advance_line
            std::string leb = encodeSLEB128(lineDelta);
            if (!leb.empty() && leb.back() == ',') leb.pop_back();
            emitLine("    db 3");
            emitLine("    db " + leb);
            currentLine = entry.line;
        }

        // DW_LNS_copy (输出一行)
        emitLine("    db 1");
    }

    // DW_LNS_extended_op: 序列结束
    emitLine("    db 0,1,1");  // extended op, length=1, DW_LNE_end_sequence
}
