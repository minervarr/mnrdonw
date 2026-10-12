# mnrdonw

A one-document live-source markdown editor. The file is the markdown buffer. The block that contains the caret is drawn as source. Every other block is drawn as styled prose. Save writes the buffer bytes.

## Where code lives

- `editor/` is pure document logic. It does not include Vulkan, the canvas, or the shell.
- `app/` is the window. It is a `FrameInputView`. It draws with the canvas and loads and saves files.
- `libs/firstparty/framework/vk_canvas` and `app_shell` are consumed as they are, except the one `hoverText` field named in `docs/superpowers/specs/2026-10-11-home-menu-design.md`. Directory listing and the Documents lookup from that spec go in `libs/firstparty/archive_engine`. Read each tree's `CLAUDE.md` before touching it. `app_shell` stays unchanged.

## Checks

Build and test with the commands in `README.md`. `editor_test` is the gate for document behavior. The Wayland window is checked by opening `fixtures/sample.md`.

## Tests

Tests approve the code. Production code stays behind them.

For a task, write its asserts in `tests/editor_test.cc` first. Run `editor_test` and watch it fail because that behavior is missing: a missing header, or an assert on the behavior itself. Then write only the code that makes those asserts pass. `./build/editor_test` must exit 0 before the next task starts. One task at a time. Do not add behavior the current tests do not name.

## Plan

Document editing is specified in `docs/superpowers/plans/2026-10-11-live-source-markdown-editor.md`. Do not reopen that design.

The home screen, the page margin, and the Linux desktop entry are specified in `docs/superpowers/specs/2026-10-11-home-menu-design.md`. That spec supersedes the centered 70-character column and the empty-launch writing buffer. It has no task list yet.

## Commits

From this repo, commit with `./git_wrapper`, never plain `git commit` or `git push`. The libraries are separate repositories with their own wrappers. Do not commit them from here unless the task changes them.
