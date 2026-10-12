#include "utf8.hh"

namespace {

bool isCont(unsigned char b) {
    return (b & 0xC0) == 0x80;
}

// 0 means the lead byte is not a valid UTF-8 start.
size_t seqLen(unsigned char lead) {
    if ((lead & 0x80) == 0x00) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

// ASCII letter, digit, underscore, or any byte >= 0x80 (widgets.cc IsWordChar).
bool isWordChar(unsigned char b) {
    return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
           b == '_' || b >= 0x80;
}

}  // namespace

size_t utf8Next(std::string_view s, size_t pos) {
    if (pos >= s.size()) return s.size();
    size_t len = seqLen(static_cast<unsigned char>(s[pos]));
    if (len == 0 || pos + len > s.size()) return pos + 1;
    for (size_t i = 1; i < len; ++i) {
        if (!isCont(static_cast<unsigned char>(s[pos + i]))) return pos + 1;
    }
    return pos + len;
}

size_t utf8Prev(std::string_view s, size_t pos) {
    if (pos == 0) return 0;
    if (pos > s.size()) pos = s.size();
    size_t i = pos - 1;
    size_t back = 0;
    while (i > 0 && isCont(static_cast<unsigned char>(s[i])) && back < 3) {
        --i;
        ++back;
    }
    if (utf8Next(s, i) == pos) return i;
    return pos - 1;
}

size_t nextWord(std::string_view s, size_t pos) {
    if (pos > s.size()) pos = s.size();
    while (pos < s.size() && !isWordChar(static_cast<unsigned char>(s[pos]))) ++pos;
    while (pos < s.size() && isWordChar(static_cast<unsigned char>(s[pos]))) ++pos;
    return pos;
}

size_t prevWord(std::string_view s, size_t pos) {
    if (pos > s.size()) pos = s.size();
    while (pos > 0 && !isWordChar(static_cast<unsigned char>(s[pos - 1]))) --pos;
    while (pos > 0 && isWordChar(static_cast<unsigned char>(s[pos - 1]))) --pos;
    return pos;
}

std::pair<size_t, size_t> wordAt(std::string_view s, size_t pos) {
    if (pos >= s.size() || !isWordChar(static_cast<unsigned char>(s[pos]))) return {pos, pos};
    size_t begin = pos;
    size_t end = pos;
    while (begin > 0 && isWordChar(static_cast<unsigned char>(s[begin - 1]))) --begin;
    while (end < s.size() && isWordChar(static_cast<unsigned char>(s[end]))) ++end;
    return {begin, end};
}

size_t codepointCount(std::string_view s) {
    size_t n = 0;
    size_t i = 0;
    while (i < s.size()) {
        i = utf8Next(s, i);
        ++n;
    }
    return n;
}
