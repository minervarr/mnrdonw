#include "document.hh"
#include "utf8.hh"

#include <cassert>

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

int main() {
    test_insert_codepoint_undo();
    test_coalesce_breaks_on_newline();
    test_invalid_utf8_is_one_step();
    test_ime_undo_is_one_step();
    test_selection_replace();
    test_enter_does_not_continue_list();
    test_dirty_clears_on_undo_to_load();
    test_word_step();
    return 0;
}
