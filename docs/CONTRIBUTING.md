# Contributing to Demonic C

Thank you for your interest in contributing to Demonic C! This document provides guidelines for contributing to the project.

## How to Contribute

### Reporting Bugs
- Check if the issue has already been reported
- Provide a minimal reproducible example
- Include:
  - Demonic C version (`./dmc-native --version`)
  - Operating system and architecture
  - Steps to reproduce
  - Expected vs actual behavior

### Suggesting Features
- Clearly describe the problem your feature solves
- Explain how it fits with the language's goals
- Consider implementation complexity
- Provide examples of usage

### Submitting Changes
1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make your changes
4. Ensure all tests pass: `make test`
5. Run the smoke test: `make smoke`
6. Update documentation as needed
7. Commit your changes with clear messages
8. Push to your branch: `git push origin feature/amazing-feature`
9. Open a pull request

## Development Setup

### Prerequisites
- Clang/LLVM compiler (for building the compiler)
- Git
- Basic understanding of C and compiler construction

### Building for Development
```bash
# Clean build
make clean

# Build compiler and runtime
make

# Run full test suite
make test

# Quick smoke test
make smoke
```

## Coding Style

### C++ Code (Compiler)
- Follow existing code style in `dmc.cpp`
- Use descriptive variable names
- Keep functions focused and small
- Add comments for complex logic
- Handle errors gracefully

### C Code (Runtime)
- Follow existing style in runtime modules
- Use consistent naming (`dmc_*` prefix)
- Proper error handling with `dmc_set_error()`
- Thread safety considerations where applicable
- Clear documentation in headers

### DMC Code (Tests/Examples)
- Follow existing test patterns
- Use descriptive test names
- Test edge cases
- Clean up resources (files, memory, sockets)
- Use meaningful exit codes for test results

## Testing Philosophy

### Unit Tests
- Each runtime function should have corresponding tests
- Test both success and failure paths
- Boundary conditions and edge cases
- Mock external dependencies when needed

### Integration Tests
- Test compiler features end-to-end
- Self-hosting tests prove language completeness
- Cross-platform compatibility tests
- Performance regression tests when applicable

### Test Organization
- `tests/*.dmc` - Feature tests with expected exit codes
- Self-hosting bootstrap tests in `run_tests.sh`
- Smoke tests validate core runtime functionality

## Documentation

### When to Update Documentation
- Adding new language features
- Changing compiler behavior
- Modifying public API
- Improving clarity of existing docs
- Adding usage examples

### Documentation Files
- `README.md` - Project overview and quick start
- `USAGE.md` - Detailed usage guide and reference
- `CONTRIBUTING.md` - This file
- `LICENSE.md` - License terms
- `docs/` - In-depth guides and tutorials

## Review Process

### What Makes a Good PR
- Focused on single concern
- Includes tests for new functionality
- Updates documentation
- Passes all existing tests
- Clear description of changes
- References related issues

### Code Review Criteria
- Correctness and safety
- Performance implications
- Code clarity and maintainability
- Consistency with existing code
- Security considerations

## Community

### Communication
- Use GitHub Issues for bug reports and feature requests
- Discuss implementation details in PRs
- Be respectful and constructive

### Recognition
- Contributors are acknowledged in release notes
- Significant contributors may be given maintainer access
- Follow semantic versioning for releases

## Getting Help

If you're stuck:
1. Check existing documentation
2. Look at similar features in the codebase
3. Examine test cases for usage patterns
4. Ask for clarification in issues
5. Start with small, well-defined contributions

Thank you for helping make Demonic C better!