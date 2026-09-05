# Demonic C Programming Language

> **Fun fact:** `.dmc` is a nod to *Devil May Cry*, because the theme needed a little style.

A powerful, self-hosting systems programming language that compiles to C with native performance and direct hardware access.

## Overview

Demonic C is a complete, production-ready systems programming language featuring:
- Full self-hosting capability (compiler written in itself)
- Direct memory management primitives
- Low-level hardware access (ports, assembly)
- Network programming (TCP sockets)
- File I/O operations
- Collections (vectors, queues, maps)
- Mathematical functions
- Text processing utilities
- Process control

## Key Features

### Memory Management
- `dmc_mem_alloc()`, `dmc_mem_free()`
- Memory read/write operations
- Arena allocation for high-performance scenarios

### File Operations
- Open, read, write, close files
- Binary and text mode support

### Network Programming
- TCP client connections
- Send/receive data over sockets
- Cross-platform (Windows/Linux)

### Data Structures
- Dynamic arrays (vectors)
- FIFO queues
- Hash maps
- Memory-mapped files

### Low-Level Access
- Inline assembly support
- Port I/O operations (x86)
- Interrupt handling
- System calls

### Mathematical & Text Functions
- Trigonometric, logarithmic, exponential functions
- String manipulation utilities
- Type conversion functions

### Process & System Control
- Argument access (`arg_text`, `proc_arg_count`)
- Process exit (`proc_exit`)
- System call interface
- Interrupt handling

## Self-Hosting Capability

The Demonic C compiler is capable of:
1. Compiling itself from DMC source to C
2. The resulting C code compiles to an executable
3. That executable can compile other DMC programs
4. This creates a complete bootstrap chain proving language completeness

## Use Cases

### Systems Programming
- Operating system components
- Device drivers
- Embedded firmware
- Bootloaders

### Performance-Critical Applications
- High-frequency trading systems
- Real-time signal processing
- Game engines
- Scientific computing

### Security & Reverse Engineering
- Exploit development tools
- Malware analysis
- Network sniffers
- Cryptographic implementations

### Compiler Construction
- Language translators
- DSL implementations
- Code generators
- Static analysis tools

## Building & Usage

### Prerequisites
- Clang/LLVM compiler (for building the compiler itself)
- Windows Subsystem for Linux or native Windows build tools
- Winsock2 library (Windows)

### Building the Compiler
```bash
# Build the DMC compiler and runtime library
make

# Run the test suite
make test

# Run the runtime smoke test
make smoke
```

### Compiling DMC Programs
```bash
# Compile a DMC program to C
./dmc-native program.dmc -o program.c

# Compile the generated C to an executable
clang program.c -o program -lws2_32  # Windows
# or
clang program.c -o program -lm       # Linux
```

### Self-Hosting Demonstration
```bash
# The compiler can compile itself
./dmc-native tests/emit.dmc -o _emit.c
clang _emit.c -o _emit.exe -lws2_32
._emit.exe tests/selflex.dmc selflex_stage_out.c
clang selflex_stage_out.c -o selflex_stage.exe -lws2_32
._selflex_stage.exe  # Should exit with code 37
```

## Language Syntax Overview

### Basic Structure
```dmc
fn main() -> int {
    // Variables
    let x: int = 42;
    var y: string = "hello";
    
    // Control flow
    if (x > 0) {
        println(y);
    }
    
    // Loops
    for (let i: int = 0; i < 10; i = i + 1) {
        // ...
    }
    
    // Functions
    return helper(x);
}

fn helper(value: int) -> int {
    return value * 2;
}
```

### Memory Operations
```dmc
// Allocate memory
let h = mem_alloc(1024);
if (h < 0) { proc_exit(1); }

// Write and read
mem_write(h, 0, 65);
let value = mem_read(h, 0);

// Free when done
mem_free(h);
```

### File Operations
```dmc
let f = file_open("data.txt", "w");
if (f < 0) { proc_exit(1); }
file_write(f, "Hello, Demonic C!");
file_close(f);
```

### Network Operations
```dmc
let sock = tcp_connect("example.com", 80);
if (sock < 0) { proc_exit(1); }
tcp_send(sock, "GET / HTTP/1.0\r\n\r\n");
let response = tcp_recv(sock, 4096);
tcp_close(sock);
```

## Architecture

### Compiler Stages
1. **Lexical Analysis** - Converts source to tokens
2. **Parsing** - Builds AST from tokens
3. **Semantic Analysis** - Type checking and validation
4. **Code Generation** - Produces C code
5. **C Compilation** - Uses system C compiler for final binary

### Runtime Library
Provides abstraction over:
- POSIX/Windows APIs
- Memory management
- File and network operations
- Data structures
- Mathematical functions

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on contributing to the Demonic C project.

## License

Demonic C is released under the MIT License - see [LICENSE.md](LICENSE.md) for details.