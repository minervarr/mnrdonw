# mnrdonw Live-Source Markdown Editor

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A one-document Wayland editor that shows the caret’s markdown block as source and every other block as styled prose, and saves the buffer bytes unchanged.

**Architecture:** `editor/` is pure document logic (gap buffer, undo, parse, spans, layout, save). `app/` is a `FrameInputView` that draws those rows with vk_canvas and talks to the Wayland host. The two libraries stay unchanged.

**Tech Stack:** C++17, CMake, `app_shell` (`FrameInputView`, Wayland host, `ui_metrics`), `vk_canvas` (`TextBuffer`, `Canvas::textStyled`, shape quads, MSDF), New Computer Modern 10 (Roman, Bold, Italic).

**Spec:** This file. The Design section is the spec. Tasks implement it and do not reopen it.

**Status:** Checked in so another machine can continue. No editor code has been written. Implement the tasks below in order. Auto-review (`/auto`) is a setting on that session, not something stored in this repo. Turn it on after plan mode is off.

## Global Constraints

- C++17. Tests are plain `assert()` in `editor_test`, Debug-only, no test framework.
- `editor/` includes no Vulkan, canvas, or shell headers. It may include `text_buffer.hh`.
- `editor_test` compiles `text_buffer.cc` itself and does not link `vk_canvas_core`. The app links `vk_canvas_core` and does not compile `text_buffer.cc` a second time.
- Do not modify `libs/firstparty/framework/vk_canvas` or `app_shell`.
- The first host is Wayland. `app/` includes no Wayland or Win32 headers.
- Faces: `assets/fonts/newcomputermodern/NewCM10-Regular.otf`, `NewCM10-Bold.otf`, `NewCM10-Italic.otf`. The math style slot stays unloaded. Strong plus emphasis draws with Bold.
- Type ratio is `kUiTypeRatio` (`1.18`) from `ui_metrics.hh`. Body size is `computeUiMetrics(shortSide).text.body`. Heading 3 is one step up, heading 2 is two, heading 1 is three. Headings 4–6 use the heading-3 size and Bold.
- Column width is `min(contentWidth - 2 * space(32), 70 * width of "n" at body)`. Block gap is `space(16)`. Gap between consecutive list items is `space(4)`. List and quote gutter is `2 * body`.
- Caret blink period is 500 ms, timer id `1`.
- Undo coalesces adjacent inserts up to 64 bytes. A newline ends the coalesce.
- Double-click threshold is 0.4 seconds, matching the single-line field.
- Word characters match `widgets.cc` `IsWordChar`: ASCII letter, digit, underscore, or any byte `>= 0x80`.
- Commit from this repo with `./git_wrapper commit "..."`. Never plain `git commit`. Do not commit the library submodules.
- One root `AGENTS.md`. No `CLAUDE.md`, no `TODO.md`, no project skill.

## Review Focus

These are the inputs a reasonable edit will get wrong. Each one has a named test in the task that owns it.

- A lone `0xFF` in the file is one caret step and is still in the buffer after undo of a neighboring insert. `test_invalid_utf8_is_one_step` (Task 1).
- A click on the first glyph of `**bold**` returns the byte of `b`, not a star. `test_bold_hit_lands_on_letter` (Task 3).
- With the caret in the paragraph, that paragraph’s row contains the markers and the heading row does not. `test_active_block_is_source` (Task 4).
- A save whose parent directory is missing leaves an existing sibling file byte-for-byte. `test_failed_save_keeps_original` (Task 5).
- Replacing the whole buffer the way an input method does undoes back to the previous text and caret in one step. `test_ime_undo_is_one_step` (Task 1).

## Design

The file is one markdown buffer. Save writes those bytes. The parse never rewrites them.

A block is a contiguous run of whole lines, range `[begin, end)` in bytes, including each line’s trailing newline when it has one. `contentBegin` is the first byte of the visible content.

| Kind | Recognized when | Content |
|---|---|---|
| Fence | A line of three or more `` ` `` or `~`, the same character. Closes on a line of at least that many of the same character, or at EOF. | Interior lines. The info string stays in the opening line and is not a field. |
| Rule | A line whose non-space characters are three or more of one of `-`, `*`, `_`, spaces allowed between them. | The whole line. |
| Heading | `#{1,6}` then a space or tab. Seven or more hashes, or no space, is a paragraph. | After the marker. `level` is 1..6. |
| List item | Optional spaces, then `-+*` or 1–9 digits and `.` or `)`, then a space or tab. One line. No continuation line and no nesting. | After the marker. `ordered` is true for a number. |
| Quote | A line whose first non-space is `>`. One line. | After one `>` and one optional space. |
| Blank | A line with no non-space characters. | The line. |
| Paragraph | Consecutive other lines, ended by a blank line or a recognized block. | The lines. |

Recognition order at the start of a block is fence, rule, heading, list item, quote, blank, paragraph. A table line, a setext underline, and an HTML line are paragraphs.

Rendered inlines, left to right, on the content range of a heading, paragraph, list item, or quote, and only when that block is not active:

- `` `...` `` is code. Unclosed backtick stays literal.
- `[text](url)` is a link. The visible text is plain. The URL has no glyphs. Unclosed stays literal.
- `**` or `__` is strong. `*` or `_` is emphasis. They nest one level. `**_x_**` and `_**x**_` are strong-plus-emphasis.
- A backslash before `` \ ` * _ [ ] `` hides the backslash. The glyph’s source range covers both bytes.
- An unclosed marker stays literal.
- A newline that sits between two content lines becomes one space whose source byte is that newline. The block’s final trailing newline has no glyph.

The active block is the block whose range contains the caret. It is drawn as its raw source, soft-wrapped, with no inline scan. A fence is one block. Each list item and each quote line is its own block. Consecutive list items use the tighter gap.

A row carries document `y` and `h`, a source range, a display string, spans (`sourceBegin`, `sourceEnd`, `displayBegin`, `displayEnd`, role), and whether it is source mode. Soft wrap breaks at the last space that fits, otherwise on a codepoint boundary. An empty buffer is one empty row at offset 0. A trailing newline has an empty row after the last line.

`hitTest` uses the same measurer as layout. A gutter click returns that row’s `contentBegin` when the row has one, otherwise the row’s `sourceBegin`.

Editing is codepoint-safe. Invalid UTF-8 lead bytes step one byte so the file round-trips. Typing, Enter, paste, Backspace, and Delete replace the selection first. Enter inserts `\n` only. Tab inserts two spaces. Shortcuts run before typed characters, and a Ctrl command does not also insert a glyph. Ctrl+Left/Right and double-click use the word rule above. Up/Down keep a sticky pixel column. Home/End are the visual row’s source ends. Ctrl+Home/End are the buffer ends. An input-method `textEdited` replaces the whole buffer and is one undo step. The app owns that undo stack. `TextBuffer` stores the bytes.

Ctrl+S writes `path.tmp` in the same directory and renames it over `path`. A failed write leaves the existing file. No launch path: the first save creates `untitled.md` in the current directory, and refuses if that name exists. A path that cannot be read reports the error and exits. The title line in the page is the file name, a ` *` when dirty, and a second line for the last save error. Dirty means the buffer differs from the last successful load or save.

The page background, text, dim, panel, track, and accent colors are `col::bg`, `col::text`, `col::dim`, `col::panel`, `col::track`, `col::accent`. The active block sits on `panel`. Inline code sits on `track`. A fence sits on `panel`. The quote bar is accent at `stroke(2)`. A rule is dim at `stroke(1)`. The selection is accent at alpha `0.35`. The caret is accent at `stroke(1)`, drawn on the on-half of the blink. The file name uses the secondary text size and dim.

The frame loop calls `beginFrame()` then `pump(haveWork)`. `haveWork` is true only when a redraw is already pending. The host timer wakes a blink. The app draws on resize, on an edit, caret, selection, or scroll change, and on the blink.

## File Map

- Create: `AGENTS.md` — rules for a later session. Pointer to `README.md` for commands.
- Create: `README.md` — what it is, build, test, run.
- Create: `CMakeLists.txt` — app target `mnrdonw`, Debug target `editor_test`.
- Create: `editor/measure.hh` — `Face`, `Measurer`.
- Create: `editor/utf8.hh`, `editor/utf8.cc` — codepoint and word steps.
- Create: `editor/document.hh`, `editor/document.cc` — buffer, selection, undo, dirty.
- Create: `editor/parse.hh`, `editor/parse.cc` — `Block`, `parseBlocks`.
- Create: `editor/inline.hh`, `editor/inline.cc` — `Span`, `SpanRole`, `scanInlines`.
- Create: `editor/layout.hh`, `editor/layout.cc` — `Row`, `Page`, `layoutPage`, `hitTest`, `caretX`, `moveVertical`.
- Create: `editor/save.hh`, `editor/save.cc` — `saveBuffer`.
- Create: `app/editor_app.hh`, `app/editor_app.cc` — view, draw, keys.
- Create: `app/main.cc` — `app_shell_main`.
- Create: `tests/stub_measure.hh`, `tests/editor_test.cc`.
- Create: `fixtures/sample.md`.
- Consume, do not modify: `libs/firstparty/framework/vk_canvas/core/text_buffer.cc`.

```cpp
enum class Face : uint8_t { Roman, Bold, Italic };

struct Measurer {
    virtual float width(std::string_view text, float sizePx, Face face) const = 0;
    virtual float lineHeight(float sizePx) const = 0;
    virtual ~Measurer() = default;
};

enum class BlockKind : uint8_t {
    Blank, Paragraph, Heading, Rule, ListItem, Quote, Fence
};

struct Block {
    BlockKind kind;
    size_t begin, end, contentBegin;
    int level;       // 1..6 for Heading, else 0
    bool ordered;    // ListItem
};

std::vector<Block> parseBlocks(std::string_view src);

enum class SpanRole : uint8_t { Plain, Strong, Em, StrongEm, Code, Link };

struct Span {
    size_t sourceBegin, sourceEnd;
    size_t displayBegin, displayEnd;
    SpanRole role;
};

struct InlineRun {
    std::string display;
    std::vector<Span> spans;
};

InlineRun scanInlines(std::string_view src, size_t begin, size_t end);

struct LayoutMetrics {
    float body, h1, h2, h3;
    float columnW, blockGap, listGap, gutter;
};

struct Row {
    float y, h, textX, sizePx;
    size_t sourceBegin, sourceEnd, contentBegin;
    std::string display;
    std::vector<Span> spans;
    bool sourceMode;
    BlockKind kind;
    int level;
    bool ordered;
    std::string gutter;
};

struct Page {
    std::vector<Row> rows;
    float height;
    size_t active;   // index into the block vector passed to layout
};

Page layoutPage(std::string_view src, const std::vector<Block>& blocks,
                size_t cursorByte, const LayoutMetrics& m, const Measurer& measure);
size_t hitTest(const Page& page, float x, float y, const Measurer& measure);
float caretX(const Page& page, size_t cursorByte, const Measurer& measure);
size_t moveVertical(const Page& page, size_t cursorByte, float preferredX,
                    int direction, const Measurer& measure);

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
};

struct SaveResult {
    bool ok;
    std::string error;
    std::string path;
};

SaveResult saveBuffer(const std::string& bytes, const std::string& path);
```

`StubMeasurer::width` is `codepointCount(text) * sizePx`. `lineHeight` is `sizePx`. `codepointCount` counts one for a valid sequence and one for a lone bad byte.

---

### Task 1: Document, UTF-8, and the test harness

**Files:**
- Create: `CMakeLists.txt`
- Modify: `AGENTS.md` (already the text below), `README.md` (replace the pointer with the build text below once `editor_test` builds)
- Create: `editor/measure.hh`, `editor/utf8.hh`, `editor/utf8.cc`, `editor/document.hh`, `editor/document.cc`
- Create: `tests/stub_measure.hh`, `tests/editor_test.cc`
- Compile into the test only: `libs/firstparty/framework/vk_canvas/core/text_buffer.cc`

**Interfaces:**
- Consumes: `TextBuffer` (`insert(string)`, `erase_before`, `erase_after`, `move_cursor_to`, `to_string`, `cursor_pos`, `length`).
- Produces: `Measurer`, `Face`, the `Document` methods above, `utf8Prev`, `utf8Next`, `prevWord`, `nextWord`, `wordAt`.

- [ ] **Step 1: Write the failing tests in `tests/editor_test.cc`**

Call these from `main`. Each is a function whose asserts are the spec.

- `test_insert_codepoint_undo`: `load("")`, `insertCodepoint(U'é')`, text is `"\xC3\xA9"`, cursor is 2. One `undo()` restores `""` and cursor 0.
- `test_coalesce_breaks_on_newline`: insert `'a'`, insert `'b'`, one undo restores `""`. Then insert `'a'`, `insertText("\n")`, one undo leaves `"a"`, the next leaves `""`.
- `test_invalid_utf8_is_one_step`: load `"\xFF"`, cursor 0, `insertCodepoint(U'a')` at 0 yields `"a\xFF"`. `setSelection(1, 1)`, `utf8Next` from 1 is 2. Backspace at cursor 2 removes only the `0xFF`.
- `test_ime_undo_is_one_step`: load `"ab"` with cursor 2, `replaceAll("xyz", 1)`, text is `"xyz"` and cursor is 1. One undo restores `"ab"` and cursor 2.
- `test_selection_replace`: text `"abcd"`, `setSelection(1, 3)`, `insertText("X")` yields `"aXd"` and cursor 2.
- `test_enter_does_not_continue_list`: load `"- item"`, cursor at end, `insertText("\n")` yields `"- item\n"`.
- `test_dirty_clears_on_undo_to_load`: load `"ab"` is clean, insert `'c'` is dirty, undo is clean.
- `test_word_step`: `nextWord("ab cd", 0)` is 2. `prevWord("ab cd", 5)` is 3. `wordAt("ab cd", 4)` is `{3, 5}`.

- [ ] **Step 2: Run the test and confirm it does not compile**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target editor_test`

Expected: failure because `editor_test.cc` has nothing to link yet. A missing header is the right failure.

- [ ] **Step 3: Add the harness and implement the document**

Root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.22)`, `project(mnrdonw CXX)`, `CMAKE_CXX_STANDARD 17`. Before `add_subdirectory(libs/firstparty/framework/app_shell)` set cache variables `APP_SHELL_APP_NAME=mnrdonw`, `APP_SHELL_WIN_CLASS=MnrdonwMain`, `APP_SHELL_WINDOW_TITLE=mnrdonw`, `APP_SHELL_APP_ID=io.nava.mnrdonw`. The app target is added in Task 6. This task only builds `editor_test`.

`editor_test` is a Debug-only executable. It compiles the `editor/` sources plus `text_buffer.cc`. Include path: `editor/` and `libs/firstparty/framework/vk_canvas/core`. It does not link Vulkan.

`Document` stores a `TextBuffer`, an anchor, and an undo stack of insert, erase, and replace commands. Each command remembers the caret and anchor from before it applied. `try_coalesce` appends to the last insert when the insert is adjacent, the combined text is at most 64 bytes, and neither piece contains `\n`. `dirty()` compares `text()` to the string stored by `load` or `noteSaved`. `replaceAll` is one command. `insertText("  ")` is what Tab will call. Movement by codepoint uses `utf8Prev` and `utf8Next`, and a byte that is not a valid lead steps by one.

`AGENTS.md` text:

```markdown
# mnrdonw

A one-document live-source markdown editor. The file is the markdown buffer. The block that contains the caret is drawn as source. Every other block is drawn as styled prose. Save writes the buffer bytes.

## Where code lives

- `editor/` is pure document logic. It does not include Vulkan, the canvas, or the shell.
- `app/` is the window. It is a `FrameInputView`. It draws with the canvas and loads and saves files.
- `libs/firstparty/framework/vk_canvas` and `app_shell` are consumed as they are. Editor behavior goes in this repo. Read those trees' `CLAUDE.md` before touching them.

## Checks

Build and test with the commands in `README.md`. `editor_test` is the gate for document behavior. The Wayland window is checked by opening `fixtures/sample.md`.

## Tests

Tests approve the code. Production code stays behind them.

For a task, write its asserts in `tests/editor_test.cc` first. Run `editor_test` and watch it fail because that behavior is missing: a missing header, or an assert on the behavior itself. Then write only the code that makes those asserts pass. `./build/editor_test` must exit 0 before the next task starts. One task at a time. Do not add behavior the current tests do not name.

## Plan

No editor code is in the tree yet. The spec and the task list are `docs/superpowers/plans/2026-10-11-live-source-markdown-editor.md`. Implement it task by task. Do not reopen the design.

## Commits

From this repo, commit with `./git_wrapper`, never plain `git commit` or `git push`. The libraries are separate repositories with their own wrappers. Do not commit them from here unless the task changes them.
```

`README.md` text:

```markdown
# mnrdonw

A one-document markdown editor. The caret's block shows the source. The rest of the page is styled prose.

## Build

Debug, from this directory:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build

## Test

    ./build/editor_test

## Run

    ./build/mnrdonw fixtures/sample.md
```

- [ ] **Step 4: Run the test**

Run: `cmake --build build --target editor_test && ./build/editor_test`

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Add the markdown document buffer and its tests"
```

---

### Task 2: Blocks

**Files:**
- Create: `editor/parse.hh`, `editor/parse.cc`
- Modify: `tests/editor_test.cc`, `CMakeLists.txt` (add `parse.cc` to `editor_test`)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `Block`, `BlockKind`, `parseBlocks`.

- [ ] **Step 1: Write the failing tests**

- `test_blocks_prose`: source `"# Hi\n\n## Sub\n\npara\nline\n\n---\n\n- item\n1. two\n\n> q\n"`. Assert the kinds in order: Heading level 1 with `contentBegin` on `H`, Blank, Heading level 2, Blank, one Paragraph whose range covers both `para` and `line`, Blank, Rule, Blank, ListItem unordered, ListItem ordered, Blank, Quote with `contentBegin` on `q`.
- `test_not_a_heading`: `"#Title\n"` and `"####### no\n"` are each one Paragraph.
- `test_rule_spaces`: `"- - -\n"` is a Rule. `"--- extra\n"` is a Paragraph.
- `test_unclosed_fence`: `"```cpp\ncode\n"` is one Fence whose `end` is the size and whose `contentBegin` is the start of `code`.
- `test_closed_fence`: `"```cpp\nint x;\n```\n"` is one Fence. The closing line is inside `[begin, end)`.
- `test_unknown_stays_paragraph`: `"| a | b |\n"` and `"<div>\n"` and `"====\n"` are paragraphs. `parseBlocks` does not change a copy of the source compared before and after.
- `test_quote_marker`: `"> hello\n"` has `contentBegin` just after `"> "`. `">> nested\n"` has `contentBegin` on the second `>`.

- [ ] **Step 2: Run `./build/editor_test` and confirm the new tests fail to compile**

- [ ] **Step 3: Implement `parseBlocks`**

Walk lines. Apply the recognition order from the Design section. A paragraph absorbs following lines until a blank line or a line that would open a fence, rule, heading, list item, or quote. Ranges include the trailing newline when the line has one. Do not allocate a rewritten copy of the file.

- [ ] **Step 4: Run `./build/editor_test`**

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Parse prose markdown into line blocks"
```

---

### Task 3: Inline spans

**Files:**
- Create: `editor/inline.hh`, `editor/inline.cc`
- Modify: `tests/editor_test.cc`, `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing. Source offsets are absolute into the same buffer the caller passes.
- Produces: `Span`, `SpanRole`, `InlineRun`, `scanInlines(src, begin, end)`.

- [ ] **Step 1: Write the failing tests**

`scanInlines` on a slice. Display offsets are into `InlineRun::display`. Source offsets are into `src`.

- `test_bold_hit_lands_on_letter`: `**bold**` displays `bold`. The span covering display `0` has `sourceBegin == 2` and role `Strong`.
- `test_em_code_link`: `*em*` displays `em`, role `Em`, source of `e` is 1. `` `code` `` displays `code`, role `Code`. `[name](url)` displays `name`, role `Link`, and `url` does not appear in `display`.
- `test_unclosed_marker`: `**bold` displays `**bold` with every span `Plain`.
- `test_strong_em`: `**_x_**` displays `x` with role `StrongEm`. `_**x**_` displays `x` with role `StrongEm`.
- `test_escape`: `\*` displays `*`. That span’s `sourceBegin` is 0 and `sourceEnd` is 2.
- `test_paragraph_join`: source `hello\nworld\n`, scan `[0, 12)`. Display is `hello world`. The space span’s source range is the newline at index 5. Display does not end with a space.

- [ ] **Step 2: Run `./build/editor_test` and confirm the new tests fail to compile**

- [ ] **Step 3: Implement `scanInlines`**

Follow the inline order in the Design section. Markers that are consumed have no display bytes. A span’s display range is half-open into `display` and concatenates to the whole string.

- [ ] **Step 4: Run `./build/editor_test`**

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Map rendered markdown spans back to source bytes"
```

---

### Task 4: Layout and hit testing

**Files:**
- Create: `editor/layout.hh`, `editor/layout.cc`
- Modify: `tests/editor_test.cc`, `tests/stub_measure.hh`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `parseBlocks`, `scanInlines`, `Measurer`.
- Produces: `LayoutMetrics`, `Row`, `Page`, `layoutPage`, `hitTest`, `caretX`, `moveVertical`.

- [ ] **Step 1: Write the failing tests**

Use `StubMeasurer` and `LayoutMetrics` with `body = 10`, `h1 = 10 * 1.18^3`, `h2 = 10 * 1.18^2`, `h3 = 10 * 1.18`, `columnW = 50`, `blockGap = 16`, `listGap = 4`, `gutter = 20`. Width is codepoints times size, so at body size five codepoints fill the column.

- `test_active_block_is_source`: source `"# Hi\n\n**bold** word\n"`, cursor at the start of the paragraph. The heading row’s display is `Hi` and does not contain `#`. The paragraph row’s display contains `**bold**`. Then the same source with the cursor inside the heading: the heading display contains `#`, and the paragraph display is `bold word` with no `*`.
- `test_wrap_at_space`: a rendered paragraph `aaaaa bbbbb\n` with the cursor in another block (prefix a blank line and put the cursor there) produces a row whose display is `aaaaa` and a following row whose display starts with `bbbbb`.
- `test_wrap_codepoint`: `abcdefghij\n` as a rendered paragraph breaks so the first row’s display is five characters.
- `test_empty_and_trailing_newline`: `""` lays out one row with source range `{0, 0}`. `"a\n"` lays out a row for `a` and an empty row whose `sourceBegin` is 2.
- `test_hit_rendered_bold`: cursor in a leading blank, paragraph `**bold**\n`. `hitTest` at the paragraph row’s `textX` and its vertical midpoint returns the source byte of `b`. `hitTest` at `x = 0` on that same row returns the row’s `contentBegin`.
- `test_move_vertical_keeps_x`: two source-mode lines `abc\n` and `abcdef\n` with the caret after `c` on the first line. `moveVertical` downward with `preferredX` at that caret returns the byte after `c` on the second line.

- [ ] **Step 2: Run `./build/editor_test` and confirm the new tests fail to compile**

- [ ] **Step 3: Implement layout**

`layoutPage` picks the active block as the last block whose `[begin, end)` contains `cursorByte`. A cursor equal to `text.size()` belongs to the empty row after a trailing newline, or to the single empty row. Each row stores the `sizePx` it was measured with: body for prose, the heading size for a rendered heading. `hitTest`, `caretX`, and `moveVertical` measure a row with that `sizePx`. Face comes from the span role: emphasis is Italic, strong and strong-plus-emphasis are Bold, and everything else is Roman. Rendered headings, paragraphs, list items, and quotes go through `scanInlines`. Rendered fences emit one row per interior line and no row for the fence lines. An empty fence emits one row whose source is the opening line. Rules and blanks emit one empty row of `lineHeight(body)`. Source-mode rows wrap the raw slice. Consecutive list items are separated by `listGap`. Every other pair of blocks is separated by `blockGap`. `hitTest` walks rows by `y`, then spans by measured display width. An `x` left of `textX` returns the row’s `contentBegin`, or `sourceBegin` when the row has no separate content. `moveVertical` finds the caret’s row, steps one row, and hit-tests `preferredX` on it.

- [ ] **Step 4: Run `./build/editor_test`**

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Lay out active source blocks and rendered prose"
```

---

### Task 5: Save

**Files:**
- Create: `editor/save.hh`, `editor/save.cc`
- Modify: `tests/editor_test.cc`, `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from the document. The caller passes bytes and a path.
- Produces: `SaveResult`, `saveBuffer(bytes, path)`.

- [ ] **Step 1: Write the failing tests**

Use a temporary directory and `chdir` into it for the untitled cases. Restore the previous working directory before returning.

- `test_save_round_trip`: write nothing yet. `saveBuffer("hello\n", dir + "/note.md")` returns ok, `path` is that file, the file bytes are `hello\n`, and `note.md.tmp` does not exist.
- `test_failed_save_keeps_original`: write `dir/note.md` as `old`. Call `saveBuffer("new", dir + "/missing/note.md")`. Result is not ok, `error` is non-empty, and `dir/note.md` is still `old`.
- `test_untitled`: in an empty temp cwd, `saveBuffer("hi", "")` creates `untitled.md` with those bytes. Create `untitled.md` first in a second temp cwd and call `saveBuffer("new", "")`. Result is not ok and the file is still the original bytes.

- [ ] **Step 2: Run `./build/editor_test` and confirm the new tests fail to compile**

- [ ] **Step 3: Implement `saveBuffer`**

Empty `path` means `untitled.md` in the current directory. If that file exists, return `{false, "untitled.md already exists", "untitled.md"}` and do not open it. Otherwise write `path + ".tmp"`, close it, and `rename` it over `path`. On any failure, remove the temp file if it was created, leave the destination untouched, and return `ok == false` with `error` set from `errno`.

- [ ] **Step 4: Run `./build/editor_test`**

Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Save the markdown buffer by rename"
```

---

### Task 6: Wayland window

**Files:**
- Create: `app/editor_app.hh`, `app/editor_app.cc`, `app/main.cc`, `fixtures/sample.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Document`, `parseBlocks`, `layoutPage`, `hitTest`, `caretX`, `moveVertical`, `saveBuffer`, `Measurer`, `computeUiMetrics`, `kUiTypeRatio`, `FrameInputView`, `Host`, `Canvas`, `col::`.
- Produces: `int app_shell_main(int argc, char** argv)` and the `mnrdonw` executable.

- [ ] **Step 1: Write `fixtures/sample.md`**

One file that contains, in order: a heading, a paragraph with bold and a link, a list of two items, a quote, a fence with one line of code, and a rule. This is the manual fixture. No test executable change.

- [ ] **Step 2: Add the app target**

`add_subdirectory` of `app_shell`, then `vk_canvas/core`, then `vk_canvas/platform/linux`. Call `app_shell_generate_min_text_size` on the three New Computer Modern faces and `add_dependencies(app_shell generate_ui_min_text_size)`. Compile the shader set named in `vk_canvas/CLAUDE.md`: the six shaders under `vk_canvas/shaders_src` plus `composite`, `tiling`, `coverage`, `msdf_vert`, and `msdf_frag` from the font engine, via `vce_compile_slang`, output beside the executable. Copy the three face files into the build tree at `assets/fonts/newcomputermodern/` relative to the executable.

Link `mnrdonw` to `app_shell`, `app_shell_wayland`, and `vk_canvas_wayland`. Do not add `text_buffer.cc` to this target. The view’s measurer calls `Canvas::textWidthStyled` and maps `Face::Roman/Bold/Italic` to `FontStyle::Roman/Bold/Italic`.

- [ ] **Step 3: Implement the view**

`EditorApp` extends `FrameInputView`. `app_shell_main` builds the host, loads `argv[1]` when present, and on a read failure calls `showErrorMessage` and returns 1. No argument loads an empty document.

Each frame: `beginFrame()`, `pump(haveWork)`, then draw only when a resize, input, edit, or blink made the picture stale. `startTimer(1, 500)` flips caret visibility. Map keys as in the Design section. A positive wheel notch of 120 reveals later text by `3 * lineHeight(body)`. Page Up and Page Down move the caret by one viewport at the sticky x. After every caret move, scroll so the caret row is inside the column viewport.

Draw the file name in the secondary size. Append ` *` when `dirty()`. A failed save sets a dim second line to `SaveResult::error`. The column is centered and capped as in Global Constraints. Draw rows with `textStyled` for text and shape quads for panels, code pads, the quote bar, the rule, the selection, and the caret. Selection alpha is `0.35`. The caret is omitted on the off half of the blink.

Ctrl+S calls `saveBuffer` with the launch path, or `""` when there was none. On success, `noteSaved()` and remember the path `saveBuffer` returned so the next save overwrites that file.

- [ ] **Step 4: Build both targets and run the fixture**

Run: `cmake --build build && ./build/editor_test && ./build/mnrdonw fixtures/sample.md`

Expected: `editor_test` exits 0. The window shows the fixture. Click the heading and the hashes appear. Click back into the paragraph and the hashes disappear. Type a character, press Ctrl+S, and the fixture file on disk contains that character and no rewritten markers anywhere else. Closing the window exits.

- [ ] **Step 5: Commit**

```bash
./git_wrapper commit "Open one markdown file in a Wayland window"
```
