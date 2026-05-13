#include "CodeGenerator.h"

#include <iomanip>
#include <map>

void CodeGenerator::emitStatement(Stmt &stmt) {
    // 记录语句行号用于 DWARF 调试信息
    if (stmt.line > 0) {
        std::string label = makeLabel("dbg_line");
        emitLine(label + ":");
        debugLineEntries.push_back({label, stmt.line});
    }

    stmt.accept(*this);
}

void CodeGenerator::visitReturnStmt(ReturnStmt &node) {
    if (node.expr) {
        if (node.expr->kind == Expr::Kind::Call &&
            !node.expr->type->isStruct() && canTailCall()) {
            emitTailCall(static_cast<CallExpr &>(*node.expr));
            return;
        }
        if (node.expr->type->isStruct()) {
            if (usesWindowsAbi() && node.expr->type->valueSize() > 8) {
                emitLine("    mov rcx, qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]");
                emitAddress(*node.expr);
                emitLine("    mov rdx, rax");
                emitCopyStructValue(*node.expr->type, "rcx", "rdx");
            } else if (!usesWindowsAbi() && !isSystemVRegisterStruct(*node.expr->type)) {
                emitLine("    mov rcx, qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]");
                emitAddress(*node.expr);
                emitLine("    mov rdx, rax");
                emitCopyStructValue(*node.expr->type, "rcx", "rdx");
                emitLine("    mov rax, rcx");
            } else if (!usesWindowsAbi() && node.expr->type->valueSize() > 8) {
                emitAddress(*node.expr);
                emitLine("    mov rax, qword [rax]");
                emitLine("    mov rdx, qword [rax+8]");
            } else {
                emitLoadStructValueToRax(*node.expr);
            }
        } else {
            emitExpr(*node.expr);
        }
    }
    emitLine("    jmp " + currentReturnLabel);
}

void CodeGenerator::visitExprStmt(ExprStmt &node) {
    emitExpr(*node.expr);
}

void CodeGenerator::visitDeclStmt(DeclStmt &node) {
    if (node.isStatic) {
        return;
    }
    if (node.type->isVla && node.vlaSizeExpr) {
        functionHasVla = true;
        emitExpr(*node.vlaSizeExpr);
        int elemSize = node.type->elementType->valueSize();
        if (elemSize > 1) {
            emitLine("    imul rax, " + std::to_string(elemSize));
        }
        emitLine("    add rax, 15");
        emitLine("    and rax, ~15");
        emitLine("    sub rsp, rax");
        emitLine("    mov rax, rsp");
        emitLine("    mov qword [rbp-" + std::to_string(node.stackOffset) + "], rax");
        return;
    }
    if (node.init) {
        if (node.type->isStruct() && node.init->kind == Expr::Kind::InitializerList) {
            emitLocalStructInitializer(node, static_cast<const InitializerListExpr &>(*node.init));
        } else if (node.type->isArray() && node.init->kind == Expr::Kind::String &&
            node.type->elementType->equals(*Type::makeChar())) {
            emitLocalStringInitializer(node, static_cast<const StringExpr &>(*node.init));
        } else if (node.type->isArray() && node.init->kind == Expr::Kind::InitializerList) {
            emitLocalArrayInitializer(node, static_cast<const InitializerListExpr &>(*node.init));
        } else {
            emitLine("    lea rax, [rbp-" + std::to_string(node.stackOffset) + "]");
            emitLine("    push rax");
            emitExpr(*node.init);
            emitLine("    pop rcx");
            emitStore(*node.type);
        }
    }
}

void CodeGenerator::visitBlockStmt(BlockStmt &node) {
    for (const auto &nested : node.statements) {
        emitStatement(*nested);
    }
}

void CodeGenerator::visitIfStmt(IfStmt &node) {
    const std::string elseLabel = makeLabel("if_else");
    const std::string endLabel = makeLabel("if_end");

    emitExpr(*node.condition);
    if (node.condition->type && node.condition->type->isFloatingPoint()) {
        emitFloatToBool();
    }
    emitLine("    cmp rax, 0");
    emitLine("    je " + elseLabel);
    emitStatement(*node.thenBranch);
    emitLine("    jmp " + endLabel);
    emitLine(elseLabel + ":");
    if (node.elseBranch) {
        emitStatement(*node.elseBranch);
    }
    emitLine(endLabel + ":");
}

void CodeGenerator::visitWhileStmt(WhileStmt &node) {
    const std::string loopLabel = makeLabel("while_begin");
    const std::string endLabel = makeLabel("while_end");

    loopContinueLabels.push_back(loopLabel);
    loopBreakLabels.push_back(endLabel);
    emitLine(loopLabel + ":");
    emitExpr(*node.condition);
    if (node.condition->type && node.condition->type->isFloatingPoint()) {
        emitFloatToBool();
    }
    emitLine("    cmp rax, 0");
    emitLine("    je " + endLabel);
    emitStatement(*node.body);
    emitLine("    jmp " + loopLabel);
    emitLine(endLabel + ":");
    loopContinueLabels.pop_back();
    loopBreakLabels.pop_back();
}

void CodeGenerator::visitForStmt(ForStmt &node) {
    const std::string conditionLabel = makeLabel("for_cond");
    const std::string updateLabel = makeLabel("for_update");
    const std::string endLabel = makeLabel("for_end");

    if (node.init) {
        emitStatement(*node.init);
    }

    // 循环展开检测：for (i = 0; i < N; i++) 且 N 是小常量 (2-4)
    int unrollCount = 0;
    if (node.condition && node.condition->kind == Expr::Kind::Binary) {
        auto &cond = static_cast<const BinaryExpr &>(*node.condition);
        if (cond.op == BinaryOp::Less && cond.right->kind == Expr::Kind::Number) {
            long long n = static_cast<const NumberExpr &>(*cond.right).value;
            if (n >= 2 && n <= 4 && node.update) {
                unrollCount = static_cast<int>(n);
            }
        }
    }

    if (unrollCount > 0) {
        loopContinueLabels.push_back(updateLabel);
        loopBreakLabels.push_back(endLabel);
        for (int u = 0; u < unrollCount; ++u) {
            emitStatement(*node.body);
            if (node.update && u < unrollCount - 1) {
                emitExpr(*node.update);
            }
        }
        if (node.update) {
            emitExpr(*node.update);
        }
        loopContinueLabels.pop_back();
        loopBreakLabels.pop_back();
    } else {
        loopContinueLabels.push_back(updateLabel);
        loopBreakLabels.push_back(endLabel);
        emitLine(conditionLabel + ":");
        if (node.condition) {
            emitExpr(*node.condition);
            if (node.condition->type && node.condition->type->isFloatingPoint()) {
                emitFloatToBool();
            }
            emitLine("    cmp rax, 0");
            emitLine("    je " + endLabel);
        }
        emitStatement(*node.body);
        emitLine(updateLabel + ":");
        if (node.update) {
            emitExpr(*node.update);
        }
        emitLine("    jmp " + conditionLabel);
        emitLine(endLabel + ":");
        loopContinueLabels.pop_back();
        loopBreakLabels.pop_back();
    }
}

void CodeGenerator::visitDoWhileStmt(DoWhileStmt &node) {
    const std::string bodyLabel = makeLabel("do_body");
    const std::string endLabel = makeLabel("do_end");

    loopContinueLabels.push_back(bodyLabel);
    loopBreakLabels.push_back(endLabel);
    emitLine(bodyLabel + ":");
    emitStatement(*node.body);
    emitExpr(*node.condition);
    if (node.condition->type && node.condition->type->isFloatingPoint()) {
        emitFloatToBool();
    }
    emitLine("    cmp rax, 0");
    emitLine("    jne " + bodyLabel);
    emitLine(endLabel + ":");
    loopContinueLabels.pop_back();
    loopBreakLabels.pop_back();
}

void CodeGenerator::visitSwitchStmt(SwitchStmt &node) {
    const std::string endLabel = makeLabel("switch_end");

    if (emitSwitchJumpTable(node, endLabel)) {
        return;
    }

    std::vector<std::string> caseLabels;
    caseLabels.reserve(node.cases.size());
    emitExpr(*node.scrutinee);
    emitLine("    push rax");

    for (const auto &c : node.cases) {
        caseLabels.push_back(makeLabel("case"));
        emitExpr(*c.label);
        emitLine("    pop rcx");
        emitLine("    cmp eax, ecx");
        emitLine("    je " + caseLabels.back());
        emitLine("    push rcx");
    }

    emitLine("    pop rcx");

    if (node.defaultBody) {
        loopBreakLabels.push_back(endLabel);
        emitStatement(*node.defaultBody);
        loopBreakLabels.pop_back();
        emitLine("    jmp " + endLabel);
    } else {
        emitLine("    jmp " + endLabel);
    }

    loopBreakLabels.push_back(endLabel);
    for (std::size_t i = 0; i < node.cases.size(); ++i) {
        emitLine(caseLabels[i] + ":");
        emitStatement(*node.cases[i].body);
    }
    loopBreakLabels.pop_back();

    emitLine(endLabel + ":");
}

void CodeGenerator::visitBreakStmt(BreakStmt &) {
    emitLine("    jmp " + loopBreakLabels.back());
}

void CodeGenerator::visitContinueStmt(ContinueStmt &) {
    emitLine("    jmp " + loopContinueLabels.back());
}

void CodeGenerator::visitGotoStmt(GotoStmt &node) {
    emitLine("    jmp label_" + node.targetName);
}

void CodeGenerator::visitLabelStmt(LabelStmt &node) {
    emitLine("label_" + node.name + ":");
    emitStatement(*node.body);
}

void CodeGenerator::visitStaticAssertStmt(StaticAssertStmt &) {
    // 编译时断言，代码生成阶段无需处理
}

bool CodeGenerator::emitSwitchJumpTable(const SwitchStmt &sw, const std::string &endLabel) {
    std::map<long long, std::size_t> valueToCase;
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        if (sw.cases[i].label->kind != Expr::Kind::Number) {
            return false;
        }
        long long val = static_cast<const NumberExpr &>(*sw.cases[i].label).value;
        if (val < -2147483648LL || val > 2147483647LL) {
            return false;
        }
        if (!valueToCase.emplace(val, i).second) {
            return false;
        }
    }

    if (valueToCase.empty()) {
        return false;
    }

    const long long minVal = valueToCase.begin()->first;
    const long long maxVal = valueToCase.rbegin()->first;
    const long long range = maxVal - minVal + 1;

    if (range > 256 || range > static_cast<long long>(sw.cases.size()) * 2) {
        return false;
    }

    const std::string tableLabel = makeLabel("switch_table");
    const std::string defaultBodyLabel = sw.defaultBody ? makeLabel("default_body") : std::string();

    std::map<long long, std::string> caseValueLabels;
    for (const auto &[val, idx] : valueToCase) {
        caseValueLabels[val] = makeLabel("case");
    }

    std::vector<std::string> slotLabels(static_cast<std::size_t>(range));
    for (long long v = minVal; v <= maxVal; ++v) {
        auto caseIt = caseValueLabels.find(v);
        if (caseIt != caseValueLabels.end()) {
            slotLabels[static_cast<std::size_t>(v - minVal)] = caseIt->second;
        } else {
            slotLabels[static_cast<std::size_t>(v - minVal)] =
                sw.defaultBody ? defaultBodyLabel : endLabel;
        }
    }

    emitRdataLine(tableLabel + ":");
    for (const auto &label : slotLabels) {
        emitRdataLine("    dq " + label);
    }

    emitExpr(*sw.scrutinee);
    if (minVal != 0) {
        emitLine("    sub eax, " + std::to_string(minVal));
    }
    emitLine("    cmp eax, " + std::to_string(maxVal - minVal));
    emitLine("    ja " + (sw.defaultBody ? defaultBodyLabel : endLabel));
    emitLine("    lea rcx, [rel " + tableLabel + "]");
    emitLine("    movsxd rax, eax");
    emitLine("    jmp [rcx + rax * 8]");

    loopBreakLabels.push_back(endLabel);
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        long long val = static_cast<const NumberExpr &>(*sw.cases[i].label).value;
        emitLine(caseValueLabels[val] + ":");
        emitStatement(*sw.cases[i].body);
    }

    if (sw.defaultBody) {
        emitLine(defaultBodyLabel + ":");
        emitStatement(*sw.defaultBody);
    }
    loopBreakLabels.pop_back();

    emitLine(endLabel + ":");
    return true;
}

void CodeGenerator::collectStaticLocals(const BlockStmt &block) {
    for (const auto &stmt : block.statements) {
        if (stmt->kind == Stmt::Kind::Decl) {
            const auto &decl = static_cast<const DeclStmt &>(*stmt);
            if (decl.isStatic) {
                staticLocalVars.push_back({decl.type, decl.staticSymbolName, decl.init.get()});
            }
        } else if (stmt->kind == Stmt::Kind::Block) {
            collectStaticLocals(static_cast<const BlockStmt &>(*stmt));
        } else if (stmt->kind == Stmt::Kind::If) {
            const auto &ifStmt = static_cast<const IfStmt &>(*stmt);
            if (ifStmt.thenBranch->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*ifStmt.thenBranch));
            }
            if (ifStmt.elseBranch && ifStmt.elseBranch->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*ifStmt.elseBranch));
            }
        } else if (stmt->kind == Stmt::Kind::While) {
            const auto &whileStmt = static_cast<const WhileStmt &>(*stmt);
            if (whileStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*whileStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::For) {
            const auto &forStmt = static_cast<const ForStmt &>(*stmt);
            if (forStmt.init && forStmt.init->kind == Stmt::Kind::Decl) {
                const auto &initDecl = static_cast<const DeclStmt &>(*forStmt.init);
                if (initDecl.isStatic) {
                    staticLocalVars.push_back({initDecl.type, initDecl.staticSymbolName, initDecl.init.get()});
                }
            }
            if (forStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*forStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::DoWhile) {
            const auto &doWhileStmt = static_cast<const DoWhileStmt &>(*stmt);
            if (doWhileStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*doWhileStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::Switch) {
            const auto &sw = static_cast<const SwitchStmt &>(*stmt);
            for (const auto &c : sw.cases) {
                if (c.body->kind == Stmt::Kind::Block) {
                    collectStaticLocals(static_cast<const BlockStmt &>(*c.body));
                }
            }
            if (sw.defaultBody && sw.defaultBody->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*sw.defaultBody));
            }
        }
    }
}

void CodeGenerator::emitStaticLocals() {
    for (const auto &var : staticLocalVars) {
        if (var.init && var.init->kind == Expr::Kind::Number) {
            const long long value = static_cast<const NumberExpr &>(*var.init).value;
            std::ostringstream line;
            switch (var.type->valueSize()) {
            case 1:
                line << var.symbolName << ": db " << value;
                break;
            case 2:
                line << var.symbolName << ": dw " << value;
                break;
            case 4:
                line << var.symbolName << ": dd " << value;
                break;
            default:
                line << var.symbolName << ": dq " << value;
                break;
            }
            emitDataLine(line.str());
        } else if (var.init && var.init->kind == Expr::Kind::FloatLiteral) {
            const double value = static_cast<const FloatLiteralExpr &>(*var.init).value;
            std::ostringstream valStream;
            valStream << std::setprecision(17) << value;
            std::string valStr = valStream.str();
            if (valStr.find('.') == std::string::npos &&
                valStr.find('e') == std::string::npos &&
                valStr.find('E') == std::string::npos) {
                valStr += ".0";
            }
            std::ostringstream line;
            if (var.type->kind == TypeKind::Float) {
                line << var.symbolName << ": dd __float32__(" << valStr << ")";
            } else {
                line << var.symbolName << ": dq __float64__(" << valStr << ")";
            }
            emitDataLine(line.str());
        } else if (var.init && var.init->kind == Expr::Kind::String) {
            const auto &stringExpr = static_cast<const StringExpr &>(*var.init);
            std::ostringstream line;
            line << var.symbolName << ": db ";
            for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
                if (i > 0) {
                    line << ", ";
                }
                line << static_cast<int>(static_cast<unsigned char>(stringExpr.value[i]));
            }
            if (!stringExpr.value.empty()) {
                line << ", ";
            }
            line << "0";
            emitDataLine(line.str());
        } else {
            std::ostringstream line;
            if (var.type->isArray() || var.type->isStruct()) {
                line << var.symbolName << ": resb " << var.type->valueSize();
            } else {
                switch (var.type->valueSize()) {
                case 1:
                    line << var.symbolName << ": resb 1";
                    break;
                case 2:
                    line << var.symbolName << ": resw 1";
                    break;
                case 4:
                    line << var.symbolName << ": resd 1";
                    break;
                default:
                    line << var.symbolName << ": resq 1";
                    break;
                }
            }
            emitBssLine(line.str());
        }
    }
}
