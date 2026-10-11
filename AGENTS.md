# mnrdonw

A one-document live-source markdown editor. The file is the markdown buffer. The block that contains the caret is drawn as source. Every other block is drawn as styled prose. Save writes the buffer bytes.

## Where code lives

- `editor/` is pure document logic. It does not include Vulkan, the canvas, or the shell.
- `app/` is the window. It is a `FrameInputView`. It draws with the canvas and loads and saves files.
- `libs/firstparty/framework/vk_canvas` and `app_shell` are consumed as they are. Editor behavior goes in this repo. Read those trees' `CLAUDE.md` before touching them.

## Checks

Build and test with the commands in `README.md`. `editor_test` is the gate for document behavior. The Wayland window is checked by opening `fixtures/sample.md`.

## Plan

No editor code is in the tree yet. The spec and the task list are `docs/superpowers/plans/2026-10-11-live-source-markdown-editor.md`. Implement it task by task. Do not reopen the design.

## Commits

From this repo, commit with `./git_wrapper`, never plain `git commit` or `git push`. The libraries are separate repositories with their own wrappers. Do not commit them from here unless the task changes them.
