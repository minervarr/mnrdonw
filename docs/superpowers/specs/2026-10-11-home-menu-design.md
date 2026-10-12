# mnrdonw Home Menu

The app opens on a black home screen of recent files. The writing page fills the window inside a one-centimeter margin. A Linux file manager can hand the editor a `.md` file.

This spec supersedes three rules in `docs/superpowers/plans/2026-10-11-live-source-markdown-editor.md`: the centered column of `min(contentWidth - 2 * space(32), 70 * width of "n")`, the empty launch that opens a writing buffer with no path, and the first save of an empty path as `untitled.md` in the current directory. Parse, spans, editing, undo, caret blink, and the atomic save of a known path stay as that plan says.

## Screens

`EditorApp` is on one screen, `Home` or `Write`. Home either lists recent names or shows the file browser. The browser is showing when the recent list is empty, and when the user has asked for it.

A launch argument opens `Write` on that path and moves the path to the front of the recent list. Only `argv[1]` is read. A launch path that cannot be read still shows the error dialog and exits. The desktop entry passes one local filesystem path (`%f`), so the app does not accept a `file://` URL.

A launch with no path opens `Home`.

### Home

The background is `col::bg`. **New** and **Browse** sit on the first line of the content box, in `col::dim`, at body size. They stay pinned while the names scroll. **Browse** is omitted when the browser is already showing.

Recent entries show the file name only, in `col::text`, at body size. The name under the pointer is `col::accent`. A click opens that file. A path that cannot be read is removed from the list, and the error is a dim line under the controls. That line stays until the next click.

**New** opens `Write` on an empty buffer with an empty path. The buffer is clean. It is not added to the recent list.

The names pack left to right inside the content box. The gap between two names is `space(32)`. A name whose width plus that gap still fits stays on the current row. Otherwise it starts the next row. A name wider than the content box sits alone on its row, clamped to that width, and is drawn with an ellipsis. The gap between rows is `space(16)`. The wheel scrolls the names. The scroll clamps to the range that still shows the flow.

### Browser

The browser lists one directory. It starts at `userDocuments()`. `..` is the first row when the directory has a parent. Directories follow, then regular files. Each group is ordered by file name, ASCII A–Z folded to a–z, and byte order after that. A directory's label has a trailing `/`. The path behind the row does not. Names that start with `.` are absent here because the listing omitted them.

The list is `drawScrollList`. Its background and hover fill are `col::bg`. Its hover text is `col::accent`. `selected` is `-1`. The fit is `kTextFit`. The row height is `body / 0.42`, so the widget's glyph size (`rowH * 0.42`) equals the body size. The wheel scrolls. A directory click lists that directory. A file click opens it and moves it to the front of the recent list. A click that cannot be read leaves the list where it is, shows the error on the dim line, and removes that path from the recent list when it is present.

Escape while the browser is showing and the recent list is non-empty returns to the names. Escape while the recent list is empty leaves the browser where it is.

An empty `userDocuments()` shows no rows and the dim line `no home directory`.

### Write

The text column is the full width of the content box. Its left edge is the margin. There is no centered 70-character column.

The file name is the first line inside the box, dim, at the secondary size, with ` *` when the buffer is dirty. The unsaved notice or the last save error is the next dim line. Prose wraps to `contentWidth - gutter` and scrolls inside the box, ending at the bottom margin. The gutter, block gap, list gap, colors, and caret stay as the live-source plan says.

Escape on a clean buffer returns to `Home`. A clean empty path is not stored as a recent file. A clean path stays at the front of the list.

Escape on a dirty buffer stays on `Write` and arms discard. The second line reads `unsaved`. A second Escape while armed throws the buffer away and returns to `Home`. The file on disk is left as it was. A change to the buffer clears the arm. Moving the caret does not. A successful save clears the arm. A failed save leaves it armed.

Ctrl+S on a known path is the existing atomic save. On success that path moves to the front of the recent list.

Ctrl+S on an empty path writes `untitled.md` inside `userDocuments()`. When that file already exists, the save error is `untitled.md already exists` and the file is not opened or overwritten. `saveBuffer("")` is not used for this, so the current directory is not the save location. When `userDocuments()` is empty, the save error is `no home directory`.

Closing the window quits. Unsaved bytes are not written.

## Geometry

```text
inset(span, scale) = 38 * scale
when span - 2 * inset < 160 * scale:
    inset = max(0, (span - 160 * scale) / 2)
pageMargin(scale, w, h) = min(inset(w, scale), inset(h, scale))
```

`38 * scale` is about one centimeter at 96 dpi when `scale` is 1. `scale` is `computeUiMetrics(short side).scale`. The content box is the window inset by `pageMargin` on every edge. A window smaller than `160 * scale` on an axis uses a zero margin on that axis, and the page margin is the smaller of the two axes.

`packFlow(contentW, gapX, rowH, gapY, widths)` returns one slot per width, in order. A slot is `{index, x, y, w, h}`.

- An empty width list returns no slots.
- `contentW <= 0` places every item on its own row at `x = 0`, `w = 0`, `h = rowH`, with `gapY` between rows.
- Otherwise each width is clamped to `[0, contentW]`.
- The first slot on a row has `x = 0`. The next slot fits when `x + previousW + gapX + thisW <= contentW`.
- A slot that does not fit starts the next row. `y` advances by `rowH + gapY`.
- `h` is `rowH`. After clamping, a slot always fits on an empty row.

Example, `contentW = 100`, `gapX = 10`, `rowH = 20`, `gapY = 8`, widths `30, 40, 50, 200`:

| index | x | y | w |
|---|---|---|---|
| 0 | 0 | 0 | 30 |
| 1 | 40 | 0 | 40 |
| 2 | 0 | 28 | 50 |
| 3 | 0 | 56 | 100 |

## Recent list

`APP_SHELL_STATE_HOME` is `.mnrdonw`, set with `CACHE FORCE` before `add_subdirectory(app_shell)`. The app writes under `~/.mnrdonw/`, including the log and the font cache. The first launch after this change rebuilds the atlas cache there. The cache left beside the binary stays where it is.

The list file is `stateDir() + "recent.txt"`. It is UTF-8, one absolute path per line, most recent first, at most 40 paths, with a trailing newline. Blank lines are dropped. A repeated path keeps its first occurrence.

`loadRecent` on a missing file returns success and no paths. On a file that exists and cannot be read, it returns failure. The app then refuses to rewrite the list for the rest of the process, so a read error cannot replace the file with a short list. `saveRecent` writes `recent.txt.tmp` beside the file and renames it over. A failed write leaves the previous file and removes the temp file.

`rememberPath` inserts at the front, removes any later copy, and drops paths past the cap. An empty path is ignored. `forgetPath` removes every equal path.

## Documents and listing

Both live in `arc::fs`. `Layout::documents` stays `Documents` under home and does not grow a filesystem read.

`documentsDirFromUserDirs(text, home)` is pure. It finds `XDG_DOCUMENTS_DIR=`. Blank lines and lines whose first non-space is `#` are ignored. One pair of surrounding double quotes is stripped. A value that begins with `$HOME` or `${HOME}` replaces that prefix with `home`. Any other `$` yields an empty string. A missing key yields an empty string.

`userDocuments()` never creates a directory.

- Linux: read `$XDG_CONFIG_HOME/user-dirs.dirs` when `XDG_CONFIG_HOME` is set, otherwise `$HOME/.config/user-dirs.dirs`. Use the parsed path when it is an existing directory. Otherwise `$HOME/Documents` when that is an existing directory. Otherwise `$HOME` when that is an existing directory. Otherwise empty.
- Windows: `Documents` under `USERPROFILE` when that directory exists, otherwise `USERPROFILE`, otherwise empty.
- Android: `layout("mnrdonw").documents` when that directory exists, otherwise that layout's `home` when it exists, otherwise empty.

`listDir(path)` returns `Result<std::vector<Entry>>` and does not throw. It lists one level with the `error_code` overloads. `.` and `..` are omitted. A name that starts with `.` is omitted. A symlink is stat'd once through its target: a directory target is `isDir`, a broken symlink is omitted. `Entry::path` is `normalize(join(path, filename))`. The result order is directories, then files, ASCII case-folded as above. A path that is not a readable directory returns `ErrorKind::Io` with `errno` and `strerror`.

## Desktop entry

Linux only, in `app/linux_desktop.cc`. At startup the app reads `/proc/self/exe`. `desktopEntryText(exec)` returns empty when `exec` is empty or contains `"`, `\n`, or `\r`. Otherwise it returns exactly:

```text
[Desktop Entry]
Type=Application
Version=1.0
Name=mnrdonw
Terminal=false
Categories=Office;TextEditor;
MimeType=text/markdown;text/x-markdown;
StartupWMClass=io.nava.mnrdonw
Exec="<exec>" %f
```

with a trailing newline. The app writes that text to `~/.local/share/applications/io.nava.mnrdonw.desktop` by the same temp-and-rename rule, and only when the bytes differ. A write then runs `update-desktop-database` on `~/.local/share/applications` with `fork` and `execlp`, waits, and ignores failure. A missing `HOME`, a failed `readlink`, or a failed write skips registration. The editor still starts. The app does not become the default handler. Windows and Android register nothing.

## Libraries

- `archive_engine`: `documentsDirFromUserDirs`, `userDocuments`, `listDir`, and a Debug test executable. Commit from that repo with its `git_wrapper`.
- `vk_canvas`: `ScrollListStyle::hoverText`, default `col::text`. `drawScrollList` uses it for the hovered row's text when the row is not selected. An untouched style still draws hover text in `col::text`. Commit from that repo with its `git_wrapper`.
- `app_shell`: unchanged. The state directory is the existing `APP_SHELL_STATE_HOME` knob.
- Parent repo: record the new gitlinks after those two commits. `add_subdirectory` of `archive_engine/core` sets `ARC_BUILD_ARCHIVE` OFF and `ARC_BUILD_FS_TESTS` ON. The app links `arc_fs`. `editor_test` does not.

`drawScrollList` is the file list. No second list widget is added.

## Tests

`editor_test` gains asserts for `pageMargin`, `packFlow` (including the example above), `parseRecent`, `rememberPath`, `forgetPath`, `serializeRecent`, `saveRecent`/`loadRecent` in a temporary directory, a failed `loadRecent` that must not be followed by a write, and `desktopEntryText` including the empty result for a quote in the path. Those sources are `editor/margin.hh`, `editor/flow.cc`, `editor/recent.cc`, and `editor/desktop_entry.cc`. None of them includes a Vulkan, canvas, or shell header.

`arc_fs` tests cover `documentsDirFromUserDirs` (quoted `$HOME`, `${HOME}`, a missing key, a foreign `$`) and `listDir` on a temporary directory (a file, a subdirectory, a hidden name omitted, directories before files, a non-directory returning `Io`, no throw). The Debug app build produces those executables, and they exit 0.

The Wayland check is `./build/mnrdonw` with no arguments, then `./build/mnrdonw fixtures/sample.md`.

## Out of scope

Keyboard navigation of the home list. A preferences control for the margin. Taking the default `.md` handler. A Windows registry entry or an Android intent filter. A native file dialog. Creating `Documents` when it is absent.
