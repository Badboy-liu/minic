#include "Parser.h"

#include <stdexcept>

Parser::Parser(std::vector<Token> tokenStream, DiagnosticEngine *diagValue)
    : tokens(std::move(tokenStream)), current(0), diag(diagValue) {}

bool Parser::hasErrors() const {
    return hasParseErrors;
}

Program Parser::parseProgram() {
    Program program;

    while (!check(TokenKind::EndOfFile)) {
        try {
            parseExternalDeclaration(program);
        } catch (const std::runtime_error &) {
            // 已经通过 diag 报告了错误，跳到同步点继续
            synchronize();
        }
    }

    return program;
}

void Parser::parseExternalDeclaration(Program &program) {
    if (match(TokenKind::KeywordTypedef)) {
        TypePtr baseType = parseBaseType();
        while (match(TokenKind::Star)) {
            baseType = Type::makePointer(std::move(baseType));
        }
        const Token &nameToken = consume(TokenKind::Identifier, "expected identifier after typedef");
        consume(TokenKind::Semicolon, "expected ';' after typedef");
        typedefs[nameToken.lexeme] = std::move(baseType);
        return;
    }
    if (match(TokenKind::KeywordEnum)) {
        std::string enumName;
        if (check(TokenKind::Identifier)) {
            enumName = advance().lexeme;
        }
        consume(TokenKind::LeftBrace, "expected '{' after enum");
        int nextValue = 0;
        while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile)) {
            const Token &name = consume(TokenKind::Identifier, "expected enum constant name");
            if (match(TokenKind::Equal)) {
                if (!check(TokenKind::Number)) {
                    fail(peek(), "expected integer constant for enum value");
                }
                nextValue = advance().intValue;
            }
            enumConstants[name.lexeme] = nextValue;
            ++nextValue;
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        consume(TokenKind::RightBrace, "expected '}' after enum");
        consume(TokenKind::Semicolon, "expected ';' after enum declaration");
        return;
    }

    // 处理全局作用域的 _Static_assert
    if (match(TokenKind::KeywordStaticAssert)) {
        auto assertStmt = parseStaticAssert();
        // 存储到 Program 中，由语义分析器检查
        program.globalStaticAsserts.push_back(
            std::unique_ptr<StaticAssertStmt>(static_cast<StaticAssertStmt *>(assertStmt.release())));
        return;
    }

    const bool isExternStorage = match(TokenKind::KeywordExtern);
    const bool isThreadLocal = match(TokenKind::KeywordThreadLocal);
    const bool isInlineFunc = match(TokenKind::KeywordInline);
    const bool isNoreturnFunc = match(TokenKind::KeywordNoreturn);
    TypePtr baseType = parseBaseType();

    // 处理 struct/union 定义后跟 ';' 的情况（仅定义类型，不声明变量）
    if ((baseType->isStruct() || baseType->isUnion()) && check(TokenKind::Semicolon)) {
        consume(TokenKind::Semicolon, "expected ';' after type definition");
        return;
    }

    if (check(TokenKind::Identifier)) {
        const Token &nameToken = consume(TokenKind::Identifier, "expected identifier");
        if (check(TokenKind::LeftParen)) {
            Function func = parseFunction(std::move(baseType), nameToken.lexeme);
            func.isInline = isInlineFunc;
            func.isNoreturn = isNoreturnFunc;
            program.functions.push_back(std::move(func));
            return;
        }
        GlobalVar global = parseGlobalVariable(std::move(baseType), nameToken.lexeme, isExternStorage);
        global.isThreadLocal = isThreadLocal;
        program.globals.push_back(std::move(global));
        return;
    }

    ParsedDeclarator declarator = parseVariableDeclarator(std::move(baseType));
    GlobalVar global;
    global.type = std::move(declarator.type);
    global.name = std::move(declarator.name);
    global.isExternStorage = isExternStorage;
    global.isThreadLocal = isThreadLocal;
    if (match(TokenKind::Equal)) {
        global.init = parseInitializer();
    }
    consume(TokenKind::Semicolon, "expected ';' after global declaration");
    program.globals.push_back(std::move(global));
}

Function Parser::parseFunction(TypePtr returnType, std::string name) {
    Function function;
    function.returnType = std::move(returnType);
    function.name = std::move(name);
    consume(TokenKind::LeftParen, "expected '(' after function name");

    if (!check(TokenKind::RightParen)) {
        if (check(TokenKind::KeywordVoid)) {
            TypePtr probe = parseBaseType();
            if (!check(TokenKind::RightParen)) {
                ParsedDeclarator declarator = parseVariableDeclarator(std::move(probe));
                function.parameters.push_back(Parameter{std::move(declarator.type), std::move(declarator.name), 0});
                while (match(TokenKind::Comma)) {
                    if (match(TokenKind::DotDotDot)) {
                        function.isVariadic = true;
                        break;
                    }
                    function.parameters.push_back(parseParameter());
                }
            }
        } else {
            do {
                if (match(TokenKind::DotDotDot)) {
                    function.isVariadic = true;
                    break;
                }
                function.parameters.push_back(parseParameter());
            } while (match(TokenKind::Comma));
        }
    }

    consume(TokenKind::RightParen, "expected ')' after function parameters");
    if (match(TokenKind::Semicolon)) {
        return function;
    }

    function.body = parseBlock();
    return function;
}

Parameter Parser::parseParameter() {
    ParsedDeclarator declarator = parseVariableDeclarator(parseBaseType());
    return Parameter{std::move(declarator.type), std::move(declarator.name), 0};
}

TypePtr Parser::parseStructType() {
    const Token &nameToken = consume(TokenKind::Identifier, "expected struct tag");
    const std::string structName = nameToken.lexeme;

    if (!match(TokenKind::LeftBrace)) {
        const auto found = structTypes.find(structName);
        if (found == structTypes.end()) {
            fail(nameToken, "unknown struct type: " + structName);
        }
        return found->second;
    }

    std::vector<StructMember> members;
    if (structTypes.find(structName) != structTypes.end()) {
        fail(nameToken, "duplicate struct definition: " + structName);
    }
    while (!check(TokenKind::RightBrace)) {
        // _Static_assert 允许出现在 struct/union 内部
        if (check(TokenKind::KeywordStaticAssert)) {
            parseStaticAssert();
            continue;
        }
        TypePtr memberBaseType = parseBaseType();
        // 匿名结构体/联合体成员：类型后直接跟 ';'，没有变量名
        if ((memberBaseType->isStruct() || memberBaseType->isUnion()) && check(TokenKind::Semicolon)) {
            consume(TokenKind::Semicolon, "expected ';' after anonymous struct/union member");
            StructMember member{"", std::move(memberBaseType), 0, 0, 0};
            members.push_back(std::move(member));
            continue;
        }
        ParsedDeclarator declarator = parseVariableDeclarator(std::move(memberBaseType));
        int bitWidth = 0;
        if (match(TokenKind::Colon)) {
            // 位域：`int x : 3;`
            if (!check(TokenKind::Number)) {
                fail(peek(), "expected integer constant for bit-field width");
            }
            bitWidth = advance().intValue;
        }
        consume(TokenKind::Semicolon, "expected ';' after struct member declaration");
        // 柔性数组成员：允许 int data[]（长度为 0 的数组）作为结构体最后一个成员
        if (declarator.type->isArray() && declarator.type->arrayLength == 0) {
            // 柔性数组成员必须是最后一个成员，大小为 0
            StructMember member{std::move(declarator.name), std::move(declarator.type), 0, 0, 0};
            members.push_back(std::move(member));
        } else if (declarator.type->isVoid() || declarator.type->isFunction() || declarator.type->isArray()) {
            fail(peek(), "unsupported struct member type for " + declarator.name);
        } else {
            StructMember member{std::move(declarator.name), std::move(declarator.type), 0, bitWidth, 0};
            members.push_back(std::move(member));
        }
    }
    consume(TokenKind::RightBrace, "expected '}' after struct definition");

    TypePtr structType = Type::makeStruct(structName, std::move(members));
    structTypes[structName] = structType;
    return structType;
}

TypePtr Parser::parseUnionType() {
    const Token &nameToken = consume(TokenKind::Identifier, "expected union tag");
    const std::string unionName = nameToken.lexeme;

    if (!match(TokenKind::LeftBrace)) {
        const auto found = unionTypes.find(unionName);
        if (found == unionTypes.end()) {
            fail(nameToken, "unknown union type: " + unionName);
        }
        return found->second;
    }

    std::vector<StructMember> members;
    if (unionTypes.find(unionName) != unionTypes.end()) {
        fail(nameToken, "duplicate union definition: " + unionName);
    }
    while (!check(TokenKind::RightBrace)) {
        if (check(TokenKind::KeywordStaticAssert)) {
            parseStaticAssert();
            continue;
        }
        TypePtr memberBaseType = parseBaseType();
        // 匿名结构体/联合体成员：类型后直接跟 ';'，没有变量名
        if ((memberBaseType->isStruct() || memberBaseType->isUnion()) && check(TokenKind::Semicolon)) {
            consume(TokenKind::Semicolon, "expected ';' after anonymous struct/union member");
            members.push_back(StructMember{"", std::move(memberBaseType), 0});
            continue;
        }
        ParsedDeclarator declarator = parseVariableDeclarator(std::move(memberBaseType));
        consume(TokenKind::Semicolon, "expected ';' after union member declaration");
        if (declarator.type->isVoid() || declarator.type->isFunction() || declarator.type->isArray()) {
            fail(peek(), "unsupported union member type for " + declarator.name);
        }
        members.push_back(StructMember{std::move(declarator.name), std::move(declarator.type), 0});
    }
    consume(TokenKind::RightBrace, "expected '}' after union definition");

    TypePtr unionType = Type::makeUnion(unionName, std::move(members));
    unionTypes[unionName] = unionType;
    return unionType;
}

GlobalVar Parser::parseGlobalVariable(TypePtr declaredType, std::string name, bool isExternStorage) {
    GlobalVar global;
    global.type = parseTypeSuffix(std::move(declaredType));
    global.name = std::move(name);
    global.isExternStorage = isExternStorage;

    if (match(TokenKind::Equal)) {
        global.init = parseInitializer();
    }
    consume(TokenKind::Semicolon, "expected ';' after global declaration");
    return global;
}

TypePtr Parser::parseType() {
    TypePtr type = parseBaseType();
    while (match(TokenKind::Star)) {
        type = Type::makePointer(type);
        if (match(TokenKind::KeywordRestrict)) {
            type->isRestrict = true;
        }
    }
    return type;
}

TypePtr Parser::parseTypeSuffix(TypePtr baseType) {
    if (match(TokenKind::LeftBracket)) {
        if (check(TokenKind::Number)) {
            const Token &lengthToken = advance();
            consume(TokenKind::RightBracket, "expected ']' after array length");
            return Type::makeArray(std::move(baseType), lengthToken.intValue);
        }
        // VLA：运行时表达式指定大小（不在此解析，由调用方处理）
        // 这里返回一个占位类型，实际大小在 parseVariableDeclarator 中处理
        fail(peek(), "expected array length (VLA not supported in this context)");
    }
    return baseType;
}

Parser::ParsedDeclarator Parser::parseVariableDeclarator(TypePtr declaredType) {
    int pointerDepth = 0;
    while (match(TokenKind::Star)) {
        ++pointerDepth;
        match(TokenKind::KeywordRestrict);  // 消耗可选的 restrict 限定符
    }

    if (check(TokenKind::Identifier)) {
        const Token &nameToken = consume(TokenKind::Identifier, "expected variable name");
        TypePtr type = std::move(declaredType);
        for (int i = 0; i < pointerDepth; ++i) {
            type = Type::makePointer(std::move(type));
        }
        // 检查数组后缀（可能是 VLA 或柔性数组成员，支持多维数组 int a[2][3]）
        std::unique_ptr<Expr> vlaSizeExpr;
        std::vector<int> arrayDimensions;
        while (match(TokenKind::LeftBracket)) {
            if (check(TokenKind::RightBracket)) {
                // 柔性数组成员：int data[]
                advance(); // 消费 ']'
                arrayDimensions.push_back(0);
            } else if (check(TokenKind::Number)) {
                const Token &lengthToken = advance();
                consume(TokenKind::RightBracket, "expected ']' after array length");
                arrayDimensions.push_back(lengthToken.intValue);
            } else {
                // VLA：解析运行时表达式
                vlaSizeExpr = parseAssignment();
                consume(TokenKind::RightBracket, "expected ']' after VLA size expression");
                arrayDimensions.push_back(0);
            }
        }
        // 从内向外构建多维数组类型：int a[2][3] → Array(Array(int,3),2)
        for (auto it = arrayDimensions.rbegin(); it != arrayDimensions.rend(); ++it) {
            if (*it == 0) {
                auto vlaType = Type::makeArray(std::move(type), 0);
                vlaType->isVla = true;
                type = std::move(vlaType);
            } else {
                type = Type::makeArray(std::move(type), *it);
            }
        }
        return ParsedDeclarator{nameToken.lexeme, std::move(type), std::move(vlaSizeExpr)};
    }

    if (pointerDepth > 0) {
        fail(peek(), "grouped declarator required after pointer prefix");
    }

    consume(TokenKind::LeftParen, "expected '(' in declarator");
    int groupedPointerDepth = 0;
    while (match(TokenKind::Star)) {
        ++groupedPointerDepth;
        match(TokenKind::KeywordRestrict);  // 消耗可选的 restrict 限定符
    }
    const Token &nameToken = consume(TokenKind::Identifier, "expected variable name");

    bool hasArraySuffix = false;
    int arrayLength = 0;
    std::unique_ptr<Expr> vlaSizeExpr;
    if (match(TokenKind::LeftBracket)) {
        if (check(TokenKind::Number)) {
            const Token &lengthToken = advance();
            consume(TokenKind::RightBracket, "expected ']' after array length");
            hasArraySuffix = true;
            arrayLength = lengthToken.intValue;
        } else {
            // VLA
            vlaSizeExpr = parseAssignment();
            consume(TokenKind::RightBracket, "expected ']' after VLA size expression");
            hasArraySuffix = true;
        }
    }

    consume(TokenKind::RightParen, "expected ')' after declarator");

    TypePtr type = std::move(declaredType);
    for (int i = 0; i < pointerDepth; ++i) {
        type = Type::makePointer(std::move(type));
    }
    if (match(TokenKind::LeftParen)) {
        type = Type::makeFunction(std::move(type), parseFunctionTypeParameters());
        consume(TokenKind::RightParen, "expected ')' after function pointer parameters");
    }
    for (int i = 0; i < groupedPointerDepth; ++i) {
        type = Type::makePointer(std::move(type));
    }
    if (hasArraySuffix) {
        if (vlaSizeExpr) {
            auto vlaType = Type::makeArray(std::move(type), 0);
            vlaType->isVla = true;
            type = std::move(vlaType);
        } else {
            type = Type::makeArray(std::move(type), arrayLength);
        }
    }

    return ParsedDeclarator{nameToken.lexeme, std::move(type), std::move(vlaSizeExpr)};
}

std::vector<TypePtr> Parser::parseFunctionTypeParameters() {
    std::vector<TypePtr> parameters;
    if (check(TokenKind::RightParen)) {
        return parameters;
    }

    if (check(TokenKind::KeywordVoid)) {
        TypePtr probe = parseBaseType();
        if (check(TokenKind::RightParen)) {
            return parameters;
        }
        while (match(TokenKind::Star)) {
            probe = Type::makePointer(probe);
        }
        if (check(TokenKind::Identifier)) {
            advance();
        }
        parameters.push_back(std::move(probe));
    } else {
        TypePtr paramType = parseType();
        if (check(TokenKind::Identifier)) {
            advance();
        }
        parameters.push_back(std::move(paramType));
    }

    while (match(TokenKind::Comma)) {
        TypePtr paramType = parseType();
        if (check(TokenKind::Identifier)) {
            advance();
        }
        parameters.push_back(std::move(paramType));
    }

    return parameters;
}

std::unique_ptr<Expr> Parser::parseInitializer() {
    if (!match(TokenKind::LeftBrace)) {
        return parseAssignment();
    }

    std::vector<std::unique_ptr<Expr>> elements;
    std::vector<std::vector<Designator>> designators;
    bool hasDesignators = false;

    if (!check(TokenKind::RightBrace)) {
        do {
            std::vector<Designator> elemDesignators;
            // 检查指定初始化器：.field = val 或 [index] = val
            while (check(TokenKind::Dot) || check(TokenKind::LeftBracket)) {
                hasDesignators = true;
                if (match(TokenKind::Dot)) {
                    // .field 指定
                    const Token &fieldToken = consume(TokenKind::Identifier, "expected field name after '.'");
                    Designator d;
                    d.kind = Designator::Field;
                    d.fieldName = fieldToken.lexeme;
                    elemDesignators.push_back(std::move(d));
                } else if (check(TokenKind::LeftBracket)) {
                    // [index] 指定
                    advance(); // 消费 '['
                    if (!check(TokenKind::Number)) {
                        fail(peek(), "expected integer constant in designator");
                    }
                    int index = advance().intValue;
                    consume(TokenKind::RightBracket, "expected ']' after designator index");
                    Designator d;
                    d.kind = Designator::Index;
                    d.index = index;
                    elemDesignators.push_back(std::move(d));
                }
            }
            if (!elemDesignators.empty()) {
                consume(TokenKind::Equal, "expected '=' after designator");
            }

            designators.push_back(std::move(elemDesignators));

            if (check(TokenKind::LeftBrace)) {
                elements.push_back(parseInitializer());
            } else {
                elements.push_back(parseAssignment());
            }
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RightBrace, "expected '}' after initializer list");
    auto list = std::make_unique<InitializerListExpr>(std::move(elements));
    if (hasDesignators) {
        list->designators = std::move(designators);
    }
    return list;
}

TypePtr Parser::parseBaseType() {
    // 解析类型限定符和修饰符
    bool typeIsConst = false;
    bool typeIsVolatile = false;
    bool typeIsRestrict = false;
    bool typeIsAtomic = false;
    int typeAlignAs = 0;
    while (true) {
        if (match(TokenKind::KeywordConst)) {
            typeIsConst = true;
        } else if (match(TokenKind::KeywordVolatile)) {
            typeIsVolatile = true;
        } else if (match(TokenKind::KeywordRestrict)) {
            typeIsRestrict = true;
        } else if (match(TokenKind::KeywordAtomic)) {
            typeIsAtomic = true;
        } else if (match(TokenKind::KeywordInline)) {
            // 消耗 inline，不做特殊处理
        } else if (match(TokenKind::KeywordNoreturn)) {
            // 消耗 _Noreturn，不做特殊处理
        } else if (match(TokenKind::KeywordThreadLocal)) {
            // 消耗 _Thread_local，不做特殊处理
        } else if (match(TokenKind::KeywordAlignas)) {
            consume(TokenKind::LeftParen, "expected '(' after '_Alignas'");
            if (isTypeSpecifier(peek().kind) ||
                (check(TokenKind::Identifier) && typedefs.find(peek().lexeme) != typedefs.end())) {
                parseType();
            } else {
                auto alignExpr = parseAssignment();
                if (alignExpr->kind == Expr::Kind::Number) {
                    typeAlignAs = static_cast<NumberExpr *>(alignExpr.get())->value;
                }
            }
            consume(TokenKind::RightParen, "expected ')' after '_Alignas' argument");
        } else {
            break;
        }
    }

    TypePtr result;
    if (match(TokenKind::KeywordStruct)) {
        result = parseStructType();
    } else if (match(TokenKind::KeywordUnion)) {
        result = parseUnionType();
    } else if (match(TokenKind::KeywordUnsigned)) {
        if (match(TokenKind::KeywordChar)) {
            result = Type::makeChar();
            result->isUnsigned = true;
        } else if (match(TokenKind::KeywordShort)) {
            result = Type::makeShort();
            result->isUnsigned = true;
        } else if (match(TokenKind::KeywordLong)) {
            if (match(TokenKind::KeywordLong)) {
                result = Type::makeLongLong();
            } else {
                result = Type::makeLong();
            }
            result->isUnsigned = true;
        } else {
            if (match(TokenKind::KeywordInt)) {
                // fallthrough
            }
            result = Type::makeInt();
            result->isUnsigned = true;
        }
    } else if (match(TokenKind::KeywordSigned)) {
        if (match(TokenKind::KeywordChar)) {
            result = Type::makeChar();
        } else if (match(TokenKind::KeywordShort)) {
            result = Type::makeShort();
        } else if (match(TokenKind::KeywordLong)) {
            if (match(TokenKind::KeywordLong)) {
                result = Type::makeLongLong();
            } else {
                result = Type::makeLong();
            }
        } else {
            if (match(TokenKind::KeywordInt)) {
                // fallthrough
            }
            result = Type::makeInt();
        }
    } else if (match(TokenKind::KeywordChar)) {
        result = Type::makeChar();
    } else if (match(TokenKind::KeywordShort)) {
        result = Type::makeShort();
    } else if (match(TokenKind::KeywordLong)) {
        if (match(TokenKind::KeywordLong)) {
            result = Type::makeLongLong();
        } else if (match(TokenKind::KeywordDouble)) {
            // long double -> 当作 double（MSVC 行为）
            result = Type::makeDouble();
        } else {
            result = Type::makeLong();
        }
    } else if (match(TokenKind::KeywordInt)) {
        result = Type::makeInt();
    } else if (match(TokenKind::KeywordVoid)) {
        result = Type::makeVoid();
    } else if (match(TokenKind::KeywordFloat)) {
        result = Type::makeFloat();
    } else if (match(TokenKind::KeywordDouble)) {
        result = Type::makeDouble();
    } else if (match(TokenKind::KeywordBool)) {
        result = Type::makeBool();
    } else if (match(TokenKind::KeywordEnum)) {
        if (check(TokenKind::Identifier)) {
            advance();
        }
        if (check(TokenKind::LeftBrace)) {
            consume(TokenKind::LeftBrace, "expected '{' after enum");
            int nextValue = 0;
            while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile)) {
                const Token &name = consume(TokenKind::Identifier, "expected enum constant name");
                if (match(TokenKind::Equal)) {
                    if (!check(TokenKind::Number)) {
                        fail(peek(), "expected integer constant for enum value");
                    }
                    nextValue = advance().intValue;
                }
                enumConstants[name.lexeme] = nextValue;
                ++nextValue;
                if (!match(TokenKind::Comma)) {
                    break;
                }
            }
            consume(TokenKind::RightBrace, "expected '}' after enum");
        }
        result = Type::makeInt();
    } else if (check(TokenKind::Identifier)) {
        const std::string &name = peek().lexeme;
        if (name == "__builtin_va_list") {
            advance();
            result = Type::makePointer(Type::makeChar());
        } else {
            auto it = typedefs.find(name);
            if (it != typedefs.end()) {
                advance();
                result = it->second;
            }
        }
    }

    if (!result) {
        fail(peek(), "expected type specifier");
    }

    // 应用类型限定符
    if (typeIsConst) {
        result->isConst = true;
    }
    if (typeIsVolatile) {
        result->isVolatile = true;
    }
    if (typeIsRestrict) {
        result->isRestrict = true;
    }
    if (typeIsAtomic) {
        result->isAtomic = true;
    }
    if (typeAlignAs > 0) {
        result->alignAs = typeAlignAs;
    }

    return result;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    consume(TokenKind::LeftBrace, "expected '{' to start a block");
    auto block = std::make_unique<BlockStmt>();

    while (!check(TokenKind::RightBrace)) {
        if (check(TokenKind::EndOfFile)) {
            fail(peek(), "unterminated block");
        }
        try {
            auto stmt = parseStatement();
            // 展平多声明符产生的嵌套 BlockStmt（不创建新作用域）
            if (stmt->kind == Stmt::Kind::Block) {
                auto &inner = static_cast<BlockStmt &>(*stmt);
                for (auto &s : inner.statements) {
                    block->statements.push_back(std::move(s));
                }
            } else {
                block->statements.push_back(std::move(stmt));
            }
        } catch (const std::runtime_error &) {
            // 语句解析失败，记录错误并同步到下一条语句
            synchronize();
        }
    }

    consume(TokenKind::RightBrace, "expected '}' to close a block");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (match(TokenKind::KeywordReturn)) {
        return parseReturnStatement();
    }
    if (match(TokenKind::KeywordStatic)) {
        auto decl = parseDeclaration(parseType());
        // 标记为 static 局部变量
        if (decl->kind == Stmt::Kind::Decl) {
            static_cast<DeclStmt &>(*decl).isStatic = true;
        }
        return decl;
    }
    if (isTypeSpecifier(peek().kind)) {
        return parseDeclaration(parseType());
    }
    if (check(TokenKind::Identifier) && typedefs.find(peek().lexeme) != typedefs.end()) {
        return parseDeclaration(parseType());
    }
    if (match(TokenKind::KeywordIf)) {
        return parseIfStatement();
    }
    if (match(TokenKind::KeywordWhile)) {
        return parseWhileStatement();
    }
    if (match(TokenKind::KeywordDo)) {
        return parseDoWhileStatement();
    }
    if (match(TokenKind::KeywordSwitch)) {
        return parseSwitchStatement();
    }
    if (match(TokenKind::KeywordFor)) {
        return parseForStatement();
    }
    if (match(TokenKind::KeywordBreak)) {
        return parseBreakStatement();
    }
    if (match(TokenKind::KeywordContinue)) {
        return parseContinueStatement();
    }
    if (match(TokenKind::KeywordGoto)) {
        return parseGotoStatement();
    }
    if (match(TokenKind::KeywordStaticAssert)) {
        return parseStaticAssert();
    }
    // 标签语句：identifier 后跟 ':'
    if (check(TokenKind::Identifier) && current + 1 < tokens.size() && tokens[current + 1].kind == TokenKind::Colon) {
        const Token &labelToken = advance();
        std::string labelName = labelToken.lexeme;
        consume(TokenKind::Colon, "expected ':' after label name");
        auto body = parseStatement();
        auto labelStmt = std::make_unique<LabelStmt>(std::move(labelName), std::move(body));
        labelStmt->line = labelToken.line;
        labelStmt->column = labelToken.column;
        return labelStmt;
    }
    if (check(TokenKind::LeftBrace)) {
        return parseBlock();
    }

    return parseExpressionStatement();
}

std::unique_ptr<Stmt> Parser::parseForStatement() {
    const Token &forToken = previous();
    consume(TokenKind::LeftParen, "expected '(' after 'for'");

    std::unique_ptr<Stmt> init;
    if (match(TokenKind::Semicolon)) {
    } else if (isTypeSpecifier(peek().kind)) {
        init = parseDeclaration(parseType());
    } else {
        auto initExpr = parseExpression();
        int initLine = initExpr->line;
        int initColumn = initExpr->column;
        consume(TokenKind::Semicolon, "expected ';' after for initializer");
        auto exprStmt = std::make_unique<ExprStmt>(std::move(initExpr));
        exprStmt->line = initLine;
        exprStmt->column = initColumn;
        init = std::move(exprStmt);
    }

    std::unique_ptr<Expr> condition;
    if (!check(TokenKind::Semicolon)) {
        condition = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after for condition");

    std::unique_ptr<Expr> update;
    if (!check(TokenKind::RightParen)) {
        update = parseExpression();
    }
    consume(TokenKind::RightParen, "expected ')' after for clauses");

    auto forStmt = std::make_unique<ForStmt>(std::move(init), std::move(condition), std::move(update), parseStatement());
    forStmt->line = forToken.line;
    forStmt->column = forToken.column;
    return forStmt;
}

std::unique_ptr<Stmt> Parser::parseReturnStatement() {
    const Token &returnToken = previous();
    if (match(TokenKind::Semicolon)) {
        auto stmt = std::make_unique<ReturnStmt>(nullptr);
        stmt->line = returnToken.line;
        stmt->column = returnToken.column;
        return stmt;
    }

    auto expr = parseExpression();
    consume(TokenKind::Semicolon, "expected ';' after return value");
    auto stmt = std::make_unique<ReturnStmt>(std::move(expr));
    stmt->line = returnToken.line;
    stmt->column = returnToken.column;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseDeclaration(TypePtr declaredType) {
    auto makeDecl = [&](const TypePtr &baseType) -> std::unique_ptr<DeclStmt> {
        const Token &nameTok = peek();
        ParsedDeclarator declarator = parseVariableDeclarator(baseType);
        std::unique_ptr<Expr> initializer;
        if (match(TokenKind::Equal)) {
            initializer = parseInitializer();
        }
        auto decl = std::make_unique<DeclStmt>(
            std::move(declarator.type),
            std::move(declarator.name),
            std::move(initializer));
        decl->vlaSizeExpr = std::move(declarator.vlaSizeExpr);
        decl->line = nameTok.line;
        decl->column = nameTok.column;
        return decl;
    };

    auto first = makeDecl(declaredType);

    // 检查是否有多个声明符：int a, b, c;
    if (!check(TokenKind::Semicolon)) {
        // 多声明符：收集到 BlockStmt 中
        auto block = std::make_unique<BlockStmt>();
        block->statements.push_back(std::move(first));
        while (match(TokenKind::Comma)) {
            block->statements.push_back(makeDecl(declaredType));
        }
        consume(TokenKind::Semicolon, "expected ';' after declaration");
        return block;
    }

    consume(TokenKind::Semicolon, "expected ';' after declaration");
    return first;
}

std::unique_ptr<Stmt> Parser::parseIfStatement() {
    const Token &ifToken = previous();
    consume(TokenKind::LeftParen, "expected '(' after 'if'");
    auto condition = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after if condition");

    auto thenBranch = parseStatement();
    std::unique_ptr<Stmt> elseBranch;
    if (match(TokenKind::KeywordElse)) {
        elseBranch = parseStatement();
    }

    auto ifStmt = std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
    ifStmt->line = ifToken.line;
    ifStmt->column = ifToken.column;
    return ifStmt;
}

std::unique_ptr<Stmt> Parser::parseWhileStatement() {
    const Token &whileToken = previous();
    consume(TokenKind::LeftParen, "expected '(' after 'while'");
    auto condition = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after while condition");
    auto whileStmt = std::make_unique<WhileStmt>(std::move(condition), parseStatement());
    whileStmt->line = whileToken.line;
    whileStmt->column = whileToken.column;
    return whileStmt;
}

std::unique_ptr<Stmt> Parser::parseDoWhileStatement() {
    const Token &doToken = previous();
    auto body = parseStatement();
    consume(TokenKind::KeywordWhile, "expected 'while' after 'do' body");
    consume(TokenKind::LeftParen, "expected '(' after 'while'");
    auto condition = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after while condition");
    consume(TokenKind::Semicolon, "expected ';' after 'do-while'");
    auto doWhileStmt = std::make_unique<DoWhileStmt>(std::move(body), std::move(condition));
    doWhileStmt->line = doToken.line;
    doWhileStmt->column = doToken.column;
    return doWhileStmt;
}

std::unique_ptr<Stmt> Parser::parseSwitchStatement() {
    consume(TokenKind::LeftParen, "expected '(' after 'switch'");
    auto scrutinee = parseExpression();
    consume(TokenKind::RightParen, "expected ')' after switch expression");
    consume(TokenKind::LeftBrace, "expected '{' to open switch body");

    std::vector<SwitchCase> cases;
    std::unique_ptr<Stmt> defaultBody;

    while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile)) {
        if (match(TokenKind::KeywordCase)) {
            auto label = parseExpression();
            consume(TokenKind::Colon, "expected ':' after case label");
            auto body = parseCaseBody();
            cases.push_back({std::move(label), std::move(body)});
        } else if (match(TokenKind::KeywordDefault)) {
            consume(TokenKind::Colon, "expected ':' after 'default'");
            defaultBody = parseCaseBody();
        } else {
            fail(peek(), "expected 'case' or 'default' in switch body");
        }
    }
    consume(TokenKind::RightBrace, "expected '}' to close switch body");
    return std::make_unique<SwitchStmt>(
        std::move(scrutinee), std::move(cases), std::move(defaultBody));
}

std::unique_ptr<Stmt> Parser::parseCaseBody() {
    // 兼容花括号写法
    if (check(TokenKind::LeftBrace)) {
        return parseBlock();
    }

    // 解析语句列表，直到遇到 case、default、} 或 EOF
    auto block = std::make_unique<BlockStmt>();
    while (!check(TokenKind::RightBrace) &&
           !check(TokenKind::KeywordCase) &&
           !check(TokenKind::KeywordDefault) &&
           !check(TokenKind::EndOfFile)) {
        block->statements.push_back(parseStatement());
    }

    // 如果只有一个语句，直接返回该语句
    if (block->statements.size() == 1) {
        return std::move(block->statements[0]);
    }
    // 如果没有语句，返回空块
    if (block->statements.empty()) {
        return std::move(block);
    }
    return std::move(block);
}

std::unique_ptr<Stmt> Parser::parseBreakStatement() {
    const Token &breakToken = previous();
    consume(TokenKind::Semicolon, "expected ';' after 'break'");
    auto stmt = std::make_unique<BreakStmt>();
    stmt->line = breakToken.line;
    stmt->column = breakToken.column;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseContinueStatement() {
    const Token &continueToken = previous();
    consume(TokenKind::Semicolon, "expected ';' after 'continue'");
    auto stmt = std::make_unique<ContinueStmt>();
    stmt->line = continueToken.line;
    stmt->column = continueToken.column;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseGotoStatement() {
    const Token &gotoToken = previous();
    const Token &nameToken = consume(TokenKind::Identifier, "expected label name after 'goto'");
    consume(TokenKind::Semicolon, "expected ';' after goto target");
    auto stmt = std::make_unique<GotoStmt>(nameToken.lexeme);
    stmt->line = gotoToken.line;
    stmt->column = gotoToken.column;
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseStaticAssert() {
    consume(TokenKind::LeftParen, "expected '(' after '_Static_assert'");
    auto condition = parseAssignment();
    std::string message;
    // C23 改进：message 可选，仅在有逗号时解析
    if (match(TokenKind::Comma)) {
        const Token &msgToken = consume(TokenKind::StringLiteral, "expected string literal as _Static_assert message");
        message = msgToken.stringValue;
    }
    consume(TokenKind::RightParen, "expected ')' after _Static_assert");
    consume(TokenKind::Semicolon, "expected ';' after _Static_assert");
    return std::make_unique<StaticAssertStmt>(std::move(condition), std::move(message));
}

std::unique_ptr<Expr> Parser::parseGeneric() {
    consume(TokenKind::LeftParen, "expected '(' after '_Generic'");
    auto controllingExpr = parseAssignment();
    consume(TokenKind::Comma, "expected ',' after _Generic controlling expression");

    std::vector<GenericAssociation> associations;
    while (!check(TokenKind::RightParen)) {
        GenericAssociation assoc;
        if (match(TokenKind::KeywordDefault)) {
            assoc.type = nullptr;
        } else {
            assoc.type = parseType();
        }
        consume(TokenKind::Colon, "expected ':' after type name in _Generic association");
        assoc.expr = parseAssignment();
        associations.push_back(std::move(assoc));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    consume(TokenKind::RightParen, "expected ')' after _Generic associations");
    return std::make_unique<GenericExpr>(std::move(controllingExpr), std::move(associations));
}

std::unique_ptr<Stmt> Parser::parseExpressionStatement() {
    auto expr = parseExpression();
    int exprLine = expr->line;
    int exprColumn = expr->column;
    consume(TokenKind::Semicolon, "expected ';' after expression");
    auto stmt = std::make_unique<ExprStmt>(std::move(expr));
    stmt->line = exprLine;
    stmt->column = exprColumn;
    return stmt;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    auto expr = parseAssignment();

    // 逗号运算符：优先级最低（低于赋值）
    while (match(TokenKind::Comma)) {
        expr = std::make_unique<BinaryExpr>(BinaryOp::Comma, std::move(expr), parseAssignment());
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto lhs = parseTernary();

    struct CompoundOp { TokenKind kind; BinaryOp op; };
    static const CompoundOp compoundOps[] = {
        {TokenKind::PlusEqual, BinaryOp::Add},
        {TokenKind::MinusEqual, BinaryOp::Subtract},
        {TokenKind::StarEqual, BinaryOp::Multiply},
        {TokenKind::SlashEqual, BinaryOp::Divide},
        {TokenKind::PercentEqual, BinaryOp::Modulo},
        {TokenKind::LessLessEqual, BinaryOp::ShiftLeft},
        {TokenKind::GreaterGreaterEqual, BinaryOp::ShiftRight},
        {TokenKind::AmpEqual, BinaryOp::BitwiseAnd},
        {TokenKind::CaretEqual, BinaryOp::BitwiseXor},
        {TokenKind::PipeEqual, BinaryOp::BitwiseOr},
    };

    for (const auto &co : compoundOps) {
        if (check(co.kind)) {
            advance(); // 消耗复合赋值运算符
            const Token &op = previous();
            auto value = parseAssignment();
            auto assign = std::make_unique<AssignExpr>(std::move(lhs), std::move(value));
            assign->isCompound = true;
            assign->compoundOp = co.op;
            assign->line = op.line;
            assign->column = op.column;
            return assign;
        }
    }

    if (!match(TokenKind::Equal)) {
        return lhs;
    }

    const Token &eq = previous();
    auto value = parseAssignment();
    auto assign = std::make_unique<AssignExpr>(std::move(lhs), std::move(value));
    assign->line = eq.line;
    assign->column = eq.column;
    return assign;
}

std::unique_ptr<Expr> Parser::parseTernary() {
    auto condition = parseLogicalOr();
    if (!match(TokenKind::Question)) {
        return condition;
    }
    const Token &question = previous();
    auto thenExpr = parseExpression();
    consume(TokenKind::Colon, "expected ':' in ternary expression");
    auto elseExpr = parseAssignment();
    auto ternary = std::make_unique<TernaryExpr>(std::move(condition), std::move(thenExpr), std::move(elseExpr));
    ternary->line = question.line;
    ternary->column = question.column;
    return ternary;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (match(TokenKind::PipePipe)) {
        const Token &op = previous();
        auto binary = std::make_unique<BinaryExpr>(BinaryOp::LogicalOr, std::move(expr), parseLogicalAnd());
        binary->line = op.line;
        binary->column = op.column;
        expr = std::move(binary);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseBitwiseOr();

    while (match(TokenKind::AmpAmp)) {
        const Token &op = previous();
        auto binary = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(expr), parseBitwiseOr());
        binary->line = op.line;
        binary->column = op.column;
        expr = std::move(binary);
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();
    while (match(TokenKind::Pipe)) {
        const Token &op = previous();
        auto binary = std::make_unique<BinaryExpr>(BinaryOp::BitwiseOr, std::move(expr), parseBitwiseXor());
        binary->line = op.line;
        binary->column = op.column;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();
    while (match(TokenKind::Caret)) {
        const Token &op = previous();
        auto binary = std::make_unique<BinaryExpr>(BinaryOp::BitwiseXor, std::move(expr), parseBitwiseAnd());
        binary->line = op.line;
        binary->column = op.column;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseBitwiseAnd() {
    auto expr = parseEquality();
    while (match(TokenKind::Ampersand)) {
        const Token &op = previous();
        auto binary = std::make_unique<BinaryExpr>(BinaryOp::BitwiseAnd, std::move(expr), parseEquality());
        binary->line = op.line;
        binary->column = op.column;
        expr = std::move(binary);
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    auto expr = parseComparison();

    while (true) {
        if (match(TokenKind::EqualEqual)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Equal, std::move(expr), parseComparison());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::BangEqual)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::NotEqual, std::move(expr), parseComparison());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto expr = parseShift();

    while (true) {
        if (match(TokenKind::Less)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Less, std::move(expr), parseShift());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::LessEqual)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::LessEqual, std::move(expr), parseShift());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::Greater)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Greater, std::move(expr), parseShift());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::GreaterEqual)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::GreaterEqual, std::move(expr), parseShift());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto expr = parseTerm();
    while (true) {
        if (match(TokenKind::LessLess)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::ShiftLeft, std::move(expr), parseTerm());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::GreaterGreater)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::ShiftRight, std::move(expr), parseTerm());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseTerm() {
    auto expr = parseFactor();

    while (true) {
        if (match(TokenKind::Plus)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Add, std::move(expr), parseFactor());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::Minus)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Subtract, std::move(expr), parseFactor());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseFactor() {
    auto expr = parseUnary();

    while (true) {
        if (match(TokenKind::Star)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Multiply, std::move(expr), parseUnary());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::Slash)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Divide, std::move(expr), parseUnary());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else if (match(TokenKind::Percent)) {
            const Token &op = previous();
            auto binary = std::make_unique<BinaryExpr>(BinaryOp::Modulo, std::move(expr), parseUnary());
            binary->line = op.line;
            binary->column = op.column;
            expr = std::move(binary);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (match(TokenKind::PlusPlus)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::PreIncrement, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::MinusMinus)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::PreDecrement, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Plus)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::Plus, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Minus)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::Minus, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Bang)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::LogicalNot, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Tilde)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::BitwiseNot, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Ampersand)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::AddressOf, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::Star)) {
        const Token &op = previous();
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::Dereference, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::KeywordSizeof)) {
        const Token &op = previous();
        if (match(TokenKind::LeftParen)) {
            if (isTypeSpecifier(peek().kind) || check(TokenKind::KeywordStruct)) {
                auto expr = std::make_unique<UnaryExpr>(UnaryOp::Sizeof, nullptr);
                expr->sizeofType = parseType();
                consume(TokenKind::RightParen, "expected ')' after sizeof type");
                expr->line = op.line;
                expr->column = op.column;
                return expr;
            }
            auto expr = std::make_unique<UnaryExpr>(UnaryOp::Sizeof, parseExpression());
            consume(TokenKind::RightParen, "expected ')' after sizeof expr");
            expr->line = op.line;
            expr->column = op.column;
            return expr;
        }
        auto expr = std::make_unique<UnaryExpr>(UnaryOp::Sizeof, parseUnary());
        expr->line = op.line;
        expr->column = op.column;
        return expr;
    }
    if (match(TokenKind::KeywordAlignof)) {
        const Token &op = previous();
        consume(TokenKind::LeftParen, "expected '(' after '_Alignof'");
        if (isTypeSpecifier(peek().kind) || check(TokenKind::KeywordStruct) || check(TokenKind::KeywordUnion)) {
            auto expr = std::make_unique<UnaryExpr>(UnaryOp::Alignof, nullptr);
            expr->sizeofType = parseType();
            consume(TokenKind::RightParen, "expected ')' after _Alignof type");
            expr->line = op.line;
            expr->column = op.column;
            return expr;
        }
        fail(peek(), "expected type name after '_Alignof('");
    }

    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (match(TokenKind::LeftParen)) {
            int callLine = previous().line;
            int callColumn = previous().column;
            std::vector<std::unique_ptr<Expr>> arguments;
            if (!check(TokenKind::RightParen)) {
                do {
                    arguments.push_back(parseAssignment());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RightParen, "expected ')' after function arguments");
            auto callExpr = std::make_unique<CallExpr>(std::move(expr), std::move(arguments));
            callExpr->line = callLine;
            callExpr->column = callColumn;
            expr = std::move(callExpr);
            continue;
        }

        if (match(TokenKind::LeftBracket)) {
            int indexLine = previous().line;
            int indexColumn = previous().column;
            auto index = parseExpression();
            consume(TokenKind::RightBracket, "expected ']' after subscript");
            auto indexExpr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
            indexExpr->line = indexLine;
            indexExpr->column = indexColumn;
            expr = std::move(indexExpr);
            continue;
        }

        if (match(TokenKind::Dot)) {
            int memberLine = previous().line;
            int memberColumn = previous().column;
            const Token &memberToken = consume(TokenKind::Identifier, "expected member name after '.'");
            auto memberExpr = std::make_unique<MemberAccessExpr>(std::move(expr), memberToken.lexeme);
            memberExpr->line = memberLine;
            memberExpr->column = memberColumn;
            expr = std::move(memberExpr);
            continue;
        }

        if (match(TokenKind::Arrow)) {
            int arrowLine = previous().line;
            int arrowColumn = previous().column;
            const Token &memberToken = consume(TokenKind::Identifier, "expected member name after '->'");
            auto dereference = std::make_unique<UnaryExpr>(UnaryOp::Dereference, std::move(expr));
            dereference->line = arrowLine;
            dereference->column = arrowColumn;
            auto memberExpr = std::make_unique<MemberAccessExpr>(std::move(dereference), memberToken.lexeme);
            memberExpr->line = arrowLine;
            memberExpr->column = arrowColumn;
            expr = std::move(memberExpr);
            continue;
        }

        if (match(TokenKind::PlusPlus)) {
            const Token &op = previous();
            auto postInc = std::make_unique<UnaryExpr>(UnaryOp::PostIncrement, std::move(expr));
            postInc->isPostfix = true;
            postInc->line = op.line;
            postInc->column = op.column;
            expr = std::move(postInc);
            continue;
        }

        if (match(TokenKind::MinusMinus)) {
            const Token &op = previous();
            auto postDec = std::make_unique<UnaryExpr>(UnaryOp::PostDecrement, std::move(expr));
            postDec->isPostfix = true;
            postDec->line = op.line;
            postDec->column = op.column;
            expr = std::move(postDec);
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (match(TokenKind::Number)) {
        auto expr = std::make_unique<NumberExpr>(previous().intValue);
        expr->line = previous().line;
        expr->column = previous().column;
        return expr;
    }
    if (match(TokenKind::FloatLiteral)) {
        auto expr = std::make_unique<FloatLiteralExpr>(previous().doubleValue);
        expr->line = previous().line;
        expr->column = previous().column;
        return expr;
    }
    if (match(TokenKind::KeywordGeneric)) {
        return parseGeneric();
    }
    if (match(TokenKind::StringLiteral)) {
        auto expr = std::make_unique<StringExpr>(previous().stringValue);
        expr->line = previous().line;
        expr->column = previous().column;
        return expr;
    }
    if (match(TokenKind::Identifier)) {
        const Token &idToken = previous();
        const std::string &name = idToken.lexeme;
        // 变长参数内建函数
        if (name == "__builtin_va_start") {
            consume(TokenKind::LeftParen, "expected '(' after __builtin_va_start");
            auto ap = parseAssignment();
            consume(TokenKind::Comma, "expected ',' in __builtin_va_start");
            const Token &lastParam = consume(TokenKind::Identifier, "expected parameter name in __builtin_va_start");
            consume(TokenKind::RightParen, "expected ')' after __builtin_va_start");
            return std::make_unique<BuiltinVaStartExpr>(std::move(ap), lastParam.lexeme);
        }
        if (name == "__builtin_va_arg") {
            consume(TokenKind::LeftParen, "expected '(' after __builtin_va_arg");
            auto ap = parseAssignment();
            consume(TokenKind::Comma, "expected ',' in __builtin_va_arg");
            TypePtr argType = parseType();
            consume(TokenKind::RightParen, "expected ')' after __builtin_va_arg");
            return std::make_unique<BuiltinVaArgExpr>(std::move(ap), std::move(argType));
        }
        if (name == "__builtin_va_end") {
            consume(TokenKind::LeftParen, "expected '(' after __builtin_va_end");
            auto ap = parseAssignment();
            consume(TokenKind::RightParen, "expected ')' after __builtin_va_end");
            return std::make_unique<BuiltinVaEndExpr>(std::move(ap));
        }
        auto enumIt = enumConstants.find(name);
        if (enumIt != enumConstants.end()) {
            auto expr = std::make_unique<NumberExpr>(enumIt->second);
            expr->line = idToken.line;
            expr->column = idToken.column;
            return expr;
        }
        auto expr = std::make_unique<VariableExpr>(name);
        expr->line = idToken.line;
        expr->column = idToken.column;
        return expr;
    }
    if (match(TokenKind::LeftParen)) {
        // 类型转换或复合字面量：(type)expr 或 (type){init}
        if (isTypeSpecifier(peek().kind) ||
            (check(TokenKind::Identifier) && typedefs.find(peek().lexeme) != typedefs.end())) {
            TypePtr targetType = parseType();
            // 解析数组后缀（如 int[]、int[3]）
            while (match(TokenKind::LeftBracket)) {
                if (check(TokenKind::Number)) {
                    int length = advance().intValue;
                    consume(TokenKind::RightBracket, "expected ']' after array length");
                    targetType = Type::makeArray(std::move(targetType), length);
                } else {
                    consume(TokenKind::RightBracket, "expected ']'");
                    targetType = Type::makeArray(std::move(targetType), 0);
                }
            }
            consume(TokenKind::RightParen, "expected ')' after type");
            // 复合字面量：(type){init}
            if (check(TokenKind::LeftBrace)) {
                auto init = parseInitializer();
                if (init->kind != Expr::Kind::InitializerList) {
                    fail(peek(), "expected initializer list for compound literal");
                }
                auto &list = static_cast<InitializerListExpr &>(*init);
                auto compoundLit = std::make_unique<CompoundLiteralExpr>(
                    std::move(targetType),
                    std::make_unique<InitializerListExpr>(std::move(list.elements)));
                compoundLit->init->designators = std::move(list.designators);
                return compoundLit;
            }
            // 类型转换
            auto operand = parseUnary();
            return std::make_unique<CastExpr>(std::move(targetType), std::move(operand));
        }
        auto expr = parseExpression();
        consume(TokenKind::RightParen, "expected ')' after expression");
        return expr;
    }

    fail(peek(), "expected expression");
}

bool Parser::isTypeSpecifier(TokenKind kind) const {
    return kind == TokenKind::KeywordStruct ||
        kind == TokenKind::KeywordChar ||
        kind == TokenKind::KeywordShort ||
        kind == TokenKind::KeywordInt ||
        kind == TokenKind::KeywordLong ||
        kind == TokenKind::KeywordVoid ||
        kind == TokenKind::KeywordUnsigned ||
        kind == TokenKind::KeywordSigned ||
        kind == TokenKind::KeywordEnum ||
        kind == TokenKind::KeywordUnion ||
        kind == TokenKind::KeywordConst ||
        kind == TokenKind::KeywordVolatile ||
        kind == TokenKind::KeywordFloat ||
        kind == TokenKind::KeywordDouble ||
        kind == TokenKind::KeywordBool ||
        kind == TokenKind::KeywordRestrict ||
        kind == TokenKind::KeywordInline ||
        kind == TokenKind::KeywordNoreturn ||
        kind == TokenKind::KeywordAtomic ||
        kind == TokenKind::KeywordAlignas ||
        kind == TokenKind::KeywordThreadLocal;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }

    advance();
    return true;
}

bool Parser::check(TokenKind kind) const {
    return peek().kind == kind;
}

const Token &Parser::advance() {
    if (current < tokens.size()) {
        ++current;
    }
    return previous();
}

const Token &Parser::peek() const {
    return tokens[current];
}

const Token &Parser::previous() const {
    return tokens[current - 1];
}

const Token &Parser::consume(TokenKind kind, const char *message) {
    if (check(kind)) {
        return advance();
    }

    fail(peek(), message);
}

[[noreturn]] void Parser::fail(const Token &token, const std::string &message) const {
    const std::string fullMessage =
        message + " near '" + tokenKindName(token.kind) + "'";
    if (diag) {
        diag->error(token.line, token.column, fullMessage);
    }
    throw std::runtime_error(
        "Parser error at line " + std::to_string(token.line) + ", column " + std::to_string(token.column) +
        ": " + fullMessage);
}

void Parser::diagError(const Token &token, const std::string &message) {
    hasParseErrors = true;
    if (diag) {
        diag->error(token.line, token.column, message);
    }
}

void Parser::synchronize() {
    // 跳过 token 直到找到语句边界（分号或右花括号）
    while (!check(TokenKind::EndOfFile)) {
        if (check(TokenKind::Semicolon)) {
            advance(); // 消耗分号
            return;
        }
        if (check(TokenKind::RightBrace)) {
            advance(); // 消耗右花括号，避免无限循环
            return;
        }
        advance();
    }
}
