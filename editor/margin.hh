#pragma once

#include <algorithm>

// 38px at scale 1 is about one centimeter at 96 dpi. The inset shrinks when
// two of them would leave less than 160 scaled pixels of content.
inline float pageMargin(float scale, float windowW, float windowH) {
    auto inset = [scale](float span) {
        float margin = 38.f * scale;
        const float minInner = 160.f * scale;
        if (span - 2.f * margin < minInner) margin = std::max(0.f, (span - minInner) * 0.5f);
        return margin;
    };
    return std::min(inset(windowW), inset(windowH));
}
