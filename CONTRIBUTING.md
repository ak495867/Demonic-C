# Contributing to Demonic C

Thank you for your interest in contributing to Demonic C. This document describes how to report issues, propose features, and submit changes.

---

## How to contribute

### Reporting bugs

Before filing a new issue, check whether it has already been reported. When filing, please include:

- Demonic C version (`./dmc-native --version`)
- Operating system and architecture
- Steps to reproduce, ideally as a minimal example
- Expected behavior vs. actual behavior

### Suggesting features

- Clearly describe the problem the feature solves, not just the feature itself
- Explain how it fits with the language's goals
- Note any implementation complexity you foresee
- Include example usage showing how the feature would be used in practice

### Submitting changes

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make your changes
4. Run the full test suite: `make test`
5. Run the runtime smoke test: `make smoke`
6. Update documentation as needed
7. Commit your changes with clear, descriptive messages
8. Push to your branch: `git push origin feature/amazing-feature`
9. Open a pull request

---

## Development setup

### Prerequisites

- Clang/LLVM (to build the compiler)
- Git
- A working understanding of C and basic compiler construction concepts

### Building for development

```bash
# Clean build
make clean

# Build compiler and runtime
make

# Run the full test suite
make test

# Quick smoke test
make smoke
```

---

## Coding style

### C++ code (compiler)

- Follow the existing style in `dmc.cpp`
- Use descriptive variable names
- Keep functions focused and small
- Comment non-obvious or complex logic
- Handle errors gracefully rather than assuming success

### C code (runtime)

- Follow the existing style used across runtime modules
- Use consistent naming with the `dmc_*` prefix
- Report errors through `dmc_set_error()`
- Consider thread safety where applicable
- Document public functions clearly in their headers

### DMC code (tests and examples)

- Follow existing test patterns
- Use descriptive test names
- Cover edge cases, not just the happy path
- Clean up resources (files, memory, sockets) before exiting
- Use meaningful exit codes to signal test outcomes

---

## Testing philosophy

### Unit tests

- Every runtime function should have corresponding tests
- Cover both success and failure paths
- Include boundary conditions and edge cases
- Mock external dependencies where practical

### Integration tests

- Test compiler features end-to-end
- Self-hosting tests validate language completeness
- Cross-platform compatibility should be exercised where relevant
- Add performance regression tests when a change could affect hot paths

### Test organization

- `tests/*.dmc` — feature tests with expected exit codes
- Self-hosting bootstrap tests are driven by `run_tests.sh`
- Smoke tests validate core runtime functionality

---

## Documentation

### When to update documentation

Update the relevant docs whenever you:

- Add a new language feature
- Change existing compiler behavior
- Modify a public API
- Improve the clarity of existing material
- Add a usage example worth preserving

### Documentation files

| File | Purpose |
|---|---|
| `README.md` | Project overview and quick start |
| `USAGE.md` | Detailed usage guide and language reference |
| `CONTRIBUTING.md` | This file |
| `LICENSE.md` | License terms |
| `docs/` | In-depth guides and tutorials |

---

## Review process

### What makes a good pull request

- Focused on a single concern
- Includes tests for new functionality
- Updates documentation where relevant
- Passes all existing tests
- Has a clear description of what changed and why
- References related issues where applicable

### Code review criteria

- Correctness and safety
- Performance implications
- Code clarity and maintainability
- Consistency with existing code and conventions
- Security considerations, particularly around memory and low-level access

---

## Community

### Communication

- Use GitHub Issues for bug reports and feature requests
- Discuss implementation details directly on pull requests
- Keep discussion respectful and constructive

### Recognition

- Contributors are acknowledged in release notes
- Sustained, significant contributors may be offered maintainer access
- Releases follow semantic versioning

---

## Getting help

If you're stuck:

1. Check the existing documentation (`README.md`, `USAGE.md`)
2. Look at how similar features are implemented elsewhere in the codebase
3. Review test cases for real usage patterns
4. Ask for clarification by opening an issue
5. Consider starting with a small, well-scoped contribution to get familiar with the codebase

Thank you for helping make Demonic C better.