#pragma once

#include <cstdint>
#include <string_view>

enum class Face : uint8_t { Roman, Bold, Italic };

struct Measurer {
    virtual float width(std::string_view text, float sizePx, Face face) const = 0;
    virtual float lineHeight(float sizePx) const = 0;
    virtual ~Measurer() = default;
};
