# vimple

vimple is a modal simple C++ and Python code editor with a **gruvbox-dark** theme that runs on your terminal inspired on Neovim compatible with rsfmake package manager, written as a single C++ file

The idea: inside a file, editing is modal (`hjkl`, `i`, `v`, `dd`, `yy`, `p`, `u`...), but everything "program-level" (opening a project, creating files, running, configuring) is done through **menus, dialogs, buttons and the mouse** — no `:` commands to memorize.

## Features

- **Modal editing**: Normal, Insert, Visual and Visual Line modes, with counts (`3dd`, `5j`), operators (`d`, `y`, `c`, `>`, `<`) and motions (`w b e`, `0 $ ^`, `gg G`, `{ }`, `%`).
- **Modern Insert mode**: mouse selection, `Ctrl-C/X/V`, `Ctrl-Z/Y`, auto-closing of `() [] {} "" ''`, auto-indent.
- **LSP (clangd)**: autocomplete, go to definition, hover info and diagnostics (errors/warnings underlined in the line).
- **Split screen**: up to 4 panes, side by side or stacked.
- **Built-in terminal** (`F4`): simplified VT100/xterm emulator with scrollback, using a pty (Linux/macOS) or ConPTY (Windows).
- **Integrated Run**: a ▶ Run button with output in a panel; click an error line (`file:line:col`) to jump to the location.
- **File explorer** with create / rename / delete.
- **Tabs**, search, go to line, undo/redo.
- **Per-project session**: remembers open files, cursor, splits and panels; reopens the last project automatically.
- **Syntax highlighting** for C/C++, plus `#include` autocomplete and common STL / popular library symbols.
- Preserves **CRLF** and **UTF-8 BOM** when saving.
- **Cross-platform**: Linux/macOS (ncurses) and Windows (PDCurses).

## Building

vimple is built with [rsfmake](https://github.com/rsfis/rsfmake):

```bash
rsfmake run -r
```

This builds the project in release mode and runs it.

### Requirements

- A C++17 compiler (g++, mingw or mscv)

## Usage

```bash
./vimple                  # reopens the last project (or asks you to pick a folder)
./vimple path/to/project
```

On Linux the terminal must support **256 colors** (`TERM=xterm-256color`). For true color, set `RSFVIM_TRUECOLOR=1`.

### Optional dependencies

- **clangd** — for autocomplete, definition, hover and diagnostics
  (Linux: your package manager, e.g. `sudo apt install clangd`; Windows: `winget install LLVM.LLVM`).
  For best results, keep a `compile_commands.json` in the project root or in `build/`, `out/`, `target/`, `cmake-build-debug/` or `cmake-build-release/`.
- **wl-clipboard**, **xclip**, **xsel** or **pbpaste** — system clipboard integration (without any of them, OSC 52 is used for copying).

## Keybindings

### Global

| Key | Action |
|---|---|
| `F10` | Open menus |
| `F1` | Mouse and color diagnostics |
| `F2` | Next pane (split) |
| `F3` | Split screen |
| `F4` | Show/hide terminal |
| `F5` / `F6` | Run / Run (release) |
| `F7` | Stop running process |
| `F8` | Output panel |
| `F9` | Configure run |
| `F11` / `F12` | Info under cursor / Go to definition |
| `Ctrl-S` | Save |
| `Ctrl-O` | Open project |
| `Ctrl-Q` | Quit |
| `Ctrl-T` | Show/hide explorer |
| `Ctrl-E` | Toggle focus explorer ↔ editor |
| `Ctrl-F` | Find |
| `Ctrl-G` | Go to line |
| `Ctrl-N` / `Ctrl-W` | New file / close file (outside Insert mode) |

### Normal mode

| Key | Action |
|---|---|
| `h j k l`, arrows | Move |
| `w b e` | Next word / previous word / end of word |
| `0 ^ $` | Line start / first non-blank / line end |
| `gg` / `G` / `NG` | Top / bottom / line N |
| `{ }` / `%` | Paragraph / matching bracket |
| `Ctrl-D` / `Ctrl-U` | Half page down / up |
| `i a I A o O` | Enter Insert mode |
| `x`, `D`, `C`, `J` | Delete char / delete to end / change to end / join lines |
| `dd dw d$ d0` | Delete line / word / to end / to start |
| `yy yw y$` | Yank (copy) |
| `cc cw c$` | Change |
| `p` / `P` | Paste after / before |
| `u` / `Ctrl-R` | Undo / redo |
| `>>` / `<<` | Indent / unindent |
| `r<c>` | Replace character |
| `v` / `V` | Visual / Visual Line mode |
| `n` / `N` | Next / previous search match |
| `gd` / `gb` | Go to definition / go back |
| `K` | Info under cursor |
| `gt` / `gT` | Next / previous tab |
| `zz` | Center cursor on screen |

### Insert mode

| Key | Action |
|---|---|
| `Esc` | Back to Normal mode |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | Copy / cut / paste |
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo |
| `Ctrl-N` / `Ctrl-P` | Open / navigate autocomplete |
| `Tab` | Accept suggestion (or insert 4 spaces) |
| Mouse drag | Select text |

### Visual mode

`y` / `Ctrl-C` copy · `d` / `x` delete · `c` change · `>` `<` indent · `Esc` cancel.

### Explorer

`Enter`/`l` open · `h` collapse · `a` new file · `A` new folder · `r` rename · `d` delete · `R` refresh · `Esc` back to editor.

## Project configuration

Each project can have a `.vimple.conf` file in its root (also editable via **Project > Configure run**, `F9`):

```ini
run=rsfmake run
release=rsfmake run -r
terminal=0
shell=
lsp=clangd --background-index --header-insertion=never
lspon=1
```

| Key | Description |
|---|---|
| `run` | Command executed by the ▶ Run button (`F5`) |
| `release` | Release-mode command (`F6`) |
| `terminal` | `1` to run in a full terminal (interactive programs) |
| `shell` | Shell for the built-in terminal (empty = `$SHELL` / `%COMSPEC%`) |
| `lsp` | Language server command |
| `lspon` | `1` enables / `0` disables the LSP |

The default commands use the **rsfmake** package manager, but you can use any command (`make`, `cmake --build build && ./build/app`, `cargo run`...).

Sessions are stored in `~/.config/vimple/` (Linux/macOS, honoring `XDG_CONFIG_HOME`) or `%APPDATA%\vimple\` (Windows).

## License
GPL-3.0 License
