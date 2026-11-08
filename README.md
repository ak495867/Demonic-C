# Demonic C

<p align="center">
  <img src="assets/logo.png" alt="Demonic-C Logo" width="200">
</p>

<p align="center">
  <strong>A self-hosting systems programming language that compiles to C with native performance, modern ergonomics, and direct hardware control.</strong>
</p>

[![GitHub last commit](https://img.shields.io/github/last-commit/ak495867/Demonic-C?style=for-the-badge)](https://github.com/ak495867/Demonic-C/commits/main)
[![C++](https://img.shields.io/badge/C%2B%2B-Compiler-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![C](https://img.shields.io/badge/C-Runtime-A8B9CC?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Self-Hosting](https://img.shields.io/badge/Compiler-Self--Hosting-8A2BE2?style=for-the-badge)](https://github.com/ak495867/Demonic-C)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey?style=for-the-badge)](https://github.com/ak495867/Demonic-C)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE.md)

> **Naming note:** the `.dmc` extension is a nod to *Devil May Cry* — high speed, absolute precision, and serious style.

---

## ⚡ Quick Install

Install Demonic C directly from your terminal in seconds:

### Linux & macOS (bash / zsh)
```bash
curl -fsSL https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash
```
*Or via wget:*
```bash
wget -qO- https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash
```

### Windows (PowerShell)
```powershell
irm https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.ps1 | iex
```

The installer detects your operating system and CPU architecture, downloads the latest pre-compiled release from GitHub, extracts it to `~/.dmc`, and configures your `PATH`.

---

## Overview

Demonic C (DMC) is a systems programming language designed for low-level control without sacrificing modern developer ergonomics. It compiles to clean, portable C99/C11, which is then built by any standard C compiler (GCC, Clang, MSVC) — giving DMC programs the raw execution speed and predictable memory layout of hand-written C, coupled with high-level language safety and expressiveness.

DMC is **self-hosting**: its own compiler is written in DMC and compiles itself into C, producing a complete multi-stage bootstrap chain.

### Core Capabilities

- **Modern Ergonomics**: LIFO `defer` cleanup, tagged unions / payload enums, string slicing (`s[0..5]`).
- **Zero-Overhead Memory Control**: Direct memory pointers, byte-level read/write primitives, and arena allocators.
- **Hardware & OS Access**: Low-level port I/O (`port_in_b`, `port_out_b`), inline assembly, process control, and system calls.
- **Networking & I/O**: High-speed TCP sockets, HTTP client, and buffered file I/O.
- **Rich Standard Library**: JSON parsing/serialization, regular expressions, SQLite database interface, date/time utilities, and dynamic collections (vectors, queues, hash maps).
- **Batteries-Included Toolchain**: Direct binary compiler (`dmc build`), native unit test runner (`dmc test`), Language Server Protocol daemon (`dmc lsp`), code formatter (`dmc fmt`), linter (`dmc lint`), and package manager (`dmc pkg`).

---

## Project Layout

| Directory / File | Description |
|---|---|
| `compiler/` | C++20 compiler implementation (`dmc.cpp`) with AST, lexer, codegen, and CLI engine |
| `runtime/` | Modular C runtime library (`runtime/dmc_runtime.c`, `runtime/core/`, `mem/`, `fs/`, `net/`) |
| `stdlib/` | DMC standard-library modules (`datetime`, `db`, `http`, `json`, `regex`) |
| `test-files/` | Unit tests (`#[test]`), feature tests (`modern_features.dmc`), and bootstrap chain (`emit.dmc`, `selflex.dmc`) |
| `build/` | Makefile and shell build scripts |
| `bin/` | Pre-compiled compiler executables and runtime static libraries |
| `.github/workflows/` | Multi-platform CI matrix and GitHub Releases automation |
| `install.sh` | Cross-platform Unix one-line installer script |
| `install.ps1` | Windows PowerShell one-line installer script |
| `docs/` | Architecture specs and language documentation |
| `packages/` | Local package registry and package metadata |
| `editor/` | VS Code extension foundation and language syntax definitions |
| `playground/` | Interactive web playground |
| `examples/` | Standalone example programs |

---

## Key Features & Code Examples

### 1. Modern Scope Cleanup (`defer`)
Execute cleanup statements automatically when exiting functions or blocks in LIFO order:

```dmc
fn process_file(path: string) {
    let f = file_open(path, "r");
    if (f < 0) { return; }
    defer file_close(f);

    // Any early return or exit safely closes the file
    let content = file_read(f);
    println(content);
}
```

### 2. Tagged Unions & Enums with Payloads
Express algebraic data types and pattern-matched results with type safety:

```dmc
enum Result {
    Ok(string),
    Err(int)
}

fn fetch_data() -> Result {
    return Result_Ok("Payload received!");
}

fn main() {
    let res = fetch_data();
    if (res.tag == Result_Ok) {
        println(res.data.Ok);
    }
}
```

### 3. String Slicing
Direct, memory-safe sub-slice expressions:

```dmc
fn main() {
    let text: string = "Demonic C Systems Language";
    let sub = text[0..9]; // "Demonic C"
    println(sub);
}
```

### 4. Built-in Testing Engine
Write unit tests directly in your `.dmc` files using the `#[test]` attribute:

```dmc
#[test]
fn test_addition() -> int {
    if (2 + 2 == 4) { return 0; }
    return 1;
}
```

Run the entire test suite across your codebase:
```bash
dmc test test-files
```

---

## CLI Toolchain Usage

The unified `dmc` executable provides a complete development toolkit:

```bash
# Check version
dmc --version

# Build a DMC program directly to a native executable
dmc build app.dmc -o bin/app

# Run all #[test] functions in a folder
dmc test test-files

# Start Language Server Protocol daemon for VS Code / Neovim
dmc lsp

# Format DMC source code
dmc fmt src/main.dmc

# Lint DMC source code
dmc lint src/main.dmc

# Create a new project scaffold
dmc new my-project

# Package management
dmc pkg search json
dmc pkg install json
```

For advanced options, see [`USAGE.md`](USAGE.md).

---

## Self-Hosting & Bootstrap Verification

DMC's self-hosting compiler can compile its own bootstrap stage:

```bash
# 1. Compile the DMC stage emitter to C using the DMC compiler
./bin/dmc test-files/emit.dmc -o test-files/_stage_emit.c

# 2. Compile the generated C to an executable stage emitter
gcc -std=c11 -w test-files/_stage_emit.c -o test-files/_stage_emit -lws2_32

# 3. Use the stage emitter to emit the selflex stage lexer
cd test-files
./_stage_emit selflex.dmc selflex_stage_out.c

# 4. Compile the selflex stage and run it
gcc -std=c11 -w selflex_stage_out.c -o selflex_stage -lws2_32
./selflex_stage   # Correctly validates 1317 tokens and exits with code 37
```

---

## Contributing

Contributions are warmly welcomed! Please review [CONTRIBUTING.md](CONTRIBUTING.md) for details on code style, testing requirements, and pull request submissions.

## License

Demonic C is distributed under the open-source MIT License. See [LICENSE.md](LICENSE.md) for full license text.
