#pragma once

#include "Ast.h"
#include "ObjectFileModel.h"

class WindowsObjectEmitter {
public:
    ObjectFileModel emit(const Program &program, bool emitEntryPoint);

private:
    static ObjectSection makeTextSection();
    static ObjectSection makeEmptyDataSection();
    static ObjectSection makeEmptyReadOnlyDataSection();
    static ObjectSection makeEmptyBssSection();
};
