#include "layout.hh"

#include "utf8.hh"

#include <utility>

namespace {

Face faceFor(SpanRole role) {
    if (role == SpanRole::Em) return Face::Italic;
    if (role == SpanRole::Strong || role == SpanRole::StrongEm) return Face::Bold;
    return Face::Roman;
}

float headingSize(const LayoutMetrics& m, int level) {
    if (level <= 1) return m.h1;
    if (level == 2) return m.h2;
    return m.h3;
}

size_t lineEnd(std::string_view src, size_t pos, size_t limit) {
    while (pos < limit && src[pos] != '\n') ++pos;
    if (pos < limit) ++pos;
    return pos;
}

size_t lineEol(std::string_view src, size_t begin, size_t end) {
    if (end > begin && src[end - 1] == '\n') return end - 1;
    return end;
}

bool isFenceCloseLine(std::string_view src, size_t begin, size_t eol, char marker, int minLen) {
    if (minLen < 3 || begin >= eol || src[begin] != marker) return false;
    size_t i = begin;
    while (i < eol && src[i] == marker) ++i;
    if (static_cast<int>(i - begin) < minLen) return false;
    while (i < eol && (src[i] == ' ' || src[i] == '\t')) ++i;
    return i == eol;
}

size_t mapSpanToSource(const Span& s, size_t displayOff) {
    if (displayOff <= s.displayBegin) return s.sourceBegin;
    if (displayOff >= s.displayEnd) return s.sourceEnd;
    if (s.displayEnd - s.displayBegin == s.sourceEnd - s.sourceBegin) {
        return s.sourceBegin + (displayOff - s.displayBegin);
    }
    return s.sourceBegin;
}

size_t mapDisplayToSource(const std::string& display, const std::vector<Span>& spans, size_t d,
                          size_t fallback) {
    if (spans.empty()) return fallback;
    if (d >= display.size()) return spans.back().sourceEnd;
    for (const Span& s : spans) {
        if (d < s.displayBegin) return s.sourceBegin;
        if (d < s.displayEnd) return mapSpanToSource(s, d);
    }
    return spans.back().sourceEnd;
}

Face faceAt(const std::vector<Span>& spans, size_t displayOff) {
    for (const Span& s : spans) {
        if (s.displayBegin <= displayOff && displayOff < s.displayEnd) return faceFor(s.role);
    }
    return Face::Roman;
}

std::vector<Span> sliceSpans(const std::vector<Span>& spans, size_t d0, size_t d1) {
    std::vector<Span> out;
    for (const Span& s : spans) {
        size_t b = s.displayBegin < d0 ? d0 : s.displayBegin;
        size_t e = s.displayEnd < d1 ? s.displayEnd : d1;
        if (b >= e) continue;
        Span n;
        n.role = s.role;
        n.displayBegin = b - d0;
        n.displayEnd = e - d0;
        if (s.displayEnd - s.displayBegin == s.sourceEnd - s.sourceBegin) {
            n.sourceBegin = s.sourceBegin + (b - s.displayBegin);
            n.sourceEnd = s.sourceBegin + (e - s.displayBegin);
        } else {
            n.sourceBegin = s.sourceBegin;
            n.sourceEnd = s.sourceEnd;
        }
        out.push_back(n);
    }
    return out;
}

size_t rowContentBegin(size_t sourceBegin, size_t sourceEnd, size_t blockContentBegin) {
    if (sourceBegin == sourceEnd) {
        return blockContentBegin == sourceBegin ? blockContentBegin : sourceBegin;
    }
    if (blockContentBegin >= sourceBegin && blockContentBegin < sourceEnd) return blockContentBegin;
    return sourceBegin;
}

struct Layouter {
    std::string_view src;
    const LayoutMetrics& m;
    const Measurer& measure;
    Page page;
    float y = 0;

    void emit(Row row) {
        row.y = y;
        row.h = measure.lineHeight(row.sizePx);
        row.textX = m.gutter;
        row.gutter.clear();
        y += row.h;
        page.rows.push_back(std::move(row));
    }

    void emitEmpty(size_t sourceBegin, size_t sourceEnd, size_t contentBegin, float sizePx,
                   bool sourceMode, BlockKind kind, int level, bool ordered) {
        Row r{};
        r.sizePx = sizePx;
        r.sourceBegin = sourceBegin;
        r.sourceEnd = sourceEnd;
        r.contentBegin = contentBegin;
        r.sourceMode = sourceMode;
        r.kind = kind;
        r.level = level;
        r.ordered = ordered;
        emit(std::move(r));
    }

    void wrapRun(const std::string& display, const std::vector<Span>& spans, size_t firstSourceBegin,
                 size_t lastSourceEnd, size_t blockContentBegin, float sizePx, bool sourceMode,
                 BlockKind kind, int level, bool ordered) {
        if (display.empty()) {
            emitEmpty(firstSourceBegin, lastSourceEnd, rowContentBegin(firstSourceBegin, lastSourceEnd,
                                                                      blockContentBegin),
                      sizePx, sourceMode, kind, level, ordered);
            return;
        }

        size_t d = 0;
        bool first = true;
        while (d < display.size()) {
            size_t lastFit = d;
            size_t lastSpace = static_cast<size_t>(-1);
            float w = 0;
            size_t i = d;
            while (i < display.size()) {
                size_t n = utf8Next(display, i);
                Face face = faceAt(spans, i);
                float gw = measure.width(std::string_view(display).substr(i, n - i), sizePx, face);
                if (w + gw > m.columnW && i > d) break;
                w += gw;
                lastFit = n;
                if (n - i == 1 && display[i] == ' ') lastSpace = i;
                i = n;
            }

            size_t breakAt = lastFit;
            size_t nextStart = lastFit;
            if (i < display.size()) {
                if (lastSpace != static_cast<size_t>(-1) && lastSpace > d) {
                    breakAt = lastSpace;
                    nextStart = lastSpace + 1;
                } else if (nextStart < display.size() && display[nextStart] == ' ') {
                    nextStart += 1;
                }
            }

            if (breakAt < d) breakAt = d;
            if (nextStart <= d) nextStart = lastFit > d ? lastFit : utf8Next(display, d);

            Row r{};
            r.sizePx = sizePx;
            r.display = display.substr(d, breakAt - d);
            r.spans = sliceSpans(spans, d, breakAt);
            r.sourceMode = sourceMode;
            r.kind = kind;
            r.level = level;
            r.ordered = ordered;
            r.sourceBegin = first ? firstSourceBegin : mapDisplayToSource(display, spans, d, firstSourceBegin);
            bool last = nextStart >= display.size();
            r.sourceEnd = last ? lastSourceEnd : mapDisplayToSource(display, spans, nextStart, lastSourceEnd);
            r.contentBegin = rowContentBegin(r.sourceBegin, r.sourceEnd, blockContentBegin);
            emit(std::move(r));
            first = false;
            d = nextStart;
        }
    }

    void layoutSource(const Block& b) {
        size_t pos = b.begin;
        bool firstLine = true;
        if (pos >= b.end) {
            emitEmpty(b.begin, b.end, b.contentBegin, m.body, true, b.kind, b.level, b.ordered);
            return;
        }
        while (pos < b.end) {
            size_t end = lineEnd(src, pos, b.end);
            size_t eol = lineEol(src, pos, end);
            std::string display(src.substr(pos, eol - pos));
            std::vector<Span> spans;
            if (!display.empty()) {
                Span s;
                s.sourceBegin = pos;
                s.sourceEnd = eol;
                s.displayBegin = 0;
                s.displayEnd = display.size();
                s.role = SpanRole::Plain;
                spans.push_back(s);
            }
            size_t firstSrc = firstLine ? b.begin : pos;
            wrapRun(display, spans, firstSrc, end, b.contentBegin, m.body, true, b.kind, b.level,
                    b.ordered);
            firstLine = false;
            pos = end;
        }
    }

    void layoutFenceRendered(const Block& b) {
        char marker = 0;
        int minLen = 0;
        if (b.begin < src.size()) {
            char ch = src[b.begin];
            if (ch == '`' || ch == '~') {
                marker = ch;
                size_t i = b.begin;
                while (i < b.end && i < src.size() && src[i] == ch) {
                    ++minLen;
                    ++i;
                }
            }
        }

        size_t interiorBegin = b.contentBegin;
        size_t interiorEnd = b.end;
        if (interiorBegin < b.end) {
            size_t lastLine = interiorBegin;
            size_t scan = interiorBegin;
            while (scan < b.end) {
                lastLine = scan;
                scan = lineEnd(src, scan, b.end);
            }
            size_t lastEnd = lineEnd(src, lastLine, b.end);
            size_t eol = lineEol(src, lastLine, lastEnd);
            if (lastLine != b.begin && isFenceCloseLine(src, lastLine, eol, marker, minLen)) {
                interiorEnd = lastLine;
            }
        }

        if (interiorBegin >= interiorEnd) {
            emitEmpty(b.begin, b.contentBegin, b.begin, m.body, false, b.kind, b.level, b.ordered);
            return;
        }

        size_t pos = interiorBegin;
        while (pos < interiorEnd) {
            size_t end = lineEnd(src, pos, interiorEnd);
            size_t eol = lineEol(src, pos, end);
            std::string display(src.substr(pos, eol - pos));
            std::vector<Span> spans;
            if (!display.empty()) {
                Span s;
                s.sourceBegin = pos;
                s.sourceEnd = eol;
                s.displayBegin = 0;
                s.displayEnd = display.size();
                s.role = SpanRole::Plain;
                spans.push_back(s);
            }
            wrapRun(display, spans, pos, end, pos, m.body, false, b.kind, b.level, b.ordered);
            pos = end;
        }
    }

    void layoutRendered(const Block& b) {
        if (b.kind == BlockKind::Fence) {
            layoutFenceRendered(b);
            return;
        }
        if (b.kind == BlockKind::Rule || b.kind == BlockKind::Blank) {
            emitEmpty(b.begin, b.end, b.contentBegin, m.body, false, b.kind, b.level, b.ordered);
            return;
        }
        float sizePx = m.body;
        if (b.kind == BlockKind::Heading) sizePx = headingSize(m, b.level);
        InlineRun run = scanInlines(src, b.contentBegin, b.end);
        wrapRun(run.display, run.spans, b.begin, b.end, b.contentBegin, sizePx, false, b.kind, b.level,
                b.ordered);
    }
};

size_t rowIndexForCursor(const Page& page, size_t cursorByte) {
    size_t found = 0;
    bool any = false;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const Row& r = page.rows[i];
        if (r.sourceBegin == r.sourceEnd) {
            if (cursorByte == r.sourceBegin) {
                found = i;
                any = true;
            }
            continue;
        }
        if (cursorByte >= r.sourceBegin && cursorByte < r.sourceEnd) {
            found = i;
            any = true;
        }
    }
    if (!any && !page.rows.empty()) return page.rows.size() - 1;
    return found;
}

const Row* rowAtY(const Page& page, float y) {
    if (page.rows.empty()) return nullptr;
    const Row* found = &page.rows.back();
    for (const Row& r : page.rows) {
        if (y < r.y + r.h) {
            found = &r;
            break;
        }
    }
    return found;
}

size_t gutterHit(const Row& row) {
    if (row.contentBegin != row.sourceBegin) return row.contentBegin;
    return row.sourceBegin;
}

}  // namespace

Page layoutPage(std::string_view src, const std::vector<Block>& blocks, size_t cursorByte,
                const LayoutMetrics& m, const Measurer& measure) {
    Layouter L{src, m, measure, {}, 0};

    size_t active = blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Block& b = blocks[i];
        if (cursorByte >= b.begin && cursorByte < b.end) {
            active = i;
            continue;
        }
        // EOF with no trailing newline is still inside the last block.
        if (cursorByte == src.size() && b.end == src.size() && cursorByte >= b.begin &&
            !src.empty() && src.back() != '\n') {
            active = i;
        }
    }
    L.page.active = blocks.empty() ? 0 : active;

    if (blocks.empty()) {
        L.emitEmpty(0, 0, 0, m.body, true, BlockKind::Blank, 0, false);
        L.page.height = L.y;
        return L.page;
    }

    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i > 0) {
            bool listTight =
                blocks[i].kind == BlockKind::ListItem && blocks[i - 1].kind == BlockKind::ListItem;
            L.y += listTight ? m.listGap : m.blockGap;
        }
        if (i == active) L.layoutSource(blocks[i]);
        else L.layoutRendered(blocks[i]);
    }

    if (!src.empty() && src.back() == '\n') {
        L.emitEmpty(src.size(), src.size(), src.size(), m.body, true, BlockKind::Blank, 0, false);
    }

    L.page.height = L.y;
    return L.page;
}

size_t hitTest(const Page& page, float x, float y, const Measurer& measure) {
    const Row* row = rowAtY(page, y);
    if (!row) return 0;
    if (x < row->textX) return gutterHit(*row);
    if (row->display.empty() || row->spans.empty()) return gutterHit(*row);

    float cx = row->textX;
    for (size_t si = 0; si < row->spans.size(); ++si) {
        const Span& s = row->spans[si];
        std::string_view piece = std::string_view(row->display).substr(s.displayBegin, s.displayEnd - s.displayBegin);
        Face face = faceFor(s.role);
        float w = measure.width(piece, row->sizePx, face);
        bool last = si + 1 == row->spans.size();
        if (x < cx + w || last) {
            float px = cx;
            size_t d = s.displayBegin;
            while (d < s.displayEnd) {
                size_t dn = utf8Next(row->display, d);
                std::string_view g = std::string_view(row->display).substr(d, dn - d);
                float gw = measure.width(g, row->sizePx, face);
                if (x < px + gw) return mapSpanToSource(s, d);
                px += gw;
                d = dn;
            }
            return mapSpanToSource(s, s.displayEnd);
        }
        cx += w;
    }
    return row->sourceEnd;
}

float caretX(const Page& page, size_t cursorByte, const Measurer& measure) {
    if (page.rows.empty()) return 0;
    const Row& row = page.rows[rowIndexForCursor(page, cursorByte)];
    float x = row.textX;
    for (const Span& s : row.spans) {
        Face face = faceFor(s.role);
        size_t d = s.displayBegin;
        while (d < s.displayEnd) {
            if (mapSpanToSource(s, d) >= cursorByte) return x;
            size_t dn = utf8Next(row.display, d);
            std::string_view g = std::string_view(row.display).substr(d, dn - d);
            x += measure.width(g, row.sizePx, face);
            d = dn;
        }
    }
    return x;
}

size_t moveVertical(const Page& page, size_t cursorByte, float preferredX, int direction,
                    const Measurer& measure) {
    if (page.rows.empty()) return cursorByte;
    size_t i = rowIndexForCursor(page, cursorByte);
    if (direction > 0 && i + 1 < page.rows.size()) ++i;
    else if (direction < 0 && i > 0) --i;
    const Row& r = page.rows[i];
    return hitTest(page, preferredX, r.y + r.h * 0.5f, measure);
}
