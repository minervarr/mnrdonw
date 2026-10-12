#include "document.hh"
#include "utf8.hh"

#include <algorithm>
#include <utility>

namespace {

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

}  // namespace

void Document::invalidateCache() {
    cache_valid_ = false;
}

const std::string& Document::text() const {
    if (!cache_valid_) {
        cached_text_ = buf_.to_string();
        cache_valid_ = true;
    }
    return cached_text_;
}

size_t Document::cursor() const {
    return buf_.cursor_pos();
}

size_t Document::anchor() const {
    return anchor_;
}

bool Document::dirty() const {
    return text() != saved_;
}

void Document::setBytes(const std::string& bytes) {
    buf_.move_cursor_to(0);
    while (buf_.length() > 0) buf_.erase_after();
    buf_.insert(bytes);
    invalidateCache();
}

void Document::insertBytes(size_t pos, const std::string& bytes) {
    buf_.move_cursor_to(pos);
    buf_.insert(bytes);
    invalidateCache();
}

void Document::eraseBytes(size_t pos, size_t n) {
    buf_.move_cursor_to(pos);
    for (size_t i = 0; i < n; ++i) buf_.erase_after();
    invalidateCache();
}

void Document::load(std::string text) {
    setBytes(text);
    buf_.move_cursor_to(0);
    anchor_ = 0;
    saved_ = this->text();
    undo_stack_.clear();
    redo_stack_.clear();
}

void Document::noteSaved() {
    saved_ = text();
}

void Document::setSelection(size_t a, size_t c) {
    size_t n = buf_.length();
    anchor_ = std::min(a, n);
    buf_.move_cursor_to(std::min(c, n));
}

void Document::selectAll() {
    setSelection(0, buf_.length());
}

bool Document::try_coalesce(size_t pos, const std::string& piece) {
    if (undo_stack_.empty()) return false;
    Command& last = undo_stack_.back();
    if (last.type != CmdType::Insert) return false;
    if (last.pos + last.text.size() != pos) return false;
    if (last.text.size() + piece.size() > 64) return false;
    if (last.text.find('\n') != std::string::npos) return false;
    if (piece.find('\n') != std::string::npos) return false;
    last.text += piece;
    last.new_cursor = last.pos + last.text.size();
    last.new_anchor = last.new_cursor;
    return true;
}

void Document::pushCmd(Command cmd) {
    undo_stack_.push_back(std::move(cmd));
    redo_stack_.clear();
}

void Document::applyForward(const Command& cmd) {
    switch (cmd.type) {
    case CmdType::Insert:
        insertBytes(cmd.pos, cmd.text);
        break;
    case CmdType::Erase:
        eraseBytes(cmd.pos, cmd.text.size());
        break;
    case CmdType::Replace:
        eraseBytes(cmd.pos, cmd.old_text.size());
        insertBytes(cmd.pos, cmd.new_text);
        break;
    }
    buf_.move_cursor_to(cmd.new_cursor);
    anchor_ = cmd.new_anchor;
}

void Document::applyReverse(const Command& cmd) {
    switch (cmd.type) {
    case CmdType::Insert:
        eraseBytes(cmd.pos, cmd.text.size());
        break;
    case CmdType::Erase:
        insertBytes(cmd.pos, cmd.text);
        break;
    case CmdType::Replace:
        eraseBytes(cmd.pos, cmd.new_text.size());
        insertBytes(cmd.pos, cmd.old_text);
        break;
    }
    buf_.move_cursor_to(cmd.old_cursor);
    anchor_ = cmd.old_anchor;
}

bool Document::undo() {
    if (undo_stack_.empty()) return false;
    Command cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    applyReverse(cmd);
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool Document::redo() {
    if (redo_stack_.empty()) return false;
    Command cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    applyForward(cmd);
    undo_stack_.push_back(std::move(cmd));
    return true;
}

void Document::deleteRange(size_t lo, size_t hi, size_t old_cursor, size_t old_anchor) {
    std::string erased = text().substr(lo, hi - lo);
    Command cmd;
    cmd.type = CmdType::Erase;
    cmd.pos = lo;
    cmd.text = erased;
    cmd.old_cursor = old_cursor;
    cmd.old_anchor = old_anchor;
    cmd.new_cursor = lo;
    cmd.new_anchor = lo;
    eraseBytes(lo, erased.size());
    buf_.move_cursor_to(lo);
    anchor_ = lo;
    pushCmd(std::move(cmd));
}

void Document::deleteSelectionAsReplace(std::string inserted) {
    size_t c = cursor();
    size_t a = anchor_;
    size_t lo = std::min(a, c);
    size_t hi = std::max(a, c);
    std::string old = text().substr(lo, hi - lo);
    Command cmd;
    cmd.type = CmdType::Replace;
    cmd.pos = lo;
    cmd.old_text = std::move(old);
    cmd.new_text = std::move(inserted);
    cmd.old_cursor = c;
    cmd.old_anchor = a;
    cmd.new_cursor = lo + cmd.new_text.size();
    cmd.new_anchor = cmd.new_cursor;
    eraseBytes(lo, cmd.old_text.size());
    insertBytes(lo, cmd.new_text);
    buf_.move_cursor_to(cmd.new_cursor);
    anchor_ = cmd.new_anchor;
    pushCmd(std::move(cmd));
}

void Document::insertText(std::string_view utf8) {
    if (utf8.empty()) return;
    size_t c = cursor();
    size_t a = anchor_;
    if (a != c) {
        deleteSelectionAsReplace(std::string(utf8));
        return;
    }
    std::string piece(utf8);
    if (try_coalesce(c, piece)) {
        insertBytes(c, piece);
        buf_.move_cursor_to(c + piece.size());
        anchor_ = cursor();
        redo_stack_.clear();
        return;
    }
    Command cmd;
    cmd.type = CmdType::Insert;
    cmd.pos = c;
    cmd.text = piece;
    cmd.old_cursor = c;
    cmd.old_anchor = a;
    cmd.new_cursor = c + piece.size();
    cmd.new_anchor = cmd.new_cursor;
    insertBytes(c, piece);
    buf_.move_cursor_to(cmd.new_cursor);
    anchor_ = cmd.new_anchor;
    pushCmd(std::move(cmd));
}

void Document::insertCodepoint(char32_t cp) {
    insertText(encodeUtf8(cp));
}

void Document::backspace() {
    size_t c = cursor();
    size_t a = anchor_;
    if (a != c) {
        size_t lo = std::min(a, c);
        size_t hi = std::max(a, c);
        deleteRange(lo, hi, c, a);
        return;
    }
    if (c == 0) return;
    size_t prev = utf8Prev(text(), c);
    deleteRange(prev, c, c, a);
}

void Document::deleteForward() {
    size_t c = cursor();
    size_t a = anchor_;
    if (a != c) {
        size_t lo = std::min(a, c);
        size_t hi = std::max(a, c);
        deleteRange(lo, hi, c, a);
        return;
    }
    size_t n = buf_.length();
    if (c >= n) return;
    size_t next = utf8Next(text(), c);
    deleteRange(c, next, c, a);
}

void Document::replaceAll(std::string text, size_t cursorByte) {
    Command cmd;
    cmd.type = CmdType::Replace;
    cmd.pos = 0;
    cmd.old_text = this->text();
    cmd.new_text = std::move(text);
    cmd.old_cursor = cursor();
    cmd.old_anchor = anchor_;
    size_t n = cmd.new_text.size();
    size_t c = std::min(cursorByte, n);
    cmd.new_cursor = c;
    cmd.new_anchor = c;
    setBytes(cmd.new_text);
    buf_.move_cursor_to(c);
    anchor_ = c;
    pushCmd(std::move(cmd));
}
