#pragma once

#include "ObjectFileModel.h"

#include <cstdint>
#include <vector>

class CoffObjectWriter {
public:
    static std::vector<std::uint8_t> writeAmd64(const ObjectFileModel &model);
};
