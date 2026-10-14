#pragma once

#include <vector>

struct FlowSlot {
    int index = 0;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

// Place each width left to right. A width that does not fit the remaining
// space starts the next row. Widths wider than contentW are clamped to it.
std::vector<FlowSlot> packFlow(float contentW, float gapX, float rowH, float gapY,
                               const std::vector<float>& widths);
