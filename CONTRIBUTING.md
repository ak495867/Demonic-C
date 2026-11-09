# Contributing to Demonic C

Welcome to the **Demonic C (DMC)** project! We're building a high-speed, expressive, bare-metal systems programming language that compiles directly to clean, portable C99/C11. Demonic C combines the raw performance and predictable memory layout of classic C with modern developer ergonomics—native multithreading, Go-style CSP channels, SIMD vector intrinsics, universal polyglot FFI, package management, and zero-cost error handling.

Whether you are fixing a subtle compiler edge case, optimizing the runtime engine, expanding the standard library, or adding beginner-friendly tooling, we welcome your contributions.

---

## Table of Contents

- [Core Philosophy](#core-philosophy)
- [Repository Architecture](#repository-architecture)
- [Prerequisites & Environment Setup](#prerequisites--environment-setup)
- [Building the Toolchain Locally](#building-the-toolchain-locally)
- [Testing & Verification](#testing--verification)
  - [Running the Test Suites](#running-the-test-suites)
  - [Authoring New Tests](#authoring-new-tests)
  - [Sanitizers & Debugging](#sanitizers--debugging)
- [Coding Standards & Conventions](#coding-standards--conventions)
  - [Compiler (`compiler/dmc.cpp`)](#compiler-compilerdmccpp)
  - [Native Runtime (`runtime/`)](#native-runtime-runtime)
  - [Standard Library (`stdlib/`)](#standard-library-stdlib)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Security & Supply Chain Integrity](#security--supply-chain-integrity)
- [Release & CI Pipeline](#release--ci-pipeline)
- [Getting Help & Community](#getting-help--community)

---

## Core Philosophy

Demonic C adheres to uncompromising systems-engineering principles:

1. **Zero External Runtime Dependencies**: A compiled DMC executable requires only the platform's standard C library. No bulky VMs, no garbage collectors, and no bloated background runtimes.
2. **Absolute Memory Transparency**: What you write maps directly to memory. Allocations are explicit (heap, stack, or custom bump arenas).
3. **Clean C Interoperability & Portability**: DMC transpiles to standard C99/C11 that builds cleanly with GCC, Clang, or MSVC on Windows, Linux, and macOS (x86_64 and ARM64).
4. **Modern Ergonomics Without Bloat**: Modern features like `defer`, pattern matching, channels, atomics, and SIMD vectors must remain zero-cost or lightweight abstractions.
5. **No Sugar-Coated Errors**: Diagnostics must be pinpoint-accurate, showing exact source locations, column numbers, colorized visual carets, and clear, actionable fix suggestions.

---

## Repository Architecture

```text
Demonic-C/
├── compiler/
│   └── dmc.cpp              # The unified single-binary compiler, parser, AST, and CLI driver (C++20)
├── runtime/
│   ├── dmc_runtime.c        # Native C runtime library (channels, threads, atomics, memory, allocators)
│   └── dmc_runtime.h        # Public C runtime interface
├── stdlib/                  # Standard library modules written in Demonic C
│   ├── thread.dmc           # Multithreading & synchronization abstractions
│   ├── http.dmc             # HTTP server and client protocol framework
│   ├── task.dmc             # Async task primitives & channel wrappers
│   ├── term.dmc             # Terminal styling, ANSI colors, and interactive prompt
│   ├── ffi.dmc              # Dynamic runtime FFI (dlopen/dlsym/LoadLibrary)
│   ├── py.dmc               # Python polyglot runtime bridge
│   ├── js.dmc               # JavaScript polyglot runtime bridge
│   ├── strings.dmc          # String manipulation, slicing, and formatting
│   ├── math.dmc             # High-performance math and trigonometry
│   ├── io.dmc               # File and stream I/O
│   ├── fs.dmc               # Filesystem operations
│   ├── os.dmc               # Process, environment, and OS access
│   ├── json.dmc             # Fast JSON parsing and serialization
│   └── crypto.dmc           # Cryptographic primitives and hashing
├── test-files/              # Modern feature and integration test suites (evaluated via `dmc test`)
├── tests/                   # Compiler and bootstrap regression tests
├── bin/                     # Output directory for native binaries and runtime archives
├── install.sh               # Unix installer script with cryptographic SHA-256 verification
├── install.ps1              # Windows installer script with cryptographic SHA-256 verification
├── .github/workflows/       # GitHub Actions CI and Release build automation
├── README.md                # Language overview and quickstart
├── USAGE.md                 # In-depth language specification and API reference
└── CONTRIBUTING.md          # This contributor guide
```

---

## Prerequisites & Environment Setup

To hack on the compiler and runtime, you will need:

- **C++20 Compiler**:
  - GCC 10+ (`g++`)
  - Clang 11+ (`clang++`)
  - MSVC 2019+ (Visual Studio 16.10 or newer)
- **C11 Compiler**:
  - GCC, Clang, or MSVC (`cl.exe`)
- **System Utilities**:
  - `git`
  - `make` (optional on Unix)
  - `ar` and `ranlib` (for building static runtime archives)
  - Python 3.8+ (for testing polyglot Python bridge & bindgen extensions)

---

## Building the Toolchain Locally

### Windows (PowerShell / MinGW / Clang)

Compile the compiler binary:
```powershell
# Create output directory
New-Item -ItemType Directory -Force -Path bin

# Compile compiler binary (C++20)
g++ -std=c++20 -O2 -Wall -Wextra compiler/dmc.cpp -o bin/dmc-native.exe
Copy-Item bin/dmc-native.exe bin/dmc.exe -Force

# (Optional) Update local user installation for immediate testing
Copy-Item bin/dmc-native.exe $HOME/.dmc/bin/dmc.exe -Force
```

Compile the static runtime archive:
```powershell
gcc -std=c11 -O3 -Iruntime -c runtime/dmc_runtime.c -o runtime/dmc_runtime.o
ar cr bin/libdmc_runtime.a runtime/dmc_runtime.o
ranlib bin/libdmc_runtime.a
```

### Linux & macOS (Bash / Zsh)

Compile the compiler binary:
```bash
mkdir -p bin

# On macOS (universal binary for Apple Silicon arm64 & Intel x86_64):
g++ -std=c++20 -O3 -Wall -Wextra -arch arm64 -arch x86_64 compiler/dmc.cpp -o bin/dmc-native
cp bin/dmc-native bin/dmc
chmod +x bin/dmc bin/dmc-native

# On Linux:
g++ -std=c++20 -O3 -Wall -Wextra compiler/dmc.cpp -o bin/dmc-native
cp bin/dmc-native bin/dmc
chmod +x bin/dmc bin/dmc-native
```

Compile the static runtime archive:
```bash
# On Linux:
gcc -std=c11 -O3 -Iruntime -c runtime/dmc_runtime.c -o runtime/dmc_runtime.o
ar cr bin/libdmc_runtime.a runtime/dmc_runtime.o
ranlib bin/libdmc_runtime.a

# On macOS (universal):
gcc -std=c11 -O3 -arch arm64 -arch x86_64 -Iruntime -c runtime/dmc_runtime.c -o runtime/dmc_runtime.o
ar cr bin/libdmc_runtime.a runtime/dmc_runtime.o
ranlib bin/libdmc_runtime.a
```

Verify your build:
```bash
./bin/dmc --version
```

---

## Testing & Verification

Demonic C ships with an integrated, high-performance test runner built right into the `dmc` executable. No external testing frameworks needed.

### Running the Test Suites

Run the complete test suite (all 15 test suites and 44+ tests):
```bash
# Windows
.\bin\dmc.exe test test-files

# Linux / macOS
./bin/dmc test test-files
```

Run a specific test suite file:
```bash
./bin/dmc test test-files/concurrency_test.dmc
./bin/dmc test test-files/simd_test.dmc
./bin/dmc test test-files/polyglot_ffi_test.dmc
```

Run the benchmark suite:
```bash
./bin/dmc bench
```

### Authoring New Tests

When adding a feature or fixing a bug, you **must** include a corresponding test.

Tests are defined inside `.dmc` files using the `#[test]` attribute:

```dmc
import thread;
import strings;

var g_worker_ran: int = 0;

fn worker_fn() {
    g_worker_ran = 1;
}

#[test]
fn test_thread_execution() -> int {
    let th = thread_spawn(worker_fn);
    if (th < 0) {
        return 1; // Test fails with non-zero exit code
    }

    thread_join(th);
    if (g_worker_ran != 1) {
        return 2; // Failure condition
    }

    return 0; // Return 0 to signal test PASS
}
```

#### Test Rules & Concurrency Safety
- Every test function decorated with `#[test]` must return `int` (`0` for PASS, positive integer for specific failure conditions).
- **Thread & Resource Safety**: Always clean up resources (`mutex_free`, `channel_free`, `thread_join`, file close). Do not allow worker threads to dereference stack pointers after caller scopes exit. When testing string arguments across threads, validate payloads in-flight inside the worker thread rather than referencing freed worker state.

### Sanitizers & Debugging

Demonic C supports first-class compiler sanitizers. Always verify memory-sensitive changes against AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan):

```bash
# Build with sanitizers active
./bin/dmc build my_feature.dmc --sanitize

# Transpile to inspect emitted C code
./bin/dmc my_feature.dmc -o debug.c
```

Because DMC automatically emits `#line` directives, GDB, LLDB, and ASan stack traces point directly to original `.dmc` source lines.

---

## Coding Standards & Conventions

### Compiler (`compiler/dmc.cpp`)

The compiler is organized as a unified, fast, single-binary C++20 implementation:

- **Standard C++20**: Use modern features (`auto`, `std::string_view`, range-based loops, constexpr). Avoid over-engineered template metaprogramming or heavy third-party dependencies.
- **Portability First**:
  - Never write un-guarded platform-specific code. Use `#ifdef _WIN32` vs POSIX guards cleanly.
  - Emitted C code must strictly adhere to standard C99/C11.
  - Explicitly include standard library headers in generated C (`<stdint.h>`, `<stdbool.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<errno.h>`). Never rely on implicit function declarations or compiler-specific default headers.
- **Diagnostic Quality**:
  - Always report line numbers, column numbers, and colored caret underlines (`^`) on syntax or semantic errors.
  - Suggest fixes when a token is close to a valid keyword or identifier.
- **Self-Hosting Discipline**:
  - Keep the language specification clean and deterministic so the compiler remains straightforward to compile and bootstrap.

### Native Runtime (`runtime/`)

- Written in clean, ISO C11.
- Prefix all runtime symbols with `dmc_` or `_dmc_` to avoid namespace collisions with user code or external C libraries.
- Thread-safe by default: guard shared global tables with mutexes or atomic operations.
- Ensure all allocations are matched with free operations and handle `NULL` allocations gracefully.

### Standard Library (`stdlib/`)

- Keep modules modular, intuitive, and thoroughly documented.
- Expose clear return types. Prefer `Result<T, E>` or `Option<T>` for operations that can fail.
- Use explicit type annotations for public module APIs.

---

## Submitting a Pull Request

Follow these steps for a clean, hassle-free PR:

1. **Fork and Branch**:
   ```bash
   git checkout -b feat/your-feature-name
   # or
   git checkout -b fix/your-bug-fix
   ```
2. **Follow Conventional Commits**:
   - `feat: add hardware atomic_cas and atomic_exchange`
   - `fix: resolve POSIX stdint.h include in emitted C headers`
   - `test: add unit tests for SIMD f32x4 lane dot product`
   - `docs: update USAGE.md with HTTP server routing examples`
   - `refactor: optimize token lookahead in expression parser`
3. **Run All Tests**:
   Ensure 100% of tests pass across all suites:
   ```bash
   ./bin/dmc test test-files
   ```
4. **Clean Git Tree**:
   Do not commit untracked temporary files, object files, `.old` binaries, or test outputs.
5. **Open Pull Request**:
   Provide a concise description of:
   - What problem is solved.
   - What changes were introduced.
   - Output from `./bin/dmc test test-files`.

---

## Security & Supply Chain Integrity

Security is not an afterthought; it is built into our core deployment pipeline:

- **Cryptographic SHA-256 Verification**:
  - Both installer scripts ([install.sh](file:///d:/DemonicC/native/install.sh) and [install.ps1](file:///d:/DemonicC/native/install.ps1)) **mandatorily verify** the published SHA-256 hash of release archives before extracting or executing binaries.
  - Never bypass checksum verification. If you modify installer scripts, test them with valid, corrupt, and missing checksums to guarantee fail-closed behavior.
- **Memory Safety**:
  - Ensure all pointer math, array indexing, and string buffers respect boundary limits.
  - Run `--sanitize` on newly introduced features.
- **Reporting Vulnerabilities**:
  - If you identify a security vulnerability or critical compiler crash, please open a private GitHub Security Advisory or report it directly to the maintainers rather than posting a public issue.

---

## Release & CI Pipeline

Our GitHub Actions automation runs on every commit and release tag across a multi-platform matrix:

| Runner | Architecture | Artifact |
|---|---|---|
| `ubuntu-latest` | `x86_64` | `dmc-linux-x86_64.tar.gz` |
| `macos-latest` | `arm64` + `x86_64` (Universal) | `dmc-macos-arm64.tar.gz`, `dmc-macos-x86_64.tar.gz` |
| `windows-latest` | `x86_64` | `dmc-windows-x86_64.zip` |

On tagged releases (`v*.*.*`):
- GitHub Actions compiles binaries with `-O3`/`-O2`.
- Runs full test suites (`./bin/dmc test test-files`).
- Generates individual `*.sha256` files and consolidated `checksums.txt`.
- Publishes all archives, checksums, and installer scripts directly to the GitHub Release page.

---

## Getting Help & Community

- **Explore Issues**: Check existing [GitHub Issues](https://github.com/ak495867/Demonic-C/issues) for planned tasks, discussions, and reported bugs.
- **Documentation**: Check [README.md](file:///d:/DemonicC/native/README.md) and [USAGE.md](file:///d:/DemonicC/native/USAGE.md) for full syntax and API specifications.
- **Interactive Explainer**: Use `dmc explain <topic>` directly in your terminal to inspect language concepts, syntax, and built-in features.

Thank you for contributing to Demonic C. Let's build the fastest, most reliable systems language together!
