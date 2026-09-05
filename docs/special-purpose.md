# Demonic C - Special Purpose Applications

This document explains how to use Demonic C for specialized domains and what capabilities it offers.

## What Demonic C Offers

### Core Capabilities
1. **Complete Systems Language** - Compiles to C, runs at native speed
2. **Self-Hosting** - Compiler written in itself, proves completeness
3. **Zero Runtime Overhead** - Direct compilation to C, no interpreter
4. **Cross-Platform** - Windows and Linux support
5. **Direct Hardware Access** - Ports, assembly, interrupts
6. **Memory Control** - Manual management, no GC
6. **Rich Standard Library** - Collections, math, text, I/O, network

### Language Features
- Strong static typing with type inference
- Functions with closures (via function pointers)
- Structs and enums
- Modules/imports system
- Pattern matching via switch
- Inline assembly
- C FFI compatibility

### Runtime Library
- Memory management (alloc, arenas, pools)
- File I/O (sync, async patterns)
- Network programming (TCP client)
- Data structures (vec, queue, map)
- Math functions (trig, exp, log, constants)
- Text processing (concat, substr, parse, format)
- Process control (args, exit, syscalls)

---

## Special Purpose Applications

### 1. Operating Systems Development
```dmc
// Kernel entry point
fn kmain() -> void {
    // Initialize hardware
    port_out8(0x3F8, 0x83);  // UART init
    println("Kernel booted");
    
    // Memory management
    let mem = mem_map(0x100000, 0x1000000);
    
    // Scheduler loop
    while (1) {
        schedule();
    }
}
```

### 2. Embedded Systems
```dmc
// Microcontroller firmware
fn main() -> int {
    // GPIO control
    port_out32(GPIO_BASE + 0x04, 0xFF);  // Set pins output
    
    // PWM via timer interrupt
    isr_set(TIMER_IRQ, pwm_handler);
    
    while (1) {
        // Main loop
        port_out32(GPIO_BASE + 0x08, 0xAA);  // Toggle pins
        delay(1000);
    }
}
```

### 3. Compiler/Interpreter Construction
```dmc
// Build a LISP interpreter in DMC
struct Cell {
    type: int;      // 0=atom, 1=pair
    atom: string;
    car: *Cell;
    cdr: *Cell;
}

fn eval(expr: *Cell, env: *Cell) -> *Cell {
    // Evaluate expression in environment
    // ... pattern matching via switch
}

fn read(stream: *char) -> *Cell {
    // Parse S-expressions
}
```

### 4. Game Engine Core
```dmc
// Entity-Component-System in DMC
struct Entity { id: int; }

struct Component {
    entity: int;
    data: *void;
}

fn create_entity() -> Entity {
    // ...
}

fn add_component(entity: int, comp: *Component) -> void {
    // ...
}

fn system_update(entities: *Entity, count: int) -> void {
    // ...
}
```

### 5. Database Engine
```dmc
// B-tree implementation
struct BTreeNode {
    keys: *int;
    values: *int;
    children: *int;
    is_leaf: bool;
    key_count: int;
}

fn btree_insert(root: *BTreeNode, key: int, value: int) -> int {
    // ...
}

fn btree_search(root: *BTreeNode, key: int) -> int {
    // ...
}
```

### 6. Network Protocol Implementation
```dmc
// Custom protocol stack
struct Packet {
    header: Header;
    payload: *char;
    length: int;
}

fn handle_packet(pkt: *Packet) -> void {
    switch (pkt.header.type) {
        case 1: { handle_syn(pkt); }
        case 2: { handle_ack(pkt); }
        case 3: { handle_data(pkt); }
    }
}
```

### 7. Scientific Computing
```dmc
// Matrix operations
fn matmul(a: *float, b: *float, c: *float, n: int) -> void {
    let i, j, k: int;
    for (i = 0; i < n; i = i + 1) {
        for (j = 0; j < n; j = j + 1) {
            let sum: float = 0.0;
            for (k = 0; k < n; k = k + 1) {
                sum = sum + a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
}
```

### 8. Blockchain/Cryptocurrency
```dmc
// Block structure
struct Block {
    index: int;
    timestamp: int;
    prev_hash: *char;
    hash: *char;
    data: *char;
    nonce: int;
}

fn mine_block(block: *Block, difficulty: int) -> int {
    while (1) {
        let h = sha256(block, sizeof(Block));
        if (meets_difficulty(h, difficulty)) {
            block.hash = h;
            return block.nonce;
        }
        block.nonce = block.nonce + 1;
    }
}
```

### 9. Real-Time Systems
```dmc
// Rate-monotonic scheduler
struct Task {
    period: int;
    deadline: int;
    function: *void;
    next_run: int;
}

fn scheduler(tasks: *Task, count: int) -> void {
    let current: int = 0;
    while (1) {
        let now = time();
        if (tasks[current].next_run <= now) {
            tasks[current].function();
            tasks[current].next_run = now + tasks[current].period;
        }
        current = (current + 1) % count;
    }
}
```

### 10. Compiler Toolchains
```dmc
// Transpiler: DMC to LLVM IR
fn translate_function(fn: *Function) -> *LLVMFunction {
    let builder = LLVMCreateBuilder();
    // ... generate LLVM IR
    return llvm_fn;
}
```

---

## Domain-Specific Guides

| Domain | Document | Key Features |
|--------|----------|--------------|
| AI/ML | `docs/ai-ml.md` | Numerical computing, tensor ops, memory efficiency |
| Quantitative Finance | `docs/quantitative-finance.md` | Low latency, tick processing, risk engines |
| Cybersecurity | `docs/cybersecurity.md` | Memory analysis, network tools, exploit dev |

---

## Getting Started for Your Domain

1. **Identify Requirements**: What primitives do you need?
2. **Check Standard Library**: Most common operations are built-in
3. **Write Helpers**: Build domain-specific abstractions on top
4. **Test Thoroughly**: Use `make test` pattern for validation
5. **Benchmark**: Compare against C baseline
6. **Deploy**: Single binary, no dependencies

---

## Architecture Decision Records

### Why Not Rust/Go/C++?
- **Simpler**: No borrow checker, no complex ownership
- **Faster Compile**: Single-pass compilation to C
- **More Control**: Direct memory, no hidden allocations
- **Self-Hosting**: Proven completeness, not just marketing

### When to Use Demonic C
- Systems programming requiring C performance
- Compiler/language tool construction
- Embedded systems with limited resources
- Security research and exploit development
- High-frequency trading
- Game engines needing determinism
- Educational purposes (understanding compilers)

### When NOT to Use
- Web applications (use Rust/Go/Node)
- Rapid prototyping (use Python)
- Team unfamiliar with systems programming
- Projects requiring extensive library ecosystem

---

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Memory alloc | O(1) | Arena: O(1) amortized |
| Vector push | O(1) amortized | Doubling strategy |
| Map lookup | O(1) average | Hash table, chaining |
| Queue ops | O(1) | Circular buffer |
| File I/O | System dependent | Buffered |
| TCP connect | Network dependent | Non-blocking available |

---

## Community and Support

- **Issues**: Report bugs and request features
- **Discussions**: Architecture and design questions
- **Examples**: `tests/` directory has 40+ examples
- **Documentation**: This folder + `README.md` + `USAGE.md`

---

## Future Extensions

Potential additions based on community needs:
- SIMD intrinsics
- Async I/O runtime
- WebAssembly target
- Debugger integration
- Package manager
- IDE support (LSP)

Contributions welcome for any of these areas!