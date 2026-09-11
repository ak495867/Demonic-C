# Commands

`dmc-native <source.dmc> [options]` - Compiles Demonic C source to C.

Options:
- `-o <file>` - Write generated C to `<file>` (default: stdout)
- `-I <dir>` - Add import search directory (repeatable)
- `--version` - Print version and exit
- `--help` - Print this message and exit

Commands:
`dmc-native <source.dmc>` - Compile DMC to C

`dmc-native pkg <command> [args]` - Package manager commands

`dmc-native new <name>` - Create new project

`dmc-native init` - Initialize project in current directory

`dmc-native fmt [--check] <file.dmc>` - Format or check .dmc files

`dmc-native lint [--fix] <file.dmc>` - Lint .dmc files for issues

`dmc-native test [path]` - Run DMC test suite

`dmc-native doc [path]` - Generate HTML documentation

`dmc-native build [source]` - Build executable from DMC source

`dmc-native bundle` - Write distributable manifest

`dmc-native login` - Registry authentication initialized locally

`dmc-native deprecate <package>` - Mark package as deprecated

`dmc-native publish` - Publish local package to registry

`dmc-native pkg install <pkg>` - Install package from registry

`dmc-native pkg uninstall <pkg>` - Remove installed package

`dmc-native pkg update <pkg>` - Update package to latest version

`dmc-native pkg search <term>` - Search registry for packages

`dmc-native pkg list` - List installed packages

`dmc-native pkg publish` - Publish local package to registry
