#include "parse.hh"

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

size_t lineEnd(std::string_view src, size_t pos) {
    while (pos < src.size() && src[pos] != '\n') ++pos;
    if (pos < src.size()) ++pos;
    return pos;
}

size_t lineEol(std::string_view src, size_t begin, size_t end) {
    if (end > begin && src[end - 1] == '\n') return end - 1;
    return end;
}

size_t skipSpaces(std::string_view src, size_t i, size_t eol) {
    while (i < eol && isSpace(src[i])) ++i;
    return i;
}

int fenceOpenLen(std::string_view src, size_t begin, size_t eol, char* marker) {
    if (begin >= eol) return 0;
    char ch = src[begin];
    if (ch != '`' && ch != '~') return 0;
    size_t i = begin;
    while (i < eol && src[i] == ch) ++i;
    int n = static_cast<int>(i - begin);
    if (n < 3) return 0;
    *marker = ch;
    return n;
}

bool fenceClose(std::string_view src, size_t begin, size_t eol, char marker, int minLen) {
    size_t i = begin;
    while (i < eol && src[i] == marker) ++i;
    if (static_cast<int>(i - begin) < minLen) return false;
    return skipSpaces(src, i, eol) == eol;
}

bool isRuleLine(std::string_view src, size_t begin, size_t eol) {
    char mark = 0;
    int count = 0;
    for (size_t i = begin; i < eol; ++i) {
        char c = src[i];
        if (isSpace(c)) continue;
        if (c != '-' && c != '*' && c != '_') return false;
        if (mark == 0) mark = c;
        else if (c != mark) return false;
        ++count;
    }
    return count >= 3;
}

bool isHeadingLine(std::string_view src, size_t begin, size_t eol, int* level, size_t* contentBegin) {
    size_t i = begin;
    int n = 0;
    while (i < eol && src[i] == '#') {
        ++n;
        ++i;
    }
    if (n < 1 || n > 6) return false;
    if (i >= eol || !isSpace(src[i])) return false;
    *level = n;
    *contentBegin = i + 1;
    return true;
}

bool isListItemLine(std::string_view src, size_t begin, size_t eol, bool* ordered, size_t* contentBegin) {
    size_t i = skipSpaces(src, begin, eol);
    if (i >= eol) return false;
    char c = src[i];
    if (c == '-' || c == '+' || c == '*') {
        ++i;
        if (i >= eol || !isSpace(src[i])) return false;
        *ordered = false;
        *contentBegin = i + 1;
        return true;
    }
    if (c < '0' || c > '9') return false;
    int digits = 0;
    while (i < eol && src[i] >= '0' && src[i] <= '9') {
        ++digits;
        ++i;
        if (digits > 9) return false;
    }
    if (digits < 1) return false;
    if (i >= eol || (src[i] != '.' && src[i] != ')')) return false;
    ++i;
    if (i >= eol || !isSpace(src[i])) return false;
    *ordered = true;
    *contentBegin = i + 1;
    return true;
}

bool isQuoteLine(std::string_view src, size_t begin, size_t eol, size_t* contentBegin) {
    size_t i = skipSpaces(src, begin, eol);
    if (i >= eol || src[i] != '>') return false;
    ++i;
    if (i < eol && isSpace(src[i])) ++i;
    *contentBegin = i;
    return true;
}

bool isBlankLine(std::string_view src, size_t begin, size_t eol) {
    return skipSpaces(src, begin, eol) == eol;
}

enum class LineKind { Fence, Rule, Heading, ListItem, Quote, Blank, Other };

LineKind classify(std::string_view src, size_t begin, size_t eol, char* fenceMarker, int* fenceLen,
                  int* level, bool* ordered, size_t* contentBegin) {
    if (int n = fenceOpenLen(src, begin, eol, fenceMarker)) {
        *fenceLen = n;
        return LineKind::Fence;
    }
    if (isRuleLine(src, begin, eol)) return LineKind::Rule;
    if (isHeadingLine(src, begin, eol, level, contentBegin)) return LineKind::Heading;
    if (isListItemLine(src, begin, eol, ordered, contentBegin)) return LineKind::ListItem;
    if (isQuoteLine(src, begin, eol, contentBegin)) return LineKind::Quote;
    if (isBlankLine(src, begin, eol)) return LineKind::Blank;
    return LineKind::Other;
}

Block makeBlock(BlockKind kind, size_t begin, size_t end, size_t contentBegin, int level = 0,
                bool ordered = false) {
    Block b;
    b.kind = kind;
    b.begin = begin;
    b.end = end;
    b.contentBegin = contentBegin;
    b.level = level;
    b.ordered = ordered;
    return b;
}

}  // namespace

std::vector<Block> parseBlocks(std::string_view src) {
    std::vector<Block> out;
    size_t pos = 0;
    while (pos < src.size()) {
        size_t begin = pos;
        size_t end = lineEnd(src, begin);
        size_t eol = lineEol(src, begin, end);

        char fenceMarker = 0;
        int fenceLen = 0;
        int level = 0;
        bool ordered = false;
        size_t contentBegin = begin;
        LineKind k = classify(src, begin, eol, &fenceMarker, &fenceLen, &level, &ordered, &contentBegin);

        if (k == LineKind::Fence) {
            size_t content = end;
            size_t scan = end;
            size_t fenceEnd = end;
            while (scan < src.size()) {
                size_t sEnd = lineEnd(src, scan);
                size_t sEol = lineEol(src, scan, sEnd);
                if (fenceClose(src, scan, sEol, fenceMarker, fenceLen)) {
                    fenceEnd = sEnd;
                    break;
                }
                scan = sEnd;
                fenceEnd = sEnd;
            }
            out.push_back(makeBlock(BlockKind::Fence, begin, fenceEnd, content));
            pos = fenceEnd;
            continue;
        }

        if (k == LineKind::Rule) {
            out.push_back(makeBlock(BlockKind::Rule, begin, end, begin));
            pos = end;
            continue;
        }

        if (k == LineKind::Heading) {
            out.push_back(makeBlock(BlockKind::Heading, begin, end, contentBegin, level));
            pos = end;
            continue;
        }

        if (k == LineKind::ListItem) {
            out.push_back(makeBlock(BlockKind::ListItem, begin, end, contentBegin, 0, ordered));
            pos = end;
            continue;
        }

        if (k == LineKind::Quote) {
            out.push_back(makeBlock(BlockKind::Quote, begin, end, contentBegin));
            pos = end;
            continue;
        }

        if (k == LineKind::Blank) {
            out.push_back(makeBlock(BlockKind::Blank, begin, end, begin));
            pos = end;
            continue;
        }

        size_t pEnd = end;
        size_t scan = end;
        while (scan < src.size()) {
            size_t sEnd = lineEnd(src, scan);
            size_t sEol = lineEol(src, scan, sEnd);
            char fm = 0;
            int fl = 0;
            int lv = 0;
            bool ord = false;
            size_t cb = scan;
            LineKind nk = classify(src, scan, sEol, &fm, &fl, &lv, &ord, &cb);
            if (nk != LineKind::Other) break;
            pEnd = sEnd;
            scan = sEnd;
        }
        out.push_back(makeBlock(BlockKind::Paragraph, begin, pEnd, begin));
        pos = pEnd;
    }
    return out;
}
