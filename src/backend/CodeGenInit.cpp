#include "CodeGenerator.h"

#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>

void CodeGenerator::emitCopyBytes(const std::string &destAddressExpr, const std::string &srcAddressExpr, int size) {
    if (size <= 0) return;
    // 小拷贝直接用 mov（避免 rep movsb 的开销）
    if (size <= 8) {
        const char *reg = size == 1 ? "al" : size <= 2 ? "ax" : size <= 4 ? "eax" : "rax";
        emitLine("    mov " + std::string(reg) + ", [" + srcAddressExpr + "]");
        emitLine("    mov [" + destAddressExpr + "], " + std::string(reg));
        return;
    }
    // 使用 rep movsb 进行批量拷贝
    emitLine("    lea rdi, [" + destAddressExpr + "]");
    emitLine("    lea rsi, [" + srcAddressExpr + "]");
    emitLine("    mov ecx, " + std::to_string(size));
    emitLine("    rep movsb");
}

void CodeGenerator::emitCopyStructValue(const Type &type, const std::string &destAddressExpr, const std::string &srcAddressExpr) {
    emitCopyBytes(destAddressExpr, srcAddressExpr, type.valueSize());
}

void CodeGenerator::emitLoadStructValueToRax(Expr &expr) {
    if (expr.kind == Expr::Kind::Call) {
        // 函数返回结构体（<=8 字节）：值已在 rax 中
        emitExpr(expr);
        return;
    }
    emitAddress(expr);
    const int size = expr.type->valueSize();
    if (size <= 8) {
        emitLine("    mov rax, qword [rax]");
    }
}

void CodeGenerator::emitStoreStructValueFromRax(const Type &type, const std::string &destAddressExpr) {
    const int size = type.valueSize();
    if (size <= 8) {
        emitLine("    mov qword [" + destAddressExpr + "], rax");
    }
}

void CodeGenerator::emitZeroFillBytes(std::ostringstream &line, int count, bool &first) const {
    if (count <= 0) return;
    if (!first) line << ", ";
    line << "db ";
    for (int i = 0; i < count; ++i) {
        if (i > 0) line << ", ";
        line << "0";
    }
    first = false;
}

// 将结构体/数组成员递归展开为扁平的标量值列表
void CodeGenerator::emitGlobalStructScalars(
    std::vector<std::pair<std::string, std::string>> &values,
    const Type &type,
    const Expr &expr) {
    if (type.isStruct()) {
        if (expr.kind == Expr::Kind::InitializerList) {
            const auto &list = static_cast<const InitializerListExpr &>(expr);
            for (std::size_t i = 0; i < type.members.size(); ++i) {
                if (i < list.elements.size()) {
                    emitGlobalStructScalars(values, *type.members[i].type, *list.elements[i]);
                } else {
                    int sz = type.members[i].type->valueSize();
                    values.emplace_back(dataDirectiveForSize(sz), "0");
                }
            }
        } else {
            int sz = type.valueSize();
            values.emplace_back(dataDirectiveForSize(sz), "0");
        }
        return;
    }
    if (type.isArray()) {
        if (expr.kind == Expr::Kind::InitializerList) {
            const auto &list = static_cast<const InitializerListExpr &>(expr);
            for (int i = 0; i < type.arrayLength; ++i) {
                if (i < static_cast<int>(list.elements.size())) {
                    emitGlobalStructScalars(values, *type.elementType, *list.elements[i]);
                } else {
                    int sz = type.elementType->valueSize();
                    values.emplace_back(dataDirectiveForSize(sz), "0");
                }
            }
        } else {
            int sz = type.valueSize();
            values.emplace_back(dataDirectiveForSize(sz), "0");
        }
        return;
    }
    // 标量值
    const std::string dir = dataDirectiveForSize(type.valueSize());
    if (type.isPointer()) {
        values.emplace_back(dir, globalAddressInitializer(expr));
    } else if (type.isFloatingPoint()) {
        if (expr.kind == Expr::Kind::FloatLiteral) {
            const double value = static_cast<const FloatLiteralExpr &>(expr).value;
            std::ostringstream valStream;
            valStream << std::setprecision(17) << value;
            std::string valStr = valStream.str();
            if (valStr.find('.') == std::string::npos &&
                valStr.find('e') == std::string::npos &&
                valStr.find('E') == std::string::npos) {
                valStr += ".0";
            }
            if (type.kind == TypeKind::Float) {
                values.emplace_back(dir, "__float32__(" + valStr + ")");
            } else {
                values.emplace_back(dir, "__float64__(" + valStr + ")");
            }
        } else {
            if (type.kind == TypeKind::Float) {
                values.emplace_back(dir, "__float32__(0.0)");
            } else {
                values.emplace_back(dir, "__float64__(0.0)");
            }
        }
    } else {
        values.emplace_back(dir, std::to_string(evaluateStaticIntegerInitializer(expr)));
    }
}

void CodeGenerator::emitGlobalStructInitializer(const GlobalVar &global, const InitializerListExpr *list) {
    // 将结构体展开为扁平的 (directive, value) 列表
    std::vector<std::pair<std::string, std::string>> values;
    if (list) {
        for (std::size_t i = 0; i < global.type->members.size(); ++i) {
            if (i < list->elements.size()) {
                emitGlobalStructScalars(values, *global.type->members[i].type, *list->elements[i]);
            } else {
                int sz = global.type->members[i].type->valueSize();
                values.emplace_back(dataDirectiveForSize(sz), "0");
            }
        }
    } else {
        int sz = global.type->valueSize();
        values.emplace_back(dataDirectiveForSize(sz), "0");
    }

    // 按指令分组，输出为 NASM 数据行
    // 相同指令的连续值合并到一行
    std::string label = global.symbolName + ": ";
    std::size_t i = 0;
    while (i < values.size()) {
        const std::string &dir = values[i].first;
        std::ostringstream line;
        line << label << dir << " " << values[i].second;
        std::size_t j = i + 1;
        while (j < values.size() && values[j].first == dir) {
            line << ", " << values[j].second;
            ++j;
        }
        emitDataLine(line.str());
        label = "";  // 后续行无标签
        i = j;
    }
}

void CodeGenerator::emitLocalStructInitializer(const DeclStmt &decl, const InitializerListExpr &list) {
    // 检查是否有指定初始化器
    bool hasDesignators = false;
    for (const auto &desig : list.designators) {
        if (!desig.empty()) {
            hasDesignators = true;
            break;
        }
    }

    if (hasDesignators) {
        // 指定初始化器：按字段名匹配
        std::unordered_map<std::string, std::size_t> fieldToElement;
        for (std::size_t ei = 0; ei < list.designators.size(); ++ei) {
            if (!list.designators[ei].empty() && list.designators[ei][0].kind == Designator::Field) {
                fieldToElement[list.designators[ei][0].fieldName] = ei;
            }
        }
        for (std::size_t mi = 0; mi < decl.type->members.size(); ++mi) {
            const auto &member = decl.type->members[mi];
            auto it = fieldToElement.find(member.name);
            if (it == fieldToElement.end()) continue;
            emitExpr(*list.elements[it->second]);
            const int memberStackOffset = decl.stackOffset - member.offset;
            emitStoreToLocalSlot(*member.type, memberStackOffset);
        }
    } else {
        // 按位置初始化
        for (std::size_t i = 0; i < list.elements.size() && i < decl.type->members.size(); ++i) {
            const auto &member = decl.type->members[i];
            const int memberStackOffset = decl.stackOffset - member.offset;
            // 嵌套结构体初始化列表：递归处理
            if (member.type->isStruct() && list.elements[i]->kind == Expr::Kind::InitializerList) {
                auto &nestedList = static_cast<InitializerListExpr &>(*list.elements[i]);
                DeclStmt nestedDecl(member.type, member.name, nullptr);
                nestedDecl.stackOffset = memberStackOffset;
                emitLocalStructInitializer(nestedDecl, nestedList);
            } else {
                emitExpr(*list.elements[i]);
                emitStoreToLocalSlot(*member.type, memberStackOffset);
            }
        }
    }
}

void CodeGenerator::emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr) {
    const int baseOffset = decl.stackOffset;
    for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
        emitLine(
            "    mov byte [rbp-" + std::to_string(baseOffset - static_cast<int>(i)) + "], " +
            std::to_string(static_cast<int>(static_cast<unsigned char>(stringExpr.value[i]))));
    }
    emitLine("    mov byte [rbp-" + std::to_string(baseOffset - static_cast<int>(stringExpr.value.size())) + "], 0");
    for (int i = static_cast<int>(stringExpr.value.size()) + 1; i < decl.type->arrayLength; ++i) {
        emitLine("    mov byte [rbp-" + std::to_string(baseOffset - i) + "], 0");
    }
}

void CodeGenerator::emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list) {
    const int elementSize = decl.type->elementType->valueSize();
    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const int elementOffset = static_cast<int>(i) * elementSize;
        // 多维数组：子元素为嵌套初始化列表时，递归处理
        if (decl.type->elementType->isArray() && list.elements[i]->kind == Expr::Kind::InitializerList) {
            emitNestedArrayValues(
                *decl.type->elementType,
                decl.stackOffset - elementOffset,
                static_cast<const InitializerListExpr &>(*list.elements[i]));
        } else {
            emitExpr(*list.elements[i]);
            emitStoreToLocalSlot(*decl.type->elementType, decl.stackOffset - elementOffset);
        }
    }
    emitZeroLocalArrayElements(*decl.type, decl.stackOffset, list.elements.size());
}

void CodeGenerator::emitNestedArrayValues(const Type &arrayType, int baseOffset, const InitializerListExpr &list) {
    const int elementSize = arrayType.elementType->valueSize();
    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const int elementOffset = static_cast<int>(i) * elementSize;
        if (arrayType.elementType->isArray() && list.elements[i]->kind == Expr::Kind::InitializerList) {
            emitNestedArrayValues(
                *arrayType.elementType,
                baseOffset - elementOffset,
                static_cast<const InitializerListExpr &>(*list.elements[i]));
        } else {
            emitExpr(*list.elements[i]);
            emitStoreToLocalSlot(*arrayType.elementType, baseOffset - elementOffset);
        }
    }
    emitZeroLocalArrayElements(arrayType, baseOffset, list.elements.size());
}

void CodeGenerator::emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex) {
    const int elementSize = arrayType.elementType->valueSize();
    const int totalBytes = (arrayType.arrayLength - static_cast<int>(startIndex)) * elementSize;

    // 大数组使用 rep stosb 优化（>= 32 字节）
    if (!arrayType.elementType->isArray() && !arrayType.elementType->isFloatingPoint() && totalBytes >= 32) {
        const int startByteOffset = baseOffset - static_cast<int>(startIndex) * elementSize;
        emitLine("    lea rdi, [rbp-" + std::to_string(startByteOffset) + "]");
        emitLine("    xor eax, eax");
        emitLine("    mov ecx, " + std::to_string(totalBytes));
        emitLine("    rep stosb");
        return;
    }

    for (std::size_t i = startIndex; i < static_cast<std::size_t>(arrayType.arrayLength); ++i) {
        const int addressOffset = baseOffset - static_cast<int>(i) * elementSize;
        if (arrayType.elementType->isArray()) {
            // 递归零初始化嵌套子数组（从索引 0 开始）
            emitZeroLocalArrayElements(*arrayType.elementType, addressOffset, 0);
        } else if (arrayType.elementType->isFloatingPoint()) {
            emitLine("    xorpd xmm0, xmm0");
            emitStoreToLocalSlot(*arrayType.elementType, addressOffset);
        } else {
            emitLine("    xor eax, eax");
            emitLine("    xor edx, edx");
            emitStoreToLocalSlot(*arrayType.elementType, addressOffset);
        }
    }
}
