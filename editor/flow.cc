#include "flow.hh"

#include <cstddef>

std::vector<FlowSlot> packFlow(float contentW, float gapX, float rowH, float gapY,
                               const std::vector<float>& widths) {
    std::vector<FlowSlot> out;
    out.reserve(widths.size());
    if (widths.empty()) return out;

    if (contentW <= 0.f) {
        for (size_t i = 0; i < widths.size(); ++i) {
            FlowSlot slot;
            slot.index = static_cast<int>(i);
            slot.y = static_cast<float>(i) * (rowH + gapY);
            slot.h = rowH;
            out.push_back(slot);
        }
        return out;
    }

    float y = 0.f;
    float right = 0.f;
    bool rowEmpty = true;
    for (size_t i = 0; i < widths.size(); ++i) {
        float w = widths[i];
        if (w < 0.f) w = 0.f;
        if (w > contentW) w = contentW;
        if (!rowEmpty && right + gapX + w > contentW) {
            y += rowH + gapY;
            right = 0.f;
            rowEmpty = true;
        }
        FlowSlot slot;
        slot.index = static_cast<int>(i);
        slot.x = rowEmpty ? 0.f : right + gapX;
        slot.y = y;
        slot.w = w;
        slot.h = rowH;
        right = slot.x + w;
        rowEmpty = false;
        out.push_back(slot);
    }
    return out;
}
