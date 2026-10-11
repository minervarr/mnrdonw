#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class SpanRole : uint8_t { Plain, Strong, Em, StrongEm, Code, Link };

struct Span {
    size_t sourceBegin, sourceEnd;
    size_t displayBegin, displayEnd;
    SpanRole role;
};

struct InlineRun {
    std::string display;
    std::vector<Span> spans;
};

InlineRun scanInlines(std::string_view src, size_t begin, size_t end);
