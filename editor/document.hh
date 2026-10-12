#pragma once

#include "text_buffer.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Document {
public:
    void load(std::string text);
    void noteSaved();
    const std::string& text() const;
    size_t cursor() const;
    size_t anchor() const;
    bool dirty() const;
    void insertCodepoint(char32_t cp);
    void insertText(std::string_view utf8);
    void backspace();
    void deleteForward();
    void setSelection(size_t anchor, size_t cursor);
    void selectAll();
    void replaceAll(std::string text, size_t cursorByte);
    bool undo();
    bool redo();

private:
    enum class CmdType : uint8_t { Insert, Erase, Replace };

    struct Command {
        CmdType type = CmdType::Insert;
        size_t pos = 0;
        std::string text;
        std::string old_text;
        std::string new_text;
        size_t old_cursor = 0;
        size_t old_anchor = 0;
        size_t new_cursor = 0;
        size_t new_anchor = 0;
    };

    TextBuffer buf_;
    size_t anchor_ = 0;
    std::string saved_;
    mutable std::string cached_text_;
    mutable bool cache_valid_ = false;
    std::vector<Command> undo_stack_;
    std::vector<Command> redo_stack_;

    void invalidateCache();
    void setBytes(const std::string& bytes);
    void insertBytes(size_t pos, const std::string& bytes);
    void eraseBytes(size_t pos, size_t n);
    bool try_coalesce(size_t pos, const std::string& piece);
    void pushCmd(Command cmd);
    void applyForward(const Command& cmd);
    void applyReverse(const Command& cmd);
    void deleteSelectionAsReplace(std::string inserted);
    void deleteRange(size_t lo, size_t hi, size_t old_cursor, size_t old_anchor);
};
