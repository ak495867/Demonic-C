# Demonic C — Comprehensive Usage Guide

This guide covers installation, the DMC command-line interface, core language syntax, modern language features, standard runtime library functions, and compilation instructions. For a high-level project overview, see [README.md](README.md).

---

## ⚡ Quick Installation

Install Demonic C directly from your terminal:

### Linux & macOS (bash / zsh)
```bash
curl -fsSL https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash
```

### Windows (PowerShell)
```powershell
irm https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.ps1 | iex
```

The installer installs to `~/.dmc/bin` and adds it to your `PATH`. Verify immediately with:
```bash
dmc --version
```

---

## Quick Start

### 1. Writing Your First Program

Create a file named `hello.dmc`:
```dmc
fn main() -> int {
    println("Hello from Demonic C!");
    return 0;
}
```

### 2. Running Directly (One Step & Cached)

Run any `.dmc` file immediately:
```bash
dmc hello.dmc
```
Demonic C caches compiled native binaries by source content hash in `~/.dmc/cache`. Subsequent executions of unchanged files run instantly in `< 5ms`.
Bypass cache with `--no-cache`:
```bash
dmc hello.dmc --no-cache
```

Pass runtime arguments directly:
```bash
dmc hello.dmc arg1 arg2
```

Run with AddressSanitizer & UndefinedBehaviorSanitizer:
```bash
dmc hello.dmc --sanitize
```

### 3. Hot-Reloading Watch Mode

Automatically rerun your code on every file save:
```bash
dmc watch hello.dmc
```

### 4. Interactive REPL

Experiment with expressions and statements directly from the command line:
```bash
dmc repl
```
Inside the REPL, type `:help` for commands, expressions to evaluate (`10 + 32`), statements (`let x = 42; println(x);`), and `:exit` to quit.

### 5. VS Code & Cursor Extension Setup

Install syntax highlighting and language configuration directly into your editor:
```bash
dmc lsp --install-vscode
```

### 6. Building Directly to Executable Binary

Use `dmc build` to transpile and link directly into a permanent binary:
```bash
dmc build hello.dmc -o hello
./hello
```

### 7. Compiling to C Code Only

If you wish to inspect the generated C99/C11 code:
```bash
dmc hello.dmc -o hello.c
```

---

## Command-Line Interface (CLI)

```
usage: dmc <source.dmc> [args...]       compile and run Demonic C program
       dmc run <source.dmc> [args...]   compile and run Demonic C program
       dmc build <source.dmc> [options] compile to executable binary
       dmc <source.dmc> -o <file.c>     transpile to C code

options:
  -o <file>       write generated C to <file>
  --emit-c        transpile to C on stdout
  --sanitize      compile with AddressSanitizer & UndefinedBehaviorSanitizer
  --no-cache      bypass compilation cache
  -I <dir>        add import search directory (repeatable)
  --lsp           start Language Server Protocol daemon
  --version       print version and exit
  --help          print this message and exit

commands:
  run <src>       compile and run Demonic C source directly (or 'dmc <src>')
  watch <src>     watch source file and rerun on save
  repl            interactive Demonic C read-eval-print loop
  build [src]     compile Demonic C source directly to executable binary
  test [dir]      run test suite
  lsp             run LSP daemon (--install-vscode to setup editor)
  pkg             package manager commands
  new <name>      create new project
  init            initialize project in current directory
  fmt [--check]   format or check .dmc files
  lint [--fix]    lint .dmc files for issues
  doc             generate documentation
```

### Common CLI Invocations

```bash
# Instant run with caching
dmc src/main.dmc

# Watch mode for live dev
dmc watch src/main.dmc

# Build directly to binary (automatic GCC/Clang invocation)
dmc build src/main.dmc -o bin/app

# Run all unit tests in test-files/
dmc test test-files

# Start LSP server for IDEs
dmc lsp

# Install VS Code extension
dmc lsp --install-vscode

# Format a source file
dmc fmt src/main.dmc

# Check formatting without modifying
dmc fmt --check src/main.dmc

# Lint source file
dmc lint src/main.dmc
```

---

## Modern Language Features

### 1. LIFO Scope Cleanup (`defer`)
Execute statements automatically when leaving a block or function:

```dmc
fn process(filename: string) {
    let f = file_open(filename, "r");
    if (f < 0) { return; }
    defer file_close(f);

    defer println("Done reading file");
    let contents = file_read(f);
    println(contents);
}
```

### 2. Tagged Unions & Enums with Payloads
Define sum types and variant payloads:

```dmc
enum Result {
    Ok(string),
    Err(int)
}

fn calculate() -> Result {
    return Result_Ok("Calculation succeeded!");
}

fn main() {
    let res = calculate();
    if (res.tag == Result_Ok) {
        println(res.data.Ok);
    }
}
```

### 3. String Slicing
Sub-string slicing with range syntax `s[start..end]`:

```dmc
fn main() {
    let s: string = "Demonic C Systems Programming";
    let part = s[0..9]; // "Demonic C"
    println(part);
}
```

### 4. Built-in Testing Attribute (`#[test]`)
Annotate test functions with `#[test]` returning `int` (0 for pass, non-zero for failure):

```dmc
#[test]
fn test_addition() -> int {
    if (10 + 20 == 30) { return 0; }
    return 1;
}

#[test]
fn test_string_slice() -> int {
    let s: string = "Hello World";
    let part = s[0..5];
    if (text_cmp(part, "Hello") == 0) { return 0; }
    return 1;
}
```

Run tests with `dmc test test-files`.

### 5. Compile-Time Null Safety (`?*T`)
Pointers marked with `?*T` cannot be dereferenced directly without verifying they are not null:

```dmc
struct Entity {
    id: int;
}

fn inspect(e: ?*Entity) {
    // e.id = 1; // Compile error: cannot dereference nullable pointer 'e' without null check
    if (e != null) {
        println(e.id); // Valid: narrowed to '*Entity' in this scope
    }
}
```

### 6. Error Propagation (`try`)
Systems functions returning negative integer error codes can be handled ergonomically with `try`:

```dmc
fn init_network() -> int {
    let sock = try tcp_connect("127.0.0.1", 8080);
    try tcp_send(sock, "PING");
    return 0;
}
```

### 7. Monomorphized Generics (`Vector<T>`)
Type-safe generic vector with specialized C representation:

```dmc
let nums: Vector<int> = Vector<int>::new();
nums.push(10);
nums.push(20);
println(nums[0]); // 10
println(nums.len()); // 2
nums.free();
```

### 8. Direct C Headers & Asset Embedding
Include raw C headers directly without generating warnings, and embed binary assets at compile time:

```dmc
import "stdio.h";
import "math.h";

let data = #embed("assets/icon.png");
let sz = #embed_size("assets/icon.png");
let msg = #embed_str("assets/notice.txt");
let magic = comptime { 1024 * 1024 };

fn main() {
    puts(msg);
}
```

### 9. First-Class Arenas
Fast bump-allocation with instant reset:

```dmc
let a: arena = arena_new(65536);
let offset = a.alloc_bytes(128);
a.write(offset, 0, 99);
println(a.read(offset, 0)); // 99
a.reset();
a.free();
```

### 10. Git-Based Package Manager (`dmc pkg add`)
Add GitHub repositories as project dependencies:

```bash
dmc pkg add https://github.com/user/dmc-json.git
```

---

## Language Reference

### Data Types

| Type | C Equivalent | Description |
|---|---|---|
| `int` | `long long` | 64-bit signed integer |
| `i32` | `int` | 32-bit signed integer |
| `i64` | `long long` | 64-bit signed integer |
| `u8` | `unsigned char` | 8-bit unsigned integer |
| `u32` | `unsigned int` | 32-bit unsigned integer |
| `u64` | `unsigned long long` | 64-bit unsigned integer |
| `float` / `f64` | `double` | 64-bit floating point |
| `f32` | `float` | 32-bit floating point |
| `bool` | `int` | Boolean (0 or 1) |
| `string` | `const char*` | Null-terminated UTF-8 string |
| `void` | `void` | Empty return type |
| `*T` | `T*` | Direct raw pointer (non-nullable) |
| `?*T` | `T*` | Nullable pointer (requires null check before dereference) |
| `Vector<T>` | `Vector_T*` | Monomorphized type-safe dynamic array |
| `arena` | `long long` | Bump-allocator handle for fast bulk allocation |

### Variable Declarations

```dmc
// Immutable binding
let x: int = 42;

// Mutable binding
var count: int = 0;
count = count + 1;

// Type inference
let name = "Demonic C";
let pi = 3.14159;
```

### Control Flow

```dmc
// If - Else If - Else
if (x > 100) {
    println("Large");
} else if (x > 50) {
    println("Medium");
} else {
    println("Small");
}

// While loop
var i: int = 0;
while (i < 10) {
    i = i + 1;
}

// For loop
for (let j: int = 0; j < 5; j = j + 1) {
    println("iteration");
}
```

### Structs & Zero-Cost Methods

Demonic C supports C-compatible structs with automatic zero-cost method dispatch:

```dmc
struct Vector2D {
    x: int;
    y: int;
}

fn Vector2D_length_sq(v: Vector2D) -> int {
    return v.x * v.x + v.y * v.y;
}

fn main() -> int {
    let v = Vector2D { x: 10, y: 20 };
    println(v.x);
    
    // Calls Vector2D_length_sq(v) with zero runtime cost
    let sq = v.length_sq();
    println(sq);
    return 0;
}
```

---

## Standard Runtime Library

### Dynamic Collections (Function & Method Syntax)

Collections can be operated on using either procedural functions or object method syntax:

```dmc
// Dynamic Vector
let v = vec_new();
v.push(10);
v.push(20);
let len = v.len();       // or vec_len(v)
let val = v.get(0);       // 10
v.free();

// Queue (FIFO)
let q = queue_new();
q.push(42);
let head = q.pop();      // 42
q.free();

// Hash Map
let m = map_new();
m.set(1, 100);
let found = m.has(1);    // 1
let value = m.get(1);    // 100
m.free();
```

### High-Level Standard Library Modules

Demonic C provides a rich suite of built-in standard modules in `stdlib/`:

| Module | Description | Key Functions / Structs |
|---|---|---|
| `strings` | High-level string operations | `str_trim`, `str_starts_with`, `str_ends_with`, `str_contains`, `str_index_of`, `str_equals` |
| `path` | Filesystem path utilities | `path_join`, `path_basename`, `path_dirname`, `path_extension`, `path_has_extension` |
| `time` | Clock and duration tools | `time_sec`, `time_millis`, `time_sleep`, `time_format` |
| `random` | Pseudorandom generation | `random_seed`, `random_range`, `random_bool` |
| `buffer` | Dynamic growable byte buffer | `Buffer`, `buffer_new`, `buffer_append_byte`, `buffer_free` |
| `log` | Structured console logging | `log_info`, `log_warn`, `log_error`, `log_debug` |
| `testing` | Unit test assertions | `assert_eq`, `assert_str_eq`, `assert_true`, `assert_false` |
| `fs` | Filesystem I/O helpers | `fs_read_to_string`, `fs_write_string`, `fs_append_string`, `fs_file_exists` |
| `os` | Operating system metadata | `os_system_platform`, `os_cpu_arch`, `os_current_dir` |
| `crypto` | Cryptographic hashing | `hash_sha256`, `to_base64`, `from_base64` |

```dmc
import "strings";
import "path";
import "time";
import "random";
import "log";
import "fs";
import "os";
import "crypto";

fn main() -> int {
    log_info("Starting Demonic C application...");

    // 1. Strings and Paths
    let p = path_join("logs", "app.log");
    if (str_ends_with(p, ".log") == 1) {
        log_info("Log file target verified.");
    }

    // 2. High-Resolution Time & Random
    random_seed(time_sec());
    let roll = random_range(1, 100);
    log_info(text_from_int(roll));

    // 3. Filesystem & Crypto
    fs_write("sample.txt", "Demonic Systems");
    if (fs_file_exists("sample.txt") == 1) {
        let content = fs_read_to_string("sample.txt");
        let hash = hash_sha256(content);
        println(hash);
        fs_remove_file("sample.txt");
    }

    return 0;
}
    println(hash);
    return 0;
}
```

### Memory Management & Arenas

```dmc
// Raw heap allocation
let h = mem_alloc(1024);
mem_write(h, 0, 65); // 'A'
let b = mem_read(h, 0);
mem_free(h);

// High-speed bulk arena allocator
let arena = arena_new(65536);
let offset = arena_alloc(arena, 32);
arena_write(arena, offset, 0, 99);
let val = arena_read(arena, offset, 0);
arena_free(arena);
```

### Networking & TCP Sockets

```dmc
let sock = tcp_connect("127.0.0.1", 8080);
if (sock >= 0) {
    tcp_send(sock, "GET / HTTP/1.0\r\n\r\n");
    let resp = tcp_recv(sock, 4096);
    tcp_close(sock);
}
```

### JSON & Regular Expressions

```dmc
// JSON Parsing
let json = "{\"user\":\"Dante\",\"level\":99}";
let user = json_get(json_parse(json), "user"); // "Dante"

// Regular Expressions
let r = regex_new("^[A-Z][a-z]+$");
let matched = regex_match(r, "Demonic"); // 1
regex_free(r);
```

---

## Self-Hosting Bootstrap Chain

The self-hosting capability can be verified step-by-step:

```bash
# 1. Compile emit.dmc into C
./bin/dmc test-files/emit.dmc -o test-files/_stage_emit.c

# 2. Compile stage emitter binary
gcc -std=c11 -w test-files/_stage_emit.c -o test-files/_stage_emit -lws2_32

# 3. Emit stage lexer with stage emitter
cd test-files
./_stage_emit selflex.dmc selflex_stage_out.c

# 4. Compile and execute selflex
gcc -std=c11 -w selflex_stage_out.c -o selflex_stage -lws2_32
./selflex_stage # Output: 1317 tokens, exit code 37
```
