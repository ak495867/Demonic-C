# Demonic C Language Support for VS Code

Official Visual Studio Code extension for the **Demonic C (DMC)** systems programming language.

Provides complete language support, syntax highlighting, integrated Language Server Protocol (LSP) intelligence, runner commands, diagnostics, and developer snippets.

---

## ✨ Features

- **Language Server Intelligence (`dmc lsp`)**:
  - Real-time syntax and type diagnostics as you type.
  - Hover tooltips with documentation, type signatures, and keywords.
  - Go to Definition (`F12`) across functions and symbols.
- **Diagnostics on Save**:
  - Live problem matcher detecting errors, warnings, and source locations in the Problems pane.
- **One-Click Run & Build**:
  - Play and Build buttons directly in the editor title bar.
  - Interactive terminal execution for `dmc run`, `dmc build`, `dmc test`, and `dmc repl`.
- **Integrated Formatting & Linting**:
  - Format document on demand (`Shift+Alt+F`) or automatically on save via `dmc fmt`.
  - Fast compiler linting via `dmc lint`.
- **Interactive Explainer**:
  - Select any keyword, type, or concept and run `Demonic C: Explain Concept / Code` (`dmc explain`).
- **Project Wizard**:
  - Scaffolding new projects via `Demonic C: Create New Project` with template selection (`cli`, `polyglot`, `game`, `lib`).
- **Comprehensive Snippet Library**:
  - `fn`, `fnm`, `main`, `test`, `struct`, `enum`, `thread`, `channel`, `mutex`, `atomic`, `simd`, `arena`, `http_res`, `defer`, `try`, `match`, `c_code`, `extern`, `embed`.

---

## 🚀 Installation

### Option 1: Automatic Toolchain Install (Recommended)
Run the built-in compiler command in your terminal:
```bash
dmc lsp --install-vscode
```
This automatically deploys the extension into your user `~/.vscode/extensions/` directory.

### Option 2: Manual Copy
Copy the `editor/vscode` folder into your VS Code extensions folder:
- **Windows**: `%USERPROFILE%\.vscode\extensions\demonic-c-2.0.0`
- **Linux & macOS**: `~/.vscode/extensions/demonic-c-2.0.0`

Restart or reload VS Code (`Developer: Reload Window`), and open any `.dmc` file.

---

## ⌨️ Commands

| Command | Title | Action |
|---|---|---|
| `demonic-c.run` | Demonic C: Run Current File | Compiles and executes active file |
| `demonic-c.build` | Demonic C: Build Current File | Compiles to native binary |
| `demonic-c.test` | Demonic C: Run Tests | Runs test suite (`dmc test`) |
| `demonic-c.format` | Demonic C: Format Document | Formats file with `dmc fmt` |
| `demonic-c.lint` | Demonic C: Lint Document | Checks file with `dmc lint` |
| `demonic-c.explain` | Demonic C: Explain Concept / Code | Interactive language concept explainer |
| `demonic-c.repl` | Demonic C: Launch REPL | Interactive read-eval-print loop |
| `demonic-c.newProject` | Demonic C: Create New Project | Project generator wizard |
| `demonic-c.doctor` | Demonic C: Toolchain Doctor (Heal) | Diagnose and self-heal toolchain |
| `demonic-c.restartServer`| Demonic C: Restart Language Server | Restarts background LSP daemon |
| `demonic-c.showMenu` | Demonic C: Show Quick Menu | Opens command action picker |

---

## ⚙️ Configuration

Open your VS Code Settings (`Ctrl+,` or `Cmd+,`) and search for `demonic-c`:

- `demonic-c.compilerPath` *(default: `"dmc"`)*: Absolute path or command name for the `dmc` executable.
- `demonic-c.enableLsp` *(default: `true`)*: Enables background Language Server Protocol daemon.
- `demonic-c.lintOnSave` *(default: `true`)*: Runs syntax and type checks whenever a `.dmc` file is saved.
- `demonic-c.formatOnSave` *(default: `true`)*: Automatically formats files using `dmc fmt` on save.

---

## License

MIT © [ak495867](https://github.com/ak495867/Demonic-C)
