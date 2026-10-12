#pragma once

#include "inline.hh"
#include "measure.hh"
#include "parse.hh"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct LayoutMetrics {
    float body, h1, h2, h3;
    float columnW, blockGap, listGap, gutter;
};

struct Row {
    float y, h, textX, sizePx;
    size_t sourceBegin, sourceEnd, contentBegin;
    std::string display;
    std::vector<Span> spans;
    bool sourceMode;
    BlockKind kind;
    int level;
    bool ordered;
    std::string gutter;
};

struct Page {
    std::vector<Row> rows;
    float height;
    size_t active;  // index into the block vector passed to layout
};

Page layoutPage(std::string_view src, const std::vector<Block>& blocks,
                size_t cursorByte, const LayoutMetrics& m, const Measurer& measure);
size_t hitTest(const Page& page, float x, float y, const Measurer& measure);
float caretX(const Page& page, size_t cursorByte, const Measurer& measure);
size_t moveVertical(const Page& page, size_t cursorByte, float preferredX,
                    int direction, const Measurer& measure);
