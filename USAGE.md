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

### 2. Running Directly (One Step)

Run any `.dmc` file immediately:
```bash
dmc hello.dmc
```
Or pass runtime arguments directly:
```bash
dmc hello.dmc arg1 arg2
```

### 3. Building Directly to Executable Binary

Use `dmc build` to transpile and link directly into a permanent binary:
```bash
dmc build hello.dmc -o hello
./hello
```

### 4. Compiling to C Code Only

If you wish to inspect the generated C99/C11 code:
```bash
dmc hello.dmc -o hello.c
```

---

## Command-Line Interface (CLI)

```
usage: dmc <source.dmc> [options]
compile Demonic C source to C.

options:
  -o <file>       write generated C to <file> (default: stdout)
  -I <dir>        add import search directory (repeatable)
  --lsp           start Language Server Protocol daemon
  --version       print version and exit
  --help          print this message and exit

commands:
  build [src]     compile Demonic C source directly to executable binary
  test [dir]      discover and execute all #[test] functions in directory
  lsp             run Demonic C Language Server Protocol (LSP) daemon
  fmt [--check]   format or check .dmc files
  lint [--fix]    lint .dmc files for code style and syntax issues
  new <name>      create new Demonic C project
  init            initialize project configuration in current directory
  pkg             package manager commands (install, search, list, publish)
  doc             generate HTML/Markdown documentation
```

### Common CLI Invocations

```bash
# Build directly to binary (automatic GCC/Clang invocation)
dmc build src/main.dmc -o bin/app

# Run all unit tests in test-files/
dmc test test-files

# Start LSP server for IDEs
dmc lsp

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
| `*T` | `T*` | Direct raw pointer |

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

### Structs

```dmc
struct Vector2D {
    x: int,
    y: int
}

fn main() -> int {
    let v = Vector2D { x: 10, y: 20 };
    println(v.x);
    return 0;
}
```

---

## Standard Runtime Library

### Dynamic Collections

```dmc
// Dynamic Vector
let v = vec_new();
vec_push(v, 10);
vec_push(v, 20);
let len = vec_len(v);
let val = vec_get(v, 0); // 10
vec_free(v);

// Queue (FIFO)
let q = queue_new();
queue_push(q, 42);
let head = queue_pop(q); // 42
queue_free(q);

// Hash Map
let m = map_new();
map_set(m, 1, 100);
let found = map_has(m, 1); // 1
let value = map_get(m, 1); // 100
map_free(m);
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
