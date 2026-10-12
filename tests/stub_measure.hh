#pragma once

#include "measure.hh"
#include "utf8.hh"

struct StubMeasurer : Measurer {
    float width(std::string_view text, float sizePx, Face) const override {
        return static_cast<float>(codepointCount(text)) * sizePx;
    }
    float lineHeight(float sizePx) const override {
        return sizePx;
    }
};
