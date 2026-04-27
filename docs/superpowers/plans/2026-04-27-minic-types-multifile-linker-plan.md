# minic Types, Multi-File Compilation, and Linker Evolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand `minic` to support common built-in scalar types, compile multiple `.c` files into one executable, and introduce internal artifact and link planning abstractions that preserve the current staged compiler architecture.

**Architecture:** The implementation keeps lexing, parsing, semantics, code generation, and toolchain invocation separated. We first refactor the type model and integer-family semantics, then make code generation width-aware, then introduce multi-translation-unit driver and link planning, while continuing to use MASM and `link.exe` as the phase-1 backend.

**Tech Stack:** C++17, CMake, Windows x64 MASM, Microsoft `link.exe`, PowerShell, manual sample-program validation under `input/`.

---

## File Structure

Planned file responsibilities for this implementation:

- Modify: `src/Token.h`
  - add token kinds for newly supported type keywords.
- Modify: `src/Lexer.cpp`
  - recognize the added keywords and preserve existing identifier behavior.
- Modify: `src/Ast.h`
  - replace the narrow `int`-only scalar model with a richer scalar representation while keeping pointer and array types.
- Modify: `src/Parser.cpp`
  - parse the supported built-in type spellings.
- Modify: `src/Parser.h`
  - declare any helper methods needed for richer type parsing.
- Modify: `src/Semantics.h`
  - define the new helper APIs for type classification, conversions, and build-wide signature aggregation.
- Modify: `src/Semantics.cpp`
  - implement integer-family rules, pointer compatibility rules, and whole-build function consistency checks.
- Modify: `src/CodeGenerator.h`
  - declare width-aware helpers and any translation-unit metadata hooks needed by the driver.
- Modify: `src/CodeGenerator.cpp`
  - emit loads, stores, arithmetic, and calls using actual scalar widths instead of assuming `int`.
- Modify: `src/Driver.h`
  - expand CLI options from one input file to many and add artifact planning structures.
- Modify: `src/Driver.cpp`
  - compile each translation unit independently, aggregate symbols, assemble all objects, and link them together.
- Modify: `src/Toolchain.h`
  - split the toolchain API into assembly and linking operations over multiple inputs.
- Modify: `src/Toolchain.cpp`
  - implement `assemble` and `linkObjects` helpers using `ml64.exe` and `link.exe`.
- Create: `src/Artifacts.h`
  - define translation-unit artifact and link-plan data objects.
- Create: `input/types_signed_unsigned.c`
  - validate narrow signed and unsigned integer behavior.
- Create: `input/types_long_long.c`
  - validate `long long` arithmetic and return paths.
- Create: `input/types_pointer_stride.c`
  - validate pointer arithmetic with non-`int` element sizes.
- Create: `input/multifile_math_a.c`
  - one side of a cross-file call sample.
- Create: `input/multifile_math_b.c`
  - the other side of a cross-file call sample containing `main`.
- Create: `input/multifile_conflict_a.c`
  - negative sample for conflicting declarations.
- Create: `input/multifile_conflict_b.c`
  - paired negative sample for conflicting declarations.
- Modify: `README.md`
  - document the new supported type boundary and multi-file invocation syntax after the implementation is working.

## Task 1: Extend Keywords and Type Representation

**Files:**
- Modify: `src/Token.h`
- Modify: `src/Lexer.cpp`
- Modify: `src/Ast.h`

- [ ] **Step 1: Add token kinds for all supported built-in type keywords**

```cpp
enum class TokenKind {
    // ...
    KeywordVoid,
    KeywordBool,
    KeywordChar,
    KeywordShort,
    KeywordInt,
    KeywordLong,
    KeywordSigned,
    KeywordUnsigned,
    KeywordFloat,
    KeywordDouble,
    // ...
};
```

- [ ] **Step 2: Update the lexer keyword table to recognize the new type names**

```cpp
if (identifier == "void") return makeToken(TokenKind::KeywordVoid);
if (identifier == "_Bool") return makeToken(TokenKind::KeywordBool);
if (identifier == "char") return makeToken(TokenKind::KeywordChar);
if (identifier == "short") return makeToken(TokenKind::KeywordShort);
if (identifier == "int") return makeToken(TokenKind::KeywordInt);
if (identifier == "long") return makeToken(TokenKind::KeywordLong);
if (identifier == "signed") return makeToken(TokenKind::KeywordSigned);
if (identifier == "unsigned") return makeToken(TokenKind::KeywordUnsigned);
if (identifier == "float") return makeToken(TokenKind::KeywordFloat);
if (identifier == "double") return makeToken(TokenKind::KeywordDouble);
```

- [ ] **Step 3: Refactor `Type` to represent scalars explicitly**

```cpp
enum class TypeKind {
    Void,
    Scalar,
    Pointer,
    Array
};

enum class ScalarKind {
    Bool,
    Char,
    Short,
    Int,
    LongLong,
    Float,
    Double
};

struct ScalarInfo {
    ScalarKind kind;
    bool isUnsigned = false;
    int size = 0;
};
```

- [ ] **Step 4: Add factory helpers and classification helpers on `Type`**

```cpp
static TypePtr makeBool();
static TypePtr makeChar(bool isUnsigned);
static TypePtr makeShort(bool isUnsigned);
static TypePtr makeInt(bool isUnsigned = false);
static TypePtr makeLongLong(bool isUnsigned);
static TypePtr makeFloat();
static TypePtr makeDouble();

bool isScalar() const;
bool isInteger() const;
bool isFloating() const;
bool isSignedInteger() const;
bool isUnsignedInteger() const;
int valueSize() const;
```

- [ ] **Step 5: Run a focused build to catch declaration breakage early**

Run: `cmake --build --preset debug`

Expected: the build fails only in places that still assume the old `TypeKind::Int` shape.

- [ ] **Step 6: Commit the structural type-model changes**

```bash
git add src/Token.h src/Lexer.cpp src/Ast.h
git commit -m "refactor: expand scalar type representation"
```

## Task 2: Parse Supported Built-In Type Spellings

**Files:**
- Modify: `src/Parser.h`
- Modify: `src/Parser.cpp`

- [ ] **Step 1: Add parser helpers for normalized built-in type parsing**

```cpp
TypePtr parseType();
TypePtr parseTypeSpecifier();
TypePtr parseNumericTypeSpecifier();
bool isTypeSpecifier(TokenKind kind) const;
```

- [ ] **Step 2: Implement parsing for the supported type spelling table**

```cpp
if (match(TokenKind::KeywordVoid)) {
    return Type::makeVoid();
}
if (match(TokenKind::KeywordBool)) {
    return Type::makeBool();
}
if (match(TokenKind::KeywordFloat)) {
    return Type::makeFloat();
}
if (match(TokenKind::KeywordDouble)) {
    return Type::makeDouble();
}
```

- [ ] **Step 3: Parse signedness and width combinations explicitly instead of accepting arbitrary keyword order**

```cpp
const bool sawSigned = match(TokenKind::KeywordSigned);
const bool sawUnsigned = !sawSigned && match(TokenKind::KeywordUnsigned);

if (match(TokenKind::KeywordChar)) {
    return Type::makeChar(sawUnsigned);
}
if (match(TokenKind::KeywordShort)) {
    match(TokenKind::KeywordInt);
    return Type::makeShort(sawUnsigned);
}
if (match(TokenKind::KeywordInt)) {
    return Type::makeInt(sawUnsigned);
}
if (match(TokenKind::KeywordLong)) {
    consume(TokenKind::KeywordLong, "expected second 'long' in 'long long'");
    match(TokenKind::KeywordInt);
    return Type::makeLongLong(sawUnsigned);
}
```

- [ ] **Step 4: Reject unsupported or malformed combinations with clear parser errors**

```cpp
if (sawSigned && sawUnsigned) {
    fail(peek(), "cannot combine 'signed' and 'unsigned'");
}
fail(peek(), "expected supported type specifier");
```

- [ ] **Step 5: Rebuild after parser work**

Run: `cmake --build --preset debug`

Expected: parser compiles, later phases may still fail until semantics are updated for the new type shape.

- [ ] **Step 6: Commit the parser expansion**

```bash
git add src/Parser.h src/Parser.cpp
git commit -m "feat: parse expanded builtin scalar types"
```

## Task 3: Implement Scalar Semantics and Function Signature Canonicalization

**Files:**
- Modify: `src/Semantics.h`
- Modify: `src/Semantics.cpp`

- [ ] **Step 1: Add semantic helper APIs for scalar compatibility and conversions**

```cpp
TypePtr decayType(const TypePtr &type) const;
TypePtr integerPromote(const TypePtr &type) const;
TypePtr usualArithmeticConversion(const TypePtr &left, const TypePtr &right) const;
bool isNullLikeIntegerLiteral(const Expr &expr) const;
bool canAssign(const TypePtr &target, const TypePtr &value) const;
bool areCompatiblePointers(const TypePtr &left, const TypePtr &right) const;
std::string typeName(const TypePtr &type) const;
```

- [ ] **Step 2: Update local declaration validation for the new supported array element types**

```cpp
if (decl.type->isVoid()) {
    fail("variable cannot have type void: " + decl.name);
}
if (decl.type->isArray() && decl.type->elementType->isVoid()) {
    fail("array element type cannot be void: " + decl.name);
}
if (decl.type->isArray() && !decl.type->elementType->isScalar() && !decl.type->elementType->isPointer()) {
    fail("unsupported array element type: " + decl.name);
}
```

- [ ] **Step 3: Use arithmetic conversion helpers when typing unary and binary expressions**

```cpp
TypePtr leftType = decayType(binary.left->type);
TypePtr rightType = decayType(binary.right->type);

if (binary.op == BinaryOp::Add || binary.op == BinaryOp::Subtract) {
    if (leftType->isScalar() && rightType->isScalar() &&
        leftType->isInteger() && rightType->isInteger()) {
        expr.type = usualArithmeticConversion(leftType, rightType);
        expr.isLValue = false;
        return;
    }
}
```

- [ ] **Step 4: Preserve pointer compatibility rules and reject invalid `void*` dereference**

```cpp
if (unary.op == UnaryOp::Dereference) {
    TypePtr operandType = decayType(unary.operand->type);
    if (!operandType->isPointer()) {
        fail("dereference requires pointer operand");
    }
    if (operandType->elementType->isVoid()) {
        fail("cannot dereference void*");
    }
}
```

- [ ] **Step 5: Build a canonical function signature representation and reuse it across translation units**

```cpp
struct FunctionSignature {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
};
```

- [ ] **Step 6: Add a build-wide aggregation entry point**

```cpp
void analyzeTranslationUnit(Program &program, TranslationUnitResult &result);
void mergeFunctionSignatures(
    const std::vector<TranslationUnitResult> &units,
    std::unordered_map<std::string, FunctionSignature> &functions) const;
```

- [ ] **Step 7: Rebuild after semantic refactor**

Run: `cmake --build --preset debug`

Expected: semantics compile, code generation may still fail where it assumes every scalar is `int`.

- [ ] **Step 8: Commit semantic conversion and signature changes**

```bash
git add src/Semantics.h src/Semantics.cpp
git commit -m "feat: add scalar semantics and signature aggregation"
```

## Task 4: Make Integer and Pointer Code Generation Width-Aware

**Files:**
- Modify: `src/CodeGenerator.h`
- Modify: `src/CodeGenerator.cpp`

- [ ] **Step 1: Add codegen helper APIs for load and store width decisions**

```cpp
int scalarSize(const TypePtr &type) const;
bool needsSignExtension(const TypePtr &type) const;
std::string loadMnemonic(const TypePtr &type) const;
std::string storeMnemonic(const TypePtr &type) const;
```

- [ ] **Step 2: Replace hard-coded `int` loads with width-aware loads**

```cpp
if (type->isInteger() && type->valueSize() == 1) {
    emit("movzx eax, byte ptr [rax]");
} else if (type->isInteger() && type->valueSize() == 2) {
    emit("movzx eax, word ptr [rax]");
} else if (type->isInteger() && type->valueSize() == 4) {
    emit("mov eax, dword ptr [rax]");
} else if (type->isInteger() && type->valueSize() == 8) {
    emit("mov rax, qword ptr [rax]");
}
```

- [ ] **Step 3: Distinguish signed and unsigned extension when widening narrow values**

```cpp
if (type->isSignedInteger() && type->valueSize() == 1) {
    emit("movsx eax, byte ptr [rax]");
}
if (type->isUnsignedInteger() && type->valueSize() == 1) {
    emit("movzx eax, byte ptr [rax]");
}
```

- [ ] **Step 4: Make pointer arithmetic scale by pointee `valueSize()` instead of assuming four-byte elements**

```cpp
const int stride = pointerType->elementType->valueSize();
emit("imul rcx, " + std::to_string(stride));
emit("add rax, rcx");
```

- [ ] **Step 5: Keep floating-point hooks explicit even if full lowering lands later**

```cpp
if (type->isFloating()) {
    throw std::runtime_error("floating-point code generation not implemented yet");
}
```

- [ ] **Step 6: Rebuild and run an existing integer sample**

Run: `cmake --build --preset debug`

Run: `.\build\Debug\minic.exe .\input\answer.c -o .\output\answer.exe`

Run: `.\output\answer.exe`

Run: `$LASTEXITCODE`

Expected: the compiler succeeds and the program still returns `42`.

- [ ] **Step 7: Commit width-aware integer and pointer code generation**

```bash
git add src/CodeGenerator.h src/CodeGenerator.cpp
git commit -m "feat: make integer code generation width aware"
```

## Task 5: Introduce Artifact Planning and Multi-Input Driver Flow

**Files:**
- Create: `src/Artifacts.h`
- Modify: `src/Driver.h`
- Modify: `src/Driver.cpp`

- [ ] **Step 1: Create artifact data objects used by the driver and toolchain**

```cpp
struct TranslationUnitInput {
    std::filesystem::path sourcePath;
    std::string sourceText;
};

struct TranslationUnitResult {
    std::filesystem::path sourcePath;
    std::filesystem::path asmPath;
    std::filesystem::path objPath;
    std::unordered_map<std::string, FunctionSignature> functions;
};

struct LinkInput {
    std::filesystem::path objectPath;
    std::filesystem::path sourcePath;
};

struct LinkPlan {
    std::vector<LinkInput> inputs;
    std::filesystem::path outputPath;
};
```

- [ ] **Step 2: Expand CLI parsing to collect many input files before `-o` and flags**

```cpp
std::vector<std::filesystem::path> inputPaths;

for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "-o") {
        options.outputPath = args[++i];
        continue;
    }
    if (args[i] == "--keep-obj") {
        options.keepObject = true;
        continue;
    }
    inputPaths.push_back(args[i]);
}
```

- [ ] **Step 3: Add deterministic artifact naming that avoids stem collisions**

```cpp
std::string artifactStem = sourcePath.stem().string();
if (!usedStems.insert(artifactStem).second) {
    artifactStem += "_" + std::to_string(index);
}
result.asmPath = outputDir / (artifactStem + ".asm");
result.objPath = outputDir / (artifactStem + ".obj");
```

- [ ] **Step 4: Compile each translation unit independently through lexer, parser, semantics, and code generation**

```cpp
for (const auto &input : inputs) {
    Lexer lexer(input.sourceText);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();
    TranslationUnitResult unit;
    semanticAnalyzer.analyzeTranslationUnit(program, unit);
    writeFile(unit.asmPath, generator.generate(program));
    units.push_back(std::move(unit));
}
```

- [ ] **Step 5: Merge function signatures across all units and validate `main` once per build**

```cpp
std::unordered_map<std::string, FunctionSignature> functions;
semanticAnalyzer.mergeFunctionSignatures(units, functions);
validateMain(functions);
```

- [ ] **Step 6: Rebuild after driver and artifact changes**

Run: `cmake --build --preset debug`

Expected: the compiler builds and still supports a single-file invocation.

- [ ] **Step 7: Commit the multi-input driver and artifact plan layer**

```bash
git add src/Artifacts.h src/Driver.h src/Driver.cpp
git commit -m "feat: add translation unit artifacts and multi-input driver flow"
```

## Task 6: Split Toolchain Assembly and Linking APIs

**Files:**
- Modify: `src/Toolchain.h`
- Modify: `src/Toolchain.cpp`

- [ ] **Step 1: Replace the one-shot assemble-and-link API with separate assembly and link calls**

```cpp
static void assemble(
    const ToolchainPaths &paths,
    const std::filesystem::path &asmPath,
    const std::filesystem::path &objPath);

static void linkObjects(
    const ToolchainPaths &paths,
    const std::vector<std::filesystem::path> &objectPaths,
    const std::filesystem::path &exePath);
```

- [ ] **Step 2: Keep the `ml64.exe` invocation focused on one assembly file**

```cpp
runCommand(
    quote(paths.ml64) + " /nologo /c /Fo" + quote(objPath) + " " + quote(asmPath),
    "assembling generated x64 assembly");
```

- [ ] **Step 3: Build one `link.exe` command that consumes all object files**

```cpp
std::string command =
    quote(paths.link) +
    " /nologo /subsystem:console /entry:mainCRTStartup /machine:x64 /out:" + quote(exePath);

for (const auto &objectPath : objectPaths) {
    command += " " + quote(objectPath);
}

command += " /libpath:" + quote(paths.sdkUmLib) + " kernel32.lib";
runCommand(command, "linking executable");
```

- [ ] **Step 4: Update the driver to assemble all translation units, then link once**

```cpp
std::vector<std::filesystem::path> objectPaths;
for (const auto &unit : units) {
    Toolchain::assemble(toolchain, unit.asmPath, unit.objPath);
    objectPaths.push_back(unit.objPath);
}
Toolchain::linkObjects(toolchain, objectPaths, options.outputPath);
```

- [ ] **Step 5: Rebuild and verify the single-file path still works**

Run: `cmake --build --preset debug`

Run: `.\build\Debug\minic.exe .\input\answer.c -o .\output\answer.exe`

Run: `.\output\answer.exe`

Run: `$LASTEXITCODE`

Expected: successful build and a `42` exit code.

- [ ] **Step 6: Commit the refactored toolchain API**

```bash
git add src/Toolchain.h src/Toolchain.cpp src/Driver.cpp
git commit -m "refactor: split assembly and linking steps"
```

## Task 7: Add Validation Samples for Types and Multi-File Builds

**Files:**
- Create: `input/types_signed_unsigned.c`
- Create: `input/types_long_long.c`
- Create: `input/types_pointer_stride.c`
- Create: `input/multifile_math_a.c`
- Create: `input/multifile_math_b.c`
- Create: `input/multifile_conflict_a.c`
- Create: `input/multifile_conflict_b.c`

- [ ] **Step 1: Add a signed and unsigned width sample**

```c
int main() {
    signed char a;
    unsigned char b;
    a = -1;
    b = 255;
    return a + b - 254;
}
```

- [ ] **Step 2: Add a `long long` sample**

```c
long long twice(long long x) {
    return x + x;
}

int main() {
    return (int)(twice(21));
}
```

- [ ] **Step 3: Add a pointer-stride sample that fails if scaling assumes four-byte elements**

```c
int main() {
    char data[4];
    char *p;
    p = data;
    p = p + 3;
    *p = 42;
    return data[3];
}
```

- [ ] **Step 4: Add a multi-file positive sample**

```c
// input/multifile_math_a.c
int add_twice(int x) {
    return x + x;
}
```

```c
// input/multifile_math_b.c
int add_twice(int x);

int main() {
    return add_twice(21);
}
```

- [ ] **Step 5: Add a multi-file conflicting declaration sample**

```c
// input/multifile_conflict_a.c
int f(unsigned int x);
```

```c
// input/multifile_conflict_b.c
int f(int x) {
    return x;
}
```

- [ ] **Step 6: Rebuild before running the new sample matrix**

Run: `cmake --build --preset debug`

Expected: compiler builds successfully.

- [ ] **Step 7: Commit the validation corpus**

```bash
git add input/types_signed_unsigned.c input/types_long_long.c input/types_pointer_stride.c input/multifile_math_a.c input/multifile_math_b.c input/multifile_conflict_a.c input/multifile_conflict_b.c
git commit -m "test: add type and multi-file validation samples"
```

## Task 8: Validate End-to-End Behavior and Update Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Run the baseline single-file validation**

Run: `cmake --build --preset debug`

Run: `.\build\Debug\minic.exe .\input\answer.c -o .\output\answer.exe`

Run: `.\output\answer.exe`

Run: `$LASTEXITCODE`

Expected: `42`

- [ ] **Step 2: Run the signed and unsigned sample**

Run: `.\build\Debug\minic.exe .\input\types_signed_unsigned.c -o .\output\types_signed_unsigned.exe`

Run: `.\output\types_signed_unsigned.exe`

Run: `$LASTEXITCODE`

Expected: `0`

- [ ] **Step 3: Run the `long long` sample**

Run: `.\build\Debug\minic.exe .\input\types_long_long.c -o .\output\types_long_long.exe`

Run: `.\output\types_long_long.exe`

Run: `$LASTEXITCODE`

Expected: `42`

- [ ] **Step 4: Run the pointer-stride sample**

Run: `.\build\Debug\minic.exe .\input\types_pointer_stride.c -o .\output\types_pointer_stride.exe`

Run: `.\output\types_pointer_stride.exe`

Run: `$LASTEXITCODE`

Expected: `42`

- [ ] **Step 5: Run the positive multi-file sample**

Run: `.\build\Debug\minic.exe .\input\multifile_math_a.c .\input\multifile_math_b.c -o .\output\multifile_math.exe`

Run: `.\output\multifile_math.exe`

Run: `$LASTEXITCODE`

Expected: `42`

- [ ] **Step 6: Run the negative multi-file conflict sample**

Run: `.\build\Debug\minic.exe .\input\multifile_conflict_a.c .\input\multifile_conflict_b.c -o .\output\multifile_conflict.exe`

Expected: compiler error describing a conflicting declaration or parameter type mismatch for `f`.

- [ ] **Step 7: Update `README.md` to document the new supported subset and multi-file usage**

```md
- `_Bool`, `char`, `short`, `int`, `long long`
- signed and unsigned integer variants
- pointers and local arrays of supported element types
- multiple input `.c` files in one invocation
```

- [ ] **Step 8: Re-run the main validation command set after docs touch**

Run: `cmake --build --preset debug`

Run: `.\build\Debug\minic.exe .\input\answer.c -o .\output\answer.exe`

Run: `.\build\Debug\minic.exe .\input\multifile_math_a.c .\input\multifile_math_b.c -o .\output\multifile_math.exe`

Expected: both compiles succeed.

- [ ] **Step 9: Commit docs and final validation adjustments**

```bash
git add README.md
git commit -m "docs: describe expanded type and multi-file support"
```

## Task 9: Optional Follow-Up for Floating-Point Lowering

**Files:**
- Modify: `src/Lexer.cpp`
- Modify: `src/Parser.cpp`
- Modify: `src/Semantics.cpp`
- Modify: `src/CodeGenerator.cpp`
- Create: `input/types_double.c`

- [ ] **Step 1: Decide whether this delivery includes executable floating-point support**

Run: inspect the current progress after Task 8 and confirm whether integer-family completeness is stable enough to take on XMM lowering safely.

Expected: either proceed with floating-point lowering or explicitly defer it to a separate branch of work.

- [ ] **Step 2: If proceeding, write a failing floating-point sample**

```c
double add(double a, double b) {
    return a + b;
}

int main() {
    return (int)add(19.5, 22.5);
}
```

- [ ] **Step 3: If proceeding, implement floating literals, semantic conversions, and XMM lowering together**

```cpp
if (type->isFloating()) {
    // route through xmm registers and floating arithmetic instructions
}
```

- [ ] **Step 4: If proceeding, validate the sample**

Run: `.\build\Debug\minic.exe .\input\types_double.c -o .\output\types_double.exe`

Run: `.\output\types_double.exe`

Run: `$LASTEXITCODE`

Expected: `42`

- [ ] **Step 5: If deferred, document the deferral clearly instead of claiming full floating support**

```md
Current built-in type parsing includes `float` and `double`, but executable floating-point code generation remains a follow-up task.
```

## Self-Review

Spec coverage check:

- expanded type representation and parsing is covered by Tasks 1 and 2,
- semantic conversion rules and whole-build function consistency are covered by Task 3,
- width-aware integer and pointer code generation is covered by Task 4,
- multi-file translation-unit orchestration and artifact planning are covered by Task 5,
- separate assembly and multi-object linking are covered by Task 6,
- validation samples and documentation are covered by Tasks 7 and 8,
- staged floating-point delivery is covered by Task 9.

Placeholder scan:

- no `TBD`, `TODO`, or "similar to previous task" placeholders remain,
- each code-changing task names exact files and concrete commands,
- validation commands include expected outcomes.

Type consistency check:

- the plan consistently uses `TypeKind`, `ScalarKind`, `TranslationUnitResult`, `LinkInput`, and `LinkPlan`,
- whole-build signature aggregation is introduced in Task 3 and consumed in Task 5,
- the toolchain split introduced in Task 6 matches the driver flow from Task 5.
