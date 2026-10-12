#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

enum class BlockKind : uint8_t {
    Blank, Paragraph, Heading, Rule, ListItem, Quote, Fence
};

struct Block {
    BlockKind kind;
    size_t begin, end, contentBegin;
    int level;       // 1..6 for Heading, else 0
    bool ordered;    // ListItem
};

std::vector<Block> parseBlocks(std::string_view src);
