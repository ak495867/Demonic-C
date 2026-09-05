# USAGE.md - Demonic C Usage Guide

## Quick Start

### Building the Compiler
```bash
# Build everything
make

# Verify installation
./dmc-native --version
```

### Writing Your First Program
```dmc
// hello.dmc
fn main() -> int {
    println("Hello from Demonic C!");
    return 0;
}
```

```bash
# Compile to C
./dmc-native hello.dmc -o hello.c

# Compile to executable
clang hello.c -o hello -lws2_32
./hello
```

## Command Line Interface

```
dmc-native <source.dmc> [options]

Options:
  -o <file>       Write generated C to <file> (default: stdout)
  -I <dir>        Add import search directory (repeatable)
  --version       Print version and exit
  --help          Print this message and exit
```

### Examples
```bash
# Compile to stdout
./dmc-native program.dmc

# Compile to specific file
./dmc-native program.dmc -o output.c

# Add import directories
./dmc-native program.dmc -I ./includes -I /usr/local/dmc/include
```

## Language Reference

### Types
| Type | C Equivalent | Description |
|------|-------------|-------------|
| `int` | `long long` | 64-bit signed integer |
| `f32` | `float` | 32-bit floating point |
| `f64` | `double` | 64-bit floating point |
| `bool` | `bool` | Boolean (0/1) |
| `string` | `const char*` | Null-terminated string |
| `void` | `void` | No return value |
| `*T` | `T*` | Pointer to T |
| `[T;N]` | `T[N]` | Fixed-size array |

### Variables
```dmc
// Immutable binding
let x: int = 42;

// Mutable binding
var y: string = "initial";
y = "changed";

// Type inference
let z = 3.14;  // inferred as f64
```

### Control Flow
```dmc
// If-else
if (condition) {
    // ...
} else if (other) {
    // ...
} else {
    // ...
}

// While loop
while (condition) {
    // ...
}

// For loop (C-style)
for (let i: int = 0; i < 10; i = i + 1) {
    // ...
}

// Do-while
do {
    // ...
} while (condition);

// Until (inverted do-while)
until (condition) {
    // ...
}

// Switch
switch (value) {
    case 1: { /* ... */ }
    case 2: { /* ... */ }
    default: { /* ... */ }
}
```

### Functions
```dmc
fn add(a: int, b: int) -> int {
    return a + b;
}

fn print_msg(msg: string) -> void {
    println(msg);
}

// Function with no parameters
fn get_answer() -> int {
    return 42;
}
```

### Structures
```dmc
struct Point {
    x: int;
    y: int;
}

fn main() -> int {
    let p = Point { x: 10, y: 20 };
    println(p.x);
    return 0;
}
```

### Enums
```dmc
enum Status {
    OK,
    ERROR,
    PENDING
}

fn main() -> int {
    let status = Status::OK;
    // Use in switch
    return 0;
}
```

### Collections (Runtime Library)
```dmc
// Vectors
let v = vec_new();
vec_push(v, 10);
vec_push(v, 20);
let val = vec_get(v, 0);  // 10

// Queues
let q = queue_new();
queue_push(q, 100);
let item = queue_pop(q);  // 100

// Maps
let m = map_new();
map_set(m, "key", 42);
let val = map_get(m, "key");  // 42
```

### Memory Operations
```dmc
// Allocate
let h = mem_alloc(1024);

// Write/Read bytes
mem_write(h, 0, 65);  // 'A'
let byte = mem_read(h, 0);

// Free
mem_free(h);

// Arena allocation (high performance)
let arena = arena_new(65536);
let offset = arena_alloc(arena, 32);
arena_write(arena, offset, 0, 42);
let val = arena_read(arena, offset, 0);
arena_free(arena);
```

### File I/O
```dmc
let f = file_open("data.bin", "wb");
if (f < 0) { proc_exit(1); }

file_write(f, "data");
file_close(f);

// Read
let f2 = file_open("data.bin", "rb");
let content = file_read(f2);
file_close(f2);
```

### Network Programming
```dmc
let sock = tcp_connect("127.0.0.1", 8080);
if (sock < 0) { proc_exit(1); }

tcp_send(sock, "GET / HTTP/1.0\r\n\r\n");
let response = tcp_recv(sock, 8192);
tcp_close(sock);
```

### Inline Assembly
```dmc
// Single instruction
asm("nop");

// Assembly block
asm {
    "mov rax, 1"
    "ret"
}
```

### Process Control
```dmc
// Access command line args
let arg = arg_text(1);  // argv[1]

// Exit with code
proc_exit(0);

// System call
let result = syscall(__NR_write, 1, "hello\n", 6);
```

### Mathematical Functions
```dmc
let pi = math_pi();
let e = math_e();
let s = math_sin(pi / 2);
let c = math_cos(0);
let root = math_sqrt(16.0);
let abs = math_abs(-42);
```

### Text Functions
```dmc
let len = text_len("hello");
let cat = text_concat("hello", " world");
let sub = text_sub("hello", 1, 3);  // "ell"
let cmp = text_cmp("a", "b");  // -1
let num = text_from_int(42);  // "42"
let val = text_to_int("123");
```

## Modules/Imports

```dmc
// Import built-in module
import "math";
import "text";
import "mem";

// Import local module
import "my_module";
```

Module file: `my_module.dmc` or `src/my_module.dmc`

```dmc
// my_module.dmc
fn helper() -> int {
    return 42;
}
```

## Cross-Platform Notes

### Windows
- Link with `-lws2_32`
- Port I/O requires administrator privileges
- Uses Winsock2 for networking

### Linux
- Link with `-lm`
- Port I/O requires `iopl(3)` or CAP_SYS_RAWIO
- Uses standard POSIX APIs

## Advanced Usage

### Self-Hosting the Compiler
```bash
# Stage 1: Native compiler builds DMC emitter
./dmc-native tests/emit.dmc -o _stage_emit.c
clang -std=c11 -w _stage_emit.c -o _stage_emit.exe -lws2_32

# Stage 2: DMC emitter compiles self-hosting lexer
cd tests && ./_stage_emit.exe selflex.dmc selflex_stage_out.c
clang -std=c11 -w selflex_stage_out.c -o selflex_stage.exe -lws2_32
cd tests && ./selflex_stage.exe  # Exit code 37 = success
```

### Writing Compiler Tools in DMC
The test suite demonstrates writing:
- Lexers (`bootstrap_lexer.dmc`)
- Parsers (`bootstrap_parser.dmc`)
- Code emitters (`emit.dmc`)

These can be used as starting points for building:
- Domain-specific languages
- Code generators
- Static analyzers
- Transpilers

## Debugging Tips

1. **Check generated C code**: `./dmc-native program.dmc -o program.c`
2. **Enable C compiler warnings**: Use `-Wall -Wextra` when compiling generated C
3. **Runtime errors**: Check `dmc_last_error()` after failed operations
4. **Memory issues**: Use arena allocators for complex lifetime management

## Performance Considerations

- Use arena allocation for many small allocations
- Prefer stack allocation (`let`) over heap (`mem_alloc`)
- Use vectors/maps from runtime for dynamic collections
- Inline assembly for critical hot paths
- The generated C code is optimized by LLVM/Clang