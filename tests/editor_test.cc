#include "document.hh"
#include "inline.hh"
#include "layout.hh"
#include "parse.hh"
#include "save.hh"
#include "stub_measure.hh"
#include "utf8.hh"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
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
    assert(doc.undo());
    assert(doc.text() == "abcd");
    assert(doc.cursor() == 3);
    assert(doc.anchor() == 1);
    assert(!doc.undo());
}

static void test_empty_insert_keeps_selection() {
    Document doc;
    doc.load("hello");
    doc.setSelection(1, 4);
    doc.insertText("");
    assert(doc.text() == "hello");
    assert(doc.cursor() == 4);
    assert(doc.anchor() == 1);
    assert(!doc.undo());

    Document bare;
    bare.load("hello");
    bare.setSelection(2, 2);
    bare.insertText("");
    assert(bare.text() == "hello");
    assert(bare.cursor() == 2);
    assert(bare.anchor() == 2);
    assert(!bare.undo());
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

static const Span* spanCovering(const InlineRun& run, size_t displayOff) {
    for (const Span& s : run.spans) {
        if (s.displayBegin <= displayOff && displayOff < s.displayEnd) return &s;
    }
    return nullptr;
}

static void test_bold_hit_lands_on_letter() {
    std::string_view src = "**bold**";
    InlineRun run = scanInlines(src, 0, src.size());
    assert(run.display == "bold");
    const Span* s = spanCovering(run, 0);
    assert(s);
    assert(s->sourceBegin == 2);
    assert(s->role == SpanRole::Strong);
}

static void test_em_code_link() {
    std::string_view emSrc = "*em*";
    InlineRun em = scanInlines(emSrc, 0, emSrc.size());
    assert(em.display == "em");
    const Span* emSpan = spanCovering(em, 0);
    assert(emSpan);
    assert(emSpan->role == SpanRole::Em);
    assert(emSpan->sourceBegin == 1);

    std::string_view codeSrc = "`code`";
    InlineRun code = scanInlines(codeSrc, 0, codeSrc.size());
    assert(code.display == "code");
    const Span* codeSpan = spanCovering(code, 0);
    assert(codeSpan);
    assert(codeSpan->role == SpanRole::Code);

    std::string_view linkSrc = "[name](url)";
    InlineRun link = scanInlines(linkSrc, 0, linkSrc.size());
    assert(link.display == "name");
    assert(link.display.find("url") == std::string::npos);
    const Span* linkSpan = spanCovering(link, 0);
    assert(linkSpan);
    assert(linkSpan->role == SpanRole::Link);
}

static void test_unclosed_marker() {
    std::string_view src = "**bold";
    InlineRun run = scanInlines(src, 0, src.size());
    assert(run.display == "**bold");
    for (const Span& s : run.spans) {
        assert(s.role == SpanRole::Plain);
    }
}

static void test_strong_em() {
    std::string_view aSrc = "**_x_**";
    InlineRun a = scanInlines(aSrc, 0, aSrc.size());
    assert(a.display == "x");
    const Span* aSpan = spanCovering(a, 0);
    assert(aSpan);
    assert(aSpan->role == SpanRole::StrongEm);

    std::string_view bSrc = "_**x**_";
    InlineRun b = scanInlines(bSrc, 0, bSrc.size());
    assert(b.display == "x");
    const Span* bSpan = spanCovering(b, 0);
    assert(bSpan);
    assert(bSpan->role == SpanRole::StrongEm);
}

static void test_escape() {
    std::string_view src = "\\*";
    InlineRun run = scanInlines(src, 0, src.size());
    assert(run.display == "*");
    const Span* s = spanCovering(run, 0);
    assert(s);
    assert(s->sourceBegin == 0);
    assert(s->sourceEnd == 2);
}

static void test_paragraph_join() {
    std::string src = "hello\nworld\n";
    InlineRun run = scanInlines(src, 0, 12);
    assert(run.display == "hello world");
    assert(run.display.empty() || run.display.back() != ' ');
    assert(run.display[5] == ' ');
    const Span* s = spanCovering(run, 5);
    assert(s);
    assert(s->sourceBegin == 5);
    assert(s->sourceEnd == 6);
}

static void test_unmatched_star_inside_strong() {
    std::string_view src = "**5 * 3**";
    InlineRun run = scanInlines(src, 0, src.size());
    assert(run.display == "5 * 3");
    const Span* five = spanCovering(run, 0);
    assert(five);
    assert(five->role == SpanRole::Strong);
    assert(run.display.find('*') != std::string::npos);
}

static void test_nearest_emphasis_closer() {
    {
        std::string_view src = "**5 * 3** and **";
        InlineRun run = scanInlines(src, 0, src.size());
        assert(run.display == "5 * 3 and **");
        const Span* head = spanCovering(run, 0);
        assert(head);
        assert(head->role == SpanRole::Strong);
        const size_t star = run.display.find('*');
        assert(star != std::string::npos);
        const Span* starSpan = spanCovering(run, star);
        assert(starSpan);
        assert(starSpan->role == SpanRole::Strong);
        const size_t trail = run.display.rfind("**");
        assert(trail != std::string::npos);
        const Span* trailSpan = spanCovering(run, trail);
        assert(trailSpan);
        assert(trailSpan->role == SpanRole::Plain);
    }
    {
        std::string_view src = "**a * b** and **c**";
        InlineRun run = scanInlines(src, 0, src.size());
        assert(run.display == "a * b and c");
        const Span* head = spanCovering(run, 0);
        assert(head);
        assert(head->role == SpanRole::Strong);
        const size_t star = run.display.find('*');
        assert(star != std::string::npos);
        const Span* starSpan = spanCovering(run, star);
        assert(starSpan);
        assert(starSpan->role == SpanRole::Strong);
        const size_t mid = run.display.find(" and ");
        assert(mid != std::string::npos);
        const Span* midSpan = spanCovering(run, mid);
        assert(midSpan);
        assert(midSpan->role == SpanRole::Plain);
        const size_t c = run.display.rfind('c');
        assert(c != std::string::npos);
        const Span* cSpan = spanCovering(run, c);
        assert(cSpan);
        assert(cSpan->role == SpanRole::Strong);
    }
    {
        std::string_view src = "__a _ b__ and __c__";
        InlineRun run = scanInlines(src, 0, src.size());
        assert(run.display == "a _ b and c");
        const Span* head = spanCovering(run, 0);
        assert(head);
        assert(head->role == SpanRole::Strong);
        const size_t score = run.display.find('_');
        assert(score != std::string::npos);
        const Span* scoreSpan = spanCovering(run, score);
        assert(scoreSpan);
        assert(scoreSpan->role == SpanRole::Strong);
        const size_t mid = run.display.find(" and ");
        assert(mid != std::string::npos);
        const Span* midSpan = spanCovering(run, mid);
        assert(midSpan);
        assert(midSpan->role == SpanRole::Plain);
        const size_t c = run.display.rfind('c');
        assert(c != std::string::npos);
        const Span* cSpan = spanCovering(run, c);
        assert(cSpan);
        assert(cSpan->role == SpanRole::Strong);
    }
    {
        std::string_view src = "**a *b* c**";
        InlineRun run = scanInlines(src, 0, src.size());
        assert(run.display == "a b c");
        const Span* head = spanCovering(run, 0);
        assert(head);
        assert(head->role == SpanRole::Strong);
        const size_t b = run.display.find('b');
        assert(b != std::string::npos);
        const Span* bSpan = spanCovering(run, b);
        assert(bSpan);
        assert(bSpan->role == SpanRole::StrongEm);
        const size_t c = run.display.find('c');
        assert(c != std::string::npos);
        const Span* cSpan = spanCovering(run, c);
        assert(cSpan);
        assert(cSpan->role == SpanRole::Strong);
    }
}

static void test_strong_inside_em() {
    std::string_view src = "*see **this** now*";
    InlineRun run = scanInlines(src, 0, src.size());
    assert(run.display == "see this now");
    const Span* see = spanCovering(run, 0);
    assert(see);
    assert(see->role == SpanRole::Em);
    size_t thisOff = run.display.find("this");
    assert(thisOff != std::string::npos);
    const Span* inner = spanCovering(run, thisOff);
    assert(inner);
    assert(inner->role == SpanRole::StrongEm);
    size_t nowOff = run.display.find(" now");
    assert(nowOff != std::string::npos);
    const Span* tail = spanCovering(run, nowOff);
    assert(tail);
    assert(tail->role == SpanRole::Em);
}

static LayoutMetrics testMetrics() {
    LayoutMetrics m;
    m.body = 10;
    m.h1 = 10 * 1.18f * 1.18f * 1.18f;
    m.h2 = 10 * 1.18f * 1.18f;
    m.h3 = 10 * 1.18f;
    m.columnW = 50;
    m.blockGap = 16;
    m.listGap = 4;
    m.gutter = 20;
    return m;
}

static std::string kindDisplay(const Page& page, BlockKind kind) {
    std::string s;
    for (const Row& r : page.rows) {
        if (r.kind == kind) s += r.display;
    }
    return s;
}

static void test_active_block_is_source() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string_view src = "# Hi\n\n**bold** word\n";
    std::vector<Block> blocks = parseBlocks(src);
    size_t para = src.find("**bold**");
    Page page = layoutPage(src, blocks, para, m, measure);
    std::string heading = kindDisplay(page, BlockKind::Heading);
    std::string paragraph = kindDisplay(page, BlockKind::Paragraph);
    assert(heading == "Hi");
    assert(heading.find('#') == std::string::npos);
    assert(paragraph.find("**bold**") != std::string::npos);

    Page page2 = layoutPage(src, blocks, 0, m, measure);
    heading = kindDisplay(page2, BlockKind::Heading);
    paragraph = kindDisplay(page2, BlockKind::Paragraph);
    assert(heading.find('#') != std::string::npos);
    assert(paragraph == "bold word" || (paragraph.find("bold") != std::string::npos &&
                                        paragraph.find("word") != std::string::npos));
    assert(paragraph.find('*') == std::string::npos);
}

struct FaceLogMeasurer : Measurer {
    mutable std::vector<std::pair<std::string, Face>> calls;
    float width(std::string_view text, float, Face face) const override {
        calls.emplace_back(std::string(text), face);
        return 1.f;
    }
    float lineHeight(float sizePx) const override { return sizePx; }
};

static void test_rendered_heading_4_measured_bold() {
    FaceLogMeasurer measure;
    LayoutMetrics m = testMetrics();
    m.columnW = 1000;
    std::string src = "#### Title\n\npara\n";
    std::vector<Block> blocks = parseBlocks(src);
    size_t para = src.find("para");
    Page page = layoutPage(src, blocks, para, m, measure);

    const Row* heading = nullptr;
    for (const Row& r : page.rows) {
        if (r.kind == BlockKind::Heading) heading = &r;
    }
    assert(heading);
    assert(!heading->sourceMode);
    assert(heading->level == 4);
    assert(heading->display == "Title");
    assert(page.active != 0);

    std::string seen;
    for (const auto& call : measure.calls) {
        if (seen.size() >= heading->display.size()) break;
        seen += call.first;
        assert(call.second == Face::Bold);
    }
    assert(seen == heading->display);
}

static void test_wrap_at_space() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string src = "\naaaaa bbbbb\n";
    std::vector<Block> blocks = parseBlocks(src);
    Page page = layoutPage(src, blocks, 0, m, measure);
    bool found = false;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        if (page.rows[i].display == "aaaaa") {
            assert(i + 1 < page.rows.size());
            assert(page.rows[i + 1].display.find("bbbbb") == 0);
            found = true;
        }
    }
    assert(found);
}

static void test_wrap_codepoint() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string src = "\nabcdefghij\n";
    std::vector<Block> blocks = parseBlocks(src);
    Page page = layoutPage(src, blocks, 0, m, measure);
    bool found = false;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const Row& r = page.rows[i];
        if (r.kind != BlockKind::Paragraph) continue;
        assert(r.display.size() == 5);
        if (r.display == "abcde") {
            assert(i + 1 < page.rows.size());
            assert(r.sourceEnd == page.rows[i + 1].sourceBegin);
            assert(visualRowEnd(page, i, src) == r.sourceEnd);
            found = true;
        }
    }
    assert(found);

    std::string nl = "abc\n";
    std::vector<Block> nlBlocks = parseBlocks(nl);
    Page nlPage = layoutPage(nl, nlBlocks, 0, m, measure);
    bool nlFound = false;
    for (size_t i = 0; i < nlPage.rows.size(); ++i) {
        const Row& r = nlPage.rows[i];
        if (r.display != "abc") continue;
        const size_t end = visualRowEnd(nlPage, i, nl);
        assert(end >= r.sourceBegin);
        assert(end < r.sourceEnd);
        assert(nl[end] == '\n');
        nlFound = true;
    }
    assert(nlFound);

    std::string soft = "\naaaaa bbbbb\n";
    std::vector<Block> softBlocks = parseBlocks(soft);
    Page softPage = layoutPage(soft, softBlocks, 0, m, measure);
    bool softFound = false;
    for (size_t i = 0; i < softPage.rows.size(); ++i) {
        const Row& r = softPage.rows[i];
        if (r.display != "aaaaa") continue;
        assert(i + 1 < softPage.rows.size());
        assert(r.sourceEnd == softPage.rows[i + 1].sourceBegin);
        const size_t end = visualRowEnd(softPage, i, soft);
        assert(end >= r.sourceBegin);
        assert(end < r.sourceEnd);
        assert(soft[end] == ' ');
        softFound = true;
    }
    assert(softFound);
}

static void test_empty_and_trailing_newline() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string_view empty = "";
    std::vector<Block> none = parseBlocks(empty);
    Page p0 = layoutPage(empty, none, 0, m, measure);
    assert(p0.rows.size() == 1);
    assert(p0.rows[0].sourceBegin == 0);
    assert(p0.rows[0].sourceEnd == 0);

    std::string_view src = "a\n";
    std::vector<Block> blocks = parseBlocks(src);
    Page p1 = layoutPage(src, blocks, 0, m, measure);
    bool sawA = false;
    bool sawEmpty = false;
    for (const Row& r : p1.rows) {
        if (r.display == "a") sawA = true;
        if (r.sourceBegin == 2) {
            assert(r.display.empty());
            sawEmpty = true;
        }
    }
    assert(sawA);
    assert(sawEmpty);
}

static void test_hit_rendered_bold() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string src = "\n**bold**\n";
    std::vector<Block> blocks = parseBlocks(src);
    Page page = layoutPage(src, blocks, 0, m, measure);
    const Row* para = nullptr;
    for (const Row& r : page.rows) {
        if (r.kind == BlockKind::Paragraph) para = &r;
    }
    assert(para);
    float midY = para->y + para->h * 0.5f;
    size_t b = src.find('b');
    assert(hitTest(page, para->textX, midY, measure) == b);
    assert(hitTest(page, 0.f, midY, measure) == para->contentBegin);
}

static void test_move_vertical_keeps_x() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string_view src = "abc\nabcdef\n";
    std::vector<Block> blocks = parseBlocks(src);
    size_t cursor = 3;
    Page page = layoutPage(src, blocks, cursor, m, measure);
    float preferredX = caretX(page, cursor, measure);
    size_t moved = moveVertical(page, cursor, preferredX, 1, measure);
    assert(moved == 7);
}

static void test_fence_line_is_one_row() {
    StubMeasurer measure;
    LayoutMetrics m = testMetrics();
    std::string src = "\n```\nint x;\n```\n";
    std::vector<Block> blocks = parseBlocks(src);
    Page page = layoutPage(src, blocks, 0, m, measure);
    int intX = 0;
    int justInt = 0;
    int justSemi = 0;
    int marker = 0;
    for (const Row& r : page.rows) {
        if (r.display == "int x;") ++intX;
        if (r.display == "int") ++justInt;
        if (r.display == "x;") ++justSemi;
        if (r.kind == BlockKind::Fence && r.display.find('`') != std::string::npos) ++marker;
    }
    assert(intX == 1);
    assert(justInt == 0);
    assert(justSemi == 0);
    assert(marker == 0);

    std::string empty = "\n```\n```\n";
    std::vector<Block> b2 = parseBlocks(empty);
    Page p2 = layoutPage(empty, b2, 0, m, measure);
    size_t openBegin = empty.find("```");
    size_t openEnd = empty.find('\n', openBegin) + 1;
    size_t closeBegin = empty.rfind("```");
    int openRows = 0;
    int closeRows = 0;
    for (const Row& r : p2.rows) {
        if (r.sourceBegin == openBegin && r.sourceEnd == openEnd) ++openRows;
        if (r.sourceBegin == closeBegin) ++closeRows;
    }
    assert(openRows == 1);
    assert(closeRows == 0);
}

static std::string cwdNow() {
    char buf[4096];
    assert(getcwd(buf, sizeof(buf)) != nullptr);
    return buf;
}

static std::string makeTempDir() {
    char tmpl[] = "/tmp/mnrdonw-XXXXXX";
    char* p = mkdtemp(tmpl);
    assert(p != nullptr);
    return p;
}

static void writeFile(const std::string& path, const std::string& bytes) {
    FILE* f = fopen(path.c_str(), "wb");
    assert(f != nullptr);
    assert(fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
    assert(fclose(f) == 0);
}

static std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    assert(f != nullptr);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    assert(n >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    std::string out(static_cast<size_t>(n), '\0');
    assert(fread(out.data(), 1, out.size(), f) == out.size());
    assert(fclose(f) == 0);
    return out;
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static void test_save_round_trip() {
    std::string dir = makeTempDir();
    std::string path = dir + "/note.md";
    SaveResult r = saveBuffer("hello\n", path);
    assert(r.ok);
    assert(r.path == path);
    assert(readFile(path) == "hello\n");
    assert(!fileExists(dir + "/note.md.tmp"));
}

static void test_failed_save_keeps_original() {
    std::string dir = makeTempDir();
    writeFile(dir + "/note.md", "old");
    SaveResult r = saveBuffer("new", dir + "/missing/note.md");
    assert(!r.ok);
    assert(!r.error.empty());
    assert(readFile(dir + "/note.md") == "old");
    assert(!fileExists(dir + "/missing"));
}

static void test_untitled() {
    std::string home = cwdNow();

    std::string dir1 = makeTempDir();
    assert(chdir(dir1.c_str()) == 0);
    SaveResult r1 = saveBuffer("hi", "");
    assert(chdir(home.c_str()) == 0);
    assert(r1.ok);
    assert(readFile(dir1 + "/untitled.md") == "hi");

    std::string dir2 = makeTempDir();
    writeFile(dir2 + "/untitled.md", "old");
    assert(chdir(dir2.c_str()) == 0);
    SaveResult r2 = saveBuffer("new", "");
    assert(chdir(home.c_str()) == 0);
    assert(!r2.ok);
    assert(readFile(dir2 + "/untitled.md") == "old");
}

int main() {
    test_insert_codepoint_undo();
    test_coalesce_breaks_on_newline();
    test_invalid_utf8_is_one_step();
    test_ime_undo_is_one_step();
    test_selection_replace();
    test_empty_insert_keeps_selection();
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
    test_bold_hit_lands_on_letter();
    test_em_code_link();
    test_unclosed_marker();
    test_strong_em();
    test_escape();
    test_paragraph_join();
    test_unmatched_star_inside_strong();
    test_nearest_emphasis_closer();
    test_strong_inside_em();
    test_active_block_is_source();
    test_rendered_heading_4_measured_bold();
    test_wrap_at_space();
    test_wrap_codepoint();
    test_empty_and_trailing_newline();
    test_hit_rendered_bold();
    test_move_vertical_keeps_x();
    test_fence_line_is_one_row();
    test_save_round_trip();
    test_failed_save_keeps_original();
    test_untitled();
    return 0;
}
