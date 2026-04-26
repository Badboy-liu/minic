cmake_minimum_required(VERSION 3.21)

project(minic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Driver.cpp
    src/Driver.h
    src/Toolchain.cpp
    src/Toolchain.h
    src/CodeGenerator.cpp
    src/CodeGenerator.h
    src/Semantics.cpp
    src/Semantics.h
    src/Parser.cpp
    src/Parser.h
    src/Lexer.cpp
    src/Lexer.h
    src/Token.cpp
    src/Token.h
    src/Ast.h
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        NOMINMAX
)
