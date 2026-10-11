#include "document.hh"
#include "parse.hh"
#include "utf8.hh"

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

static void test_insert_codepoint_undo() {
    Document doc;
    doc.load("");
    doc.insertCodepoint(U'é');
    assert(doc.text() == "\xC3\xA9");
    assert(doc.cursor() == 2);
    assert(doc.undo());
    assert(doc.text() == "");
    assert(doc.cursor() == 0);
}

static void test_coalesce_breaks_on_newline() {
    Document doc;
    doc.load("");
    doc.insertCodepoint(U'a');
    doc.insertCodepoint(U'b');
    assert(doc.undo());
    assert(doc.text() == "");
    doc.insertCodepoint(U'a');
    doc.insertText("\n");
    assert(doc.undo());
    assert(doc.text() == "a");
    assert(doc.undo());
    assert(doc.text() == "");
}

static void test_invalid_utf8_is_one_step() {
    Document doc;
    doc.load("\xFF");
    assert(doc.cursor() == 0);
    doc.insertCodepoint(U'a');
    assert(doc.text() == "a\xFF");
    assert(doc.undo());
    assert(doc.text() == "\xFF");
    assert(doc.cursor() == 0);
    doc.insertCodepoint(U'a');
    assert(doc.text() == "a\xFF");
    doc.setSelection(1, 1);
    assert(utf8Next(doc.text(), 1) == 2);
    doc.setSelection(2, 2);
    doc.backspace();
    assert(doc.text() == "a");
}

static void test_ime_undo_is_one_step() {
    Document doc;
    doc.load("ab");
    doc.setSelection(2, 2);
    doc.replaceAll("xyz", 1);
    assert(doc.text() == "xyz");
    assert(doc.cursor() == 1);
    assert(doc.undo());
    assert(doc.text() == "ab");
    assert(doc.cursor() == 2);
}

static void test_selection_replace() {
    Document doc;
    doc.load("abcd");
    doc.setSelection(1, 3);
    doc.insertText("X");
    assert(doc.text() == "aXd");
    assert(doc.cursor() == 2);
}

static void test_enter_does_not_continue_list() {
    Document doc;
    doc.load("- item");
    doc.setSelection(doc.text().size(), doc.text().size());
    doc.insertText("\n");
    assert(doc.text() == "- item\n");
}

static void test_dirty_clears_on_undo_to_load() {
    Document doc;
    doc.load("ab");
    assert(!doc.dirty());
    doc.insertCodepoint(U'c');
    assert(doc.dirty());
    assert(doc.undo());
    assert(!doc.dirty());
}

static void test_word_step() {
    assert(nextWord("ab cd", 0) == 2);
    assert(prevWord("ab cd", 5) == 3);
    std::pair<size_t, size_t> w = wordAt("ab cd", 4);
    assert(w.first == 3 && w.second == 5);
}

static void test_blocks_prose() {
    std::string_view src = "# Hi\n\n## Sub\n\npara\nline\n\n---\n\n- item\n1. two\n\n> q\n";
    std::vector<Block> b = parseBlocks(src);
    assert(b.size() == 12);
    assert(b[0].kind == BlockKind::Heading);
    assert(b[0].level == 1);
    assert(src[b[0].contentBegin] == 'H');
    assert(b[1].kind == BlockKind::Blank);
    assert(b[2].kind == BlockKind::Heading);
    assert(b[2].level == 2);
    assert(b[3].kind == BlockKind::Blank);
    assert(b[4].kind == BlockKind::Paragraph);
    assert(b[4].begin == src.find("para"));
    assert(b[4].end == src.find("line") + 5);
    assert(b[5].kind == BlockKind::Blank);
    assert(b[6].kind == BlockKind::Rule);
    assert(b[7].kind == BlockKind::Blank);
    assert(b[8].kind == BlockKind::ListItem);
    assert(!b[8].ordered);
    assert(b[9].kind == BlockKind::ListItem);
    assert(b[9].ordered);
    assert(b[10].kind == BlockKind::Blank);
    assert(b[11].kind == BlockKind::Quote);
    assert(src[b[11].contentBegin] == 'q');
}

static void test_not_a_heading() {
    std::vector<Block> a = parseBlocks("#Title\n");
    assert(a.size() == 1);
    assert(a[0].kind == BlockKind::Paragraph);
    std::vector<Block> b = parseBlocks("####### no\n");
    assert(b.size() == 1);
    assert(b[0].kind == BlockKind::Paragraph);
}

static void test_rule_spaces() {
    std::vector<Block> a = parseBlocks("- - -\n");
    assert(a.size() == 1);
    assert(a[0].kind == BlockKind::Rule);
    std::vector<Block> b = parseBlocks("--- extra\n");
    assert(b.size() == 1);
    assert(b[0].kind == BlockKind::Paragraph);
}

static void test_unclosed_fence() {
    std::string_view src = "```cpp\ncode\n";
    std::vector<Block> b = parseBlocks(src);
    assert(b.size() == 1);
    assert(b[0].kind == BlockKind::Fence);
    assert(b[0].end == src.size());
    assert(b[0].contentBegin == src.find("code"));
}

static void test_closed_fence() {
    std::string_view src = "```cpp\nint x;\n```\n";
    std::vector<Block> b = parseBlocks(src);
    assert(b.size() == 1);
    assert(b[0].kind == BlockKind::Fence);
    size_t close = src.rfind("```");
    assert(close >= b[0].begin && close < b[0].end);
}

static void test_unknown_stays_paragraph() {
    const char* samples[] = {"| a | b |\n", "<div>\n", "====\n"};
    for (const char* s : samples) {
        std::string src = s;
        std::string before = src;
        std::vector<Block> b = parseBlocks(src);
        assert(src == before);
        assert(b.size() == 1);
        assert(b[0].kind == BlockKind::Paragraph);
    }
}

static void test_quote_marker() {
    std::string_view a = "> hello\n";
    std::vector<Block> ba = parseBlocks(a);
    assert(ba.size() == 1);
    assert(ba[0].kind == BlockKind::Quote);
    assert(ba[0].contentBegin == 2);
    std::string_view c = ">> nested\n";
    std::vector<Block> bc = parseBlocks(c);
    assert(bc.size() == 1);
    assert(bc[0].kind == BlockKind::Quote);
    assert(bc[0].contentBegin == 1);
    assert(c[bc[0].contentBegin] == '>');
}

int main() {
    test_insert_codepoint_undo();
    test_coalesce_breaks_on_newline();
    test_invalid_utf8_is_one_step();
    test_ime_undo_is_one_step();
    test_selection_replace();
    test_enter_does_not_continue_list();
    test_dirty_clears_on_undo_to_load();
    test_word_step();
    test_blocks_prose();
    test_not_a_heading();
    test_rule_spaces();
    test_unclosed_fence();
    test_closed_fence();
    test_unknown_stays_paragraph();
    test_quote_marker();
    return 0;
}
