#include "inline.hh"

namespace {

bool isEscapable(char c) {
    return c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']';
}

struct Scanner {
    std::string_view src;
    std::string display;
    std::vector<Span> spans;

    void emit(size_t srcBegin, size_t srcEnd, std::string_view text, SpanRole role) {
        if (text.empty()) return;
        size_t d0 = display.size();
        display.append(text.data(), text.size());
        Span s;
        s.sourceBegin = srcBegin;
        s.sourceEnd = srcEnd;
        s.displayBegin = d0;
        s.displayEnd = display.size();
        s.role = role;
        spans.push_back(s);
    }
};

void emitRegion(Scanner& sc, size_t from, size_t to, SpanRole role) {
    size_t i = from;
    while (i < to) {
        if (sc.src[i] == '\n') {
            sc.emit(i, i + 1, " ", role);
            ++i;
            continue;
        }
        size_t j = i;
        while (j < to && sc.src[j] != '\n') ++j;
        sc.emit(i, j, sc.src.substr(i, j - i), role);
        i = j;
    }
}

bool closerAt(std::string_view src, size_t i, size_t limit, char c0, char c1, int n) {
    if (n <= 0 || i >= limit || src[i] != c0) return false;
    if (n == 1) return true;
    return i + 1 < limit && src[i + 1] == c1;
}

bool hasCloserAhead(std::string_view src, size_t i, size_t limit, char c0, char c1, int n) {
    if (n <= 0) return true;
    while (i + static_cast<size_t>(n) <= limit) {
        if (closerAt(src, i, limit, c0, c1, n)) return true;
        ++i;
    }
    return false;
}

bool tryCode(Scanner& sc, size_t pos, size_t limit, size_t* next) {
    if (pos >= limit || sc.src[pos] != '`') return false;
    size_t i = pos + 1;
    size_t close = i;
    while (close < limit && sc.src[close] != '`') ++close;
    if (close >= limit || close == i) return false;
    emitRegion(sc, i, close, SpanRole::Code);
    *next = close + 1;
    return true;
}

bool tryLink(Scanner& sc, size_t pos, size_t limit, size_t* next) {
    if (pos >= limit || sc.src[pos] != '[') return false;
    size_t i = pos + 1;
    size_t rb = i;
    while (rb < limit && sc.src[rb] != ']') ++rb;
    if (rb >= limit) return false;
    if (rb + 1 >= limit || sc.src[rb + 1] != '(') return false;
    size_t rp = rb + 2;
    while (rp < limit && sc.src[rp] != ')') ++rp;
    if (rp >= limit) return false;
    emitRegion(sc, i, rb, SpanRole::Link);
    *next = rp + 1;
    return true;
}

bool scanDelimited(Scanner& sc, size_t pos, size_t limit, char c0, char c1, int closerLen,
                   SpanRole role, bool canStrong, bool canEm, size_t* outPos);

bool tryInner(Scanner& sc, size_t pos, size_t limit, char innerC0, char innerC1, int innerLen,
              SpanRole innerRole, bool innerCanStrong, bool innerCanEm,
              char outerC0, char outerC1, int outerLen, size_t* next) {
    size_t snapD = sc.display.size();
    size_t snapS = sc.spans.size();
    size_t n = 0;
    // A longer outer closer (emphasis inside strong) must stay intact. A later
    // pair does not license an inner span that would read those bytes.
    size_t innerLimit = limit;
    if (outerLen > innerLen) {
        size_t at = pos;
        while (at + static_cast<size_t>(outerLen) <= limit) {
            if (closerAt(sc.src, at, limit, outerC0, outerC1, outerLen)) {
                innerLimit = at;
                break;
            }
            ++at;
        }
    }
    if (!scanDelimited(sc, pos, innerLimit, innerC0, innerC1, innerLen, innerRole, innerCanStrong,
                       innerCanEm, &n)) {
        return false;
    }
    if (!hasCloserAhead(sc.src, n, limit, outerC0, outerC1, outerLen)) {
        sc.display.resize(snapD);
        sc.spans.resize(snapS);
        return false;
    }
    *next = n;
    return true;
}

bool scanDelimited(Scanner& sc, size_t pos, size_t limit, char c0, char c1, int closerLen,
                   SpanRole role, bool canStrong, bool canEm, size_t* outPos) {
    size_t snapD = sc.display.size();
    size_t snapS = sc.spans.size();

    size_t i = pos;
    while (i < limit) {
        char c = sc.src[i];

        if (canStrong && i + 1 < limit && (c == '*' || c == '_') && sc.src[i + 1] == c) {
            SpanRole inner = canEm ? SpanRole::Strong : SpanRole::StrongEm;
            size_t n = 0;
            if (tryInner(sc, i + 2, limit, c, c, 2, inner, false, canEm, c0, c1, closerLen, &n)) {
                i = n;
                continue;
            }
        }

        if (closerAt(sc.src, i, limit, c0, c1, closerLen)) {
            if (i == pos) break;
            *outPos = i + static_cast<size_t>(closerLen);
            return true;
        }

        if (c == '\\' && i + 1 < limit && isEscapable(sc.src[i + 1])) {
            sc.emit(i, i + 2, sc.src.substr(i + 1, 1), role);
            i += 2;
            continue;
        }

        if (c == '\n') {
            sc.emit(i, i + 1, " ", role);
            ++i;
            continue;
        }

        size_t n = 0;
        if (c == '`' && tryCode(sc, i, limit, &n)) {
            i = n;
            continue;
        }
        if (c == '[' && tryLink(sc, i, limit, &n)) {
            i = n;
            continue;
        }

        if (canEm && (c == '*' || c == '_')) {
            SpanRole inner = canStrong ? SpanRole::Em : SpanRole::StrongEm;
            if (tryInner(sc, i + 1, limit, c, 0, 1, inner, canStrong, false, c0, c1, closerLen,
                         &n)) {
                i = n;
                continue;
            }
        }

        size_t j = i + 1;
        while (j < limit) {
            char d = sc.src[j];
            if (d == '\\' || d == '\n' || d == '`' || d == '[' || d == '*' || d == '_') break;
            ++j;
        }
        sc.emit(i, j, sc.src.substr(i, j - i), role);
        i = j;
    }

    if (closerLen == 0) {
        *outPos = i;
        return true;
    }

    sc.display.resize(snapD);
    sc.spans.resize(snapS);
    return false;
}

}  // namespace

InlineRun scanInlines(std::string_view src, size_t begin, size_t end) {
    if (end > src.size()) end = src.size();
    if (begin > end) begin = end;
    size_t limit = end;
    if (limit > begin && src[limit - 1] == '\n') --limit;

    Scanner sc;
    sc.src = src;
    size_t outPos = begin;
    scanDelimited(sc, begin, limit, 0, 0, 0, SpanRole::Plain, true, true, &outPos);

    InlineRun run;
    run.display = std::move(sc.display);
    run.spans = std::move(sc.spans);
    return run;
}
