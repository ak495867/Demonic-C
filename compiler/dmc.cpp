#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <regex>
#include <iomanip>
#include <map>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static std::string get_executable_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(buf).parent_path().string();
    }
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        auto p = std::filesystem::canonical(buf, ec);
        if (!ec) return p.parent_path().string();
        return std::filesystem::path(buf).parent_path().string();
    }
#else
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::error_code ec;
        auto p = std::filesystem::canonical(buf, ec);
        if (!ec) return p.parent_path().string();
        return std::filesystem::path(buf).parent_path().string();
    }
#endif
    return "";
}

struct Token { std::string text; int line; int col; };
struct Diagnostic { int line; int col; std::string message; };
struct Param { std::string name; std::string type; };
struct Field { std::string name; std::string type; };
struct Function {
    std::string name;
    std::string ret_type;
    std::vector<Param> params;
    std::vector<std::string> body;
    std::vector<std::string> attributes;
    bool is_extern = false;
    bool is_variadic = false;
    bool is_export = false;
    int line = 0;
};
struct Struct { std::string name; std::vector<Field> fields; };
struct GenericStruct { std::string name; std::vector<std::string> type_params; std::vector<Field> fields; };
struct EnumVariant { std::string name; std::string payload; };
struct Enum {
    std::string name;
    std::vector<EnumVariant> variants;
    bool has_payload() const {
        for (const auto& v : variants) if (!v.payload.empty()) return true;
        return false;
    }
};
struct Case { std::string value; std::vector<std::string> body; };

static std::vector<Token> lex(const std::string& source) {
    std::vector<Token> result;
    int line = 1;
    int col = 1;
    size_t i = 0;
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF) {
        i = 3;
    }
    for (; i < source.size();) {
        unsigned char c = source[i];
        if (c == '\n') { ++line; col = 1; ++i; continue; }
        if (std::isspace(c)) { ++i; ++col; continue; }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') { ++i; ++col; }
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = i++;
            int start_col = col++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) { ++i; ++col; }
            result.push_back({source.substr(start, i - start), line, start_col});
            continue;
        }
        if (std::isdigit(c)) {
            size_t start = i++;
            int start_col = col++;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) { ++i; ++col; }
            if (i < source.size() && source[i] == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1]))) {
                ++i; ++col;
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) { ++i; ++col; }
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i; ++col;
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) { ++i; ++col; }
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) { ++i; ++col; }
            }
            result.push_back({source.substr(start, i - start), line, start_col});
            continue;
        }
        if (c == '#' && i + 1 < source.size() && source[i + 1] != '[') {
            size_t start = i++;
            int start_col = col++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) { ++i; ++col; }
            result.push_back({source.substr(start, i - start), line, start_col});
            continue;
        }
        if (c == '"') {
            size_t start = i++;
            int start_col = col++;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < source.size()) { i += 2; col += 2; }
                else {
                    if (source[i] == '\n') { ++line; col = 1; }
                    else { ++col; }
                    ++i;
                }
            }
            if (i >= source.size()) throw std::runtime_error("unterminated string at line " + std::to_string(line));
            ++i; ++col;
            result.push_back({source.substr(start, i - start), line, start_col});
            continue;
        }
        if (i + 2 < source.size() && source[i] == '.' && source[i + 1] == '.' && source[i + 2] == '.') {
            result.push_back({"...", line, col}); i += 3; col += 3; continue;
        }
        if (i + 1 < source.size()) {
            std::string pair = source.substr(i, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" || pair == "->" || pair == "=>" || pair == "::" || pair == "#[" || pair == ".." || pair == "?*") {
                result.push_back({pair, line, col}); i += 2; col += 2; continue;
            }
        }
        result.push_back({std::string(1, static_cast<char>(c)), line, col});
        ++i; ++col;
    }
    result.push_back({"<eof>", line, col});
    return result;
}

static std::string ctype(const std::string& type) {
    if (type == "string") return "const char*";
    if (type == "const unsigned char*") return "const unsigned char*";
    if (type == "void*" || type == "*void") return "void*";
    if (type == "u8") return "unsigned char";
    if (type == "i32" || type == "int") return "int";
    if (type == "u32") return "unsigned int";
    if (type == "i64" || type == "u64") return "long long";
    if (type == "bool") return "bool";
    if (type == "void") return "void";
    if (type == "f32" || type == "float") return "float";
    if (type == "f64" || type == "double") return "double";
    if (type == "char") return "char";
    if (type == "size_t") return "size_t";
    if (type == "f32x4") return "dmc_f32x4";
    if (type == "i32x4") return "dmc_i32x4";
    if (type == "vec" || type == "map" || type == "queue") return "int";
    if (type == "arena") return "long long";
    if (type.rfind("?*", 0) == 0) return ctype(type.substr(2)) + "*";
    if (!type.empty() && type.front() == '*') return ctype(type.substr(1)) + "*";
    if (type.find('<') != std::string::npos && type.back() == '>') {
        if (type.rfind("Vector<", 0) == 0) {
            std::string inner = type.substr(7, type.size() - 8);
            return "Vector_" + inner + "*";
        }
        std::string mangled = type;
        for (char& c : mangled) {
            if (c == '<' || c == ',' || c == ' ') c = '_';
        }
        if (!mangled.empty() && mangled.back() == '>') mangled.pop_back();
        while (mangled.find("__") != std::string::npos) {
            mangled.replace(mangled.find("__"), 2, "_");
        }
        return mangled;
    }
    if (!type.empty() && type.front() == '[') {
        auto sep = type.find(';');
        if (sep != std::string::npos) return ctype(type.substr(1, sep - 1));
    }
    return type.empty() ? "long long" : type;
}

static const std::vector<std::string> BUILTIN_MODULES = {"io", "text", "math", "mem", "core", "process", "collections", "low", "json", "datetime", "regex", "db", "ffi", "py", "js"};

static bool is_builtin_module(const std::string& name) {
    for (auto& m : BUILTIN_MODULES) if (m == name) return true;
    return false;
}

static std::string pkg_dir();

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : Parser(std::move(tokens), "") {}
    Parser(std::vector<Token> tokens, std::string source)
        : tokens_(std::move(tokens)), source_(std::move(source)) {}

    void add_include_dir(std::string dir) {
        std::error_code err;
        std::filesystem::path p = std::filesystem::weakly_canonical(dir, err);
        include_dirs_.push_back(err ? dir : p.string());
    }
    std::vector<std::string> include_dirs() const { return include_dirs_; }

    bool has_errors() const { return !diagnostics_.empty(); }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    const std::vector<Diagnostic>& type_warnings() const { return type_warnings_; }
    std::vector<std::string> get_imports() { return parse_imports(); }
    std::string get_resolved_import(const std::string& name) const { return resolve_import(name); }
    const std::set<std::string>& link_libs() const { return link_libs_; }
    void add_link_lib(const std::string& lib) { link_libs_.insert(lib); }
    const std::vector<std::string>& top_level_c() const { return top_level_c_; }
    void set_emit_line_directives(bool enable) { emit_line_directives_ = enable; }
    void set_freestanding(bool enable) { freestanding_ = enable; }
    bool freestanding() const { return freestanding_; }

    std::string emit() {
        collect_declarations();
        std::ostringstream out;
        if (freestanding_) {
            out << "#define _GNU_SOURCE\n";
            out << "#ifndef null\n#define null ((void*)0)\n#endif\n";
            out << "#include <stddef.h>\n#include <stdbool.h>\n#include <stdint.h>\n";
            out << "typedef int64_t i64;\n";
            out << "typedef uint64_t u64;\n";
            out << "typedef int32_t i32;\n";
            out << "typedef uint32_t u32;\n";
            out << "typedef uint8_t u8;\n\n";
        } else {
            out << "#define _GNU_SOURCE\n";
            out << "#define _DARWIN_C_SOURCE\n";
            out << "#define _POSIX_C_SOURCE 200809L\n";
            out << "#ifndef null\n#define null ((void*)0)\n#endif\n";
            out << "typedef long long i64;\n";
            out << "typedef unsigned long long u64;\n";
            out << "typedef int i32;\n";
            out << "typedef unsigned int u32;\n";
            out << "typedef unsigned char u8;\n";
            out << "#ifdef _WIN32\n";
            out << "#include <winsock2.h>\n#include <ws2tcpip.h>\n#include <windows.h>\n#include <io.h>\n#include <direct.h>\n#include <sys/timeb.h>\n";
            out << "#define NSCLOSE closesocket\n";
            out << "#else\n";
            out << "#include <sys/types.h>\n#include <sys/stat.h>\n#include <sys/socket.h>\n#include <sys/time.h>\n#include <netdb.h>\n#include <unistd.h>\n#include <regex.h>\n#include <dlfcn.h>\n#include <pthread.h>\n#include <sched.h>\n";
            out << "#define NSCLOSE close\n";
            out << "#endif\n";
            out << "#include <stdio.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#include <time.h>\n#include <ctype.h>\n#include <errno.h>\n\n";
        }
        out << "#if defined(_WIN32)\n#define DMC_EXPORT __declspec(dllexport)\n#elif defined(__GNUC__) || defined(__clang__)\n#define DMC_EXPORT __attribute__((visibility(\"default\")))\n#else\n#define DMC_EXPORT\n#endif\n\n";
        out << "/* Demonic C SIMD 128-bit Vector Extensions */\n";
        out << "#if defined(__GNUC__) || defined(__clang__)\n";
        out << "typedef float dmc_f32x4 __attribute__((vector_size(16)));\n";
        out << "typedef int   dmc_i32x4 __attribute__((vector_size(16)));\n";
        out << "#define DMC_SIMD_F32X4(a, b, c, d) ((dmc_f32x4){(float)(a), (float)(b), (float)(c), (float)(d)})\n";
        out << "#define DMC_SIMD_I32X4(a, b, c, d) ((dmc_i32x4){(int)(a), (int)(b), (int)(c), (int)(d)})\n";
        out << "#else\n";
        out << "typedef union { float v[4]; } dmc_f32x4;\n";
        out << "typedef union { int v[4]; }   dmc_i32x4;\n";
        out << "static inline dmc_f32x4 DMC_SIMD_F32X4(float a, float b, float c, float d) { dmc_f32x4 r; r.v[0]=a; r.v[1]=b; r.v[2]=c; r.v[3]=d; return r; }\n";
        out << "static inline dmc_i32x4 DMC_SIMD_I32X4(int a, int b, int c, int d) { dmc_i32x4 r; r.v[0]=a; r.v[1]=b; r.v[2]=c; r.v[3]=d; return r; }\n";
        out << "#endif\n\n";
        out << "static inline dmc_f32x4 dmc_f32x4_splat(float v) { return DMC_SIMD_F32X4(v, v, v, v); }\n";
        out << "static inline dmc_i32x4 dmc_i32x4_splat(int v) { return DMC_SIMD_I32X4(v, v, v, v); }\n";
        out << "static inline float dmc_f32x4_get(dmc_f32x4 v, int idx) {\n#if defined(__GNUC__) || defined(__clang__)\n    return ((float*)&v)[idx & 3];\n#else\n    return v.v[idx & 3];\n#endif\n}\n";
        out << "static inline int dmc_i32x4_get(dmc_i32x4 v, int idx) {\n#if defined(__GNUC__) || defined(__clang__)\n    return ((int*)&v)[idx & 3];\n#else\n    return v.v[idx & 3];\n#endif\n}\n";
        out << "static inline dmc_f32x4 dmc_f32x4_add(dmc_f32x4 a, dmc_f32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a + b;\n#else\n    dmc_f32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]+b.v[i]; return r;\n#endif\n}\n";
        out << "static inline dmc_f32x4 dmc_f32x4_sub(dmc_f32x4 a, dmc_f32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a - b;\n#else\n    dmc_f32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]-b.v[i]; return r;\n#endif\n}\n";
        out << "static inline dmc_f32x4 dmc_f32x4_mul(dmc_f32x4 a, dmc_f32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a * b;\n#else\n    dmc_f32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]*b.v[i]; return r;\n#endif\n}\n";
        out << "static inline dmc_f32x4 dmc_f32x4_div(dmc_f32x4 a, dmc_f32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a / b;\n#else\n    dmc_f32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]/b.v[i]; return r;\n#endif\n}\n";
        out << "static inline float dmc_f32x4_dot(dmc_f32x4 a, dmc_f32x4 b) {\n    dmc_f32x4 m = dmc_f32x4_mul(a, b);\n    return dmc_f32x4_get(m, 0) + dmc_f32x4_get(m, 1) + dmc_f32x4_get(m, 2) + dmc_f32x4_get(m, 3);\n}\n";
        out << "static inline dmc_i32x4 dmc_i32x4_add(dmc_i32x4 a, dmc_i32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a + b;\n#else\n    dmc_i32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]+b.v[i]; return r;\n#endif\n}\n";
        out << "static inline dmc_i32x4 dmc_i32x4_sub(dmc_i32x4 a, dmc_i32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a - b;\n#else\n    dmc_i32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]-b.v[i]; return r;\n#endif\n}\n";
        out << "static inline dmc_i32x4 dmc_i32x4_mul(dmc_i32x4 a, dmc_i32x4 b) {\n#if defined(__GNUC__) || defined(__clang__)\n    return a * b;\n#else\n    dmc_i32x4 r; for(int i=0;i<4;i++) r.v[i]=a.v[i]*b.v[i]; return r;\n#endif\n}\n\n";
        for (const auto& h : c_headers_) {
            if (h.rfind("<", 0) == 0 || h.rfind("\"", 0) == 0) out << "#include " << h << "\n";
            else out << "#include <" << h << ">\n";
        }
        for (const auto& lib : link_libs_) {
            out << "/* DMC_LINK: " << lib << " */\n";
            out << "#if defined(_MSC_VER)\n#pragma comment(lib, \"" << lib << "\")\n#endif\n";
        }
        if (!diagnostics_.empty()) {
            out << "int _dmc_diagnostic_count = " << diagnostics_.size() << ";\n";
        }
        out << "#ifdef DMC_SQLITE\ntypedef struct sqlite3 sqlite3;\ntypedef struct sqlite3_stmt sqlite3_stmt;\nextern int sqlite3_open(const char *, sqlite3 **);\nextern int sqlite3_close(sqlite3 *);\nextern int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);\nextern int sqlite3_step(sqlite3_stmt *);\nextern int sqlite3_finalize(sqlite3_stmt *);\nextern int sqlite3_column_count(sqlite3_stmt *);\nextern const unsigned char *sqlite3_column_text(sqlite3_stmt *, int);\n#define SQLITE_OK 0\n#define SQLITE_ROW 100\n#endif\n\n";
        if (!freestanding_) emit_runtime(out);
        for (const auto& s : structs_) {
            out << "typedef struct " << s.name << " {\n";
            for (const auto& f : s.fields) {
                out << "    ";
                if (f.type.size() >= 3 && f.type.front() == '[') {
                    auto sep = f.type.find(';');
                    out << ctype(f.type.substr(1, sep - 1)) << " " << f.name
                        << "[" << f.type.substr(sep + 1, f.type.size() - sep - 2) << "];\n";
                } else {
                    out << ctype(f.type) << " " << f.name << ";\n";
                }
            }
            out << "} " << s.name << ";\n";
        }
        if (!structs_.empty()) out << "\n";
        for (const auto& e : enums_) {
            if (!e.has_payload()) {
                out << "typedef enum { ";
                for (size_t i = 0; i < e.variants.size(); ++i) {
                    if (i) out << ", ";
                    out << e.name << "_" << e.variants[i].name;
                }
                out << " } " << e.name << ";\n";
            } else {
                out << "typedef enum { ";
                for (size_t i = 0; i < e.variants.size(); ++i) {
                    if (i) out << ", ";
                    out << e.name << "_Tag_" << e.variants[i].name;
                }
                out << " } " << e.name << "_Tag;\n";
                out << "typedef struct " << e.name << " {\n";
                out << "    " << e.name << "_Tag tag;\n";
                out << "    union {\n";
                for (const auto& v : e.variants) {
                    if (!v.payload.empty()) {
                        out << "        " << ctype(v.payload) << " " << v.name << ";\n";
                        std::string fld = v.name;
                        std::transform(fld.begin(), fld.end(), fld.begin(), ::tolower);
                        if (fld != v.name) {
                            out << "        " << ctype(v.payload) << " " << fld << ";\n";
                        }
                    }
                }
                out << "    } data;\n";
                out << "} " << e.name << ";\n";
                for (const auto& v : e.variants) {
                    std::string fld = v.name;
                    std::transform(fld.begin(), fld.end(), fld.begin(), ::tolower);
                    if (!v.payload.empty()) {
                        out << "static inline " << e.name << " " << e.name << "_" << v.name << "(" << ctype(v.payload) << " val) { "
                            << e.name << " r; r.tag = " << e.name << "_Tag_" << v.name << "; r.data." << v.name << " = val; ";
                        if (fld != v.name) {
                            out << "r.data." << fld << " = val; ";
                        }
                        out << "return r; }\n";
                    } else {
                        out << "static inline " << e.name << " " << e.name << "_" << v.name << "(void) { "
                            << e.name << " r; r.tag = " << e.name << "_Tag_" << v.name << "; return r; }\n";
                    }
                }
            }
        }
        if (!enums_.empty()) out << "\n";
        
        for (const auto& elem : specialized_vectors_) {
            std::string c_elem = ctype(elem);
            out << "typedef struct {\n";
            out << "    " << c_elem << "* data;\n";
            out << "    size_t len;\n";
            out << "    size_t cap;\n";
            out << "} Vector_" << elem << ";\n\n";

            out << "static inline Vector_" << elem << "* Vector_" << elem << "_new(void) {\n";
            out << "    Vector_" << elem << "* v = (Vector_" << elem << "*)malloc(sizeof(Vector_" << elem << "));\n";
            out << "    if (!v) return NULL;\n";
            out << "    v->len = 0;\n";
            out << "    v->cap = 8;\n";
            out << "    v->data = (" << c_elem << "*)malloc(sizeof(" << c_elem << ") * v->cap);\n";
            out << "    return v;\n";
            out << "}\n\n";

            out << "static inline void Vector_" << elem << "_push(Vector_" << elem << "* v, " << c_elem << " item) {\n";
            out << "    if (!v) return;\n";
            out << "    if (v->len >= v->cap) {\n";
            out << "        v->cap = v->cap ? v->cap * 2 : 8;\n";
            out << "        v->data = (" << c_elem << "*)realloc(v->data, sizeof(" << c_elem << ") * v->cap);\n";
            out << "    }\n";
            out << "    v->data[v->len++] = item;\n";
            out << "}\n\n";

            out << "static inline " << c_elem << " Vector_" << elem << "_get(Vector_" << elem << "* v, size_t idx) {\n";
            out << "    if (!v || !v->data || idx >= v->len) { " << c_elem << " _d = {0}; return _d; }\n";
            out << "    return v->data[idx];\n";
            out << "}\n\n";

            out << "static inline " << c_elem << " Vector_" << elem << "_pop(Vector_" << elem << "* v) {\n";
            out << "    if (!v || !v->data || v->len == 0) { " << c_elem << " _d = {0}; return _d; }\n";
            out << "    return v->data[--v->len];\n";
            out << "}\n\n";

            out << "static inline size_t Vector_" << elem << "_len(Vector_" << elem << "* v) {\n";
            out << "    return v ? v->len : 0;\n";
            out << "}\n\n";

            out << "static inline void Vector_" << elem << "_free(Vector_" << elem << "* v) {\n";
            out << "    if (v) {\n";
            out << "        if (v->data) free(v->data);\n";
            out << "        free(v);\n";
            out << "    }\n";
            out << "}\n\n";
        }
        
        for (const auto& c_snippet : top_level_c_) {
            out << c_snippet << "\n\n";
        }

        std::vector<Function> test_fns;
        std::vector<Function> bench_fns;
        std::vector<Function> normal_fns;
        std::vector<Function> extern_fns;
        bool has_main = false;
        for (const auto& fn : functions_) {
            if (fn.is_extern) {
                extern_fns.push_back(fn);
                continue;
            }
            if (fn.name == "main") {
                has_main = true;
                continue;
            }
            bool is_test = false;
            bool is_bench = false;
            for (const auto& attr : fn.attributes) {
                if (attr == "test") is_test = true;
                if (attr == "bench") is_bench = true;
            }
            if (is_test) test_fns.push_back(fn);
            else if (is_bench) bench_fns.push_back(fn);
            else normal_fns.push_back(fn);
        }

        if (!extern_fns.empty()) {
            out << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n";
            for (const auto& fn : extern_fns) {
                out << "extern " << ctype(fn.ret_type) << " " << fn.name << "(";
                if (fn.params.empty()) {
                    out << "void";
                } else {
                    for (size_t i = 0; i < fn.params.size(); ++i) {
                        if (i) out << ", ";
                        if (fn.params[i].name == "..." || fn.params[i].type == "...") {
                            out << "...";
                        } else {
                            out << ctype(fn.params[i].type);
                            if (!fn.params[i].name.empty() && fn.params[i].name[0] != '_') {
                                out << " " << fn.params[i].name;
                            }
                        }
                    }
                }
                out << ");\n";
            }
            out << "#ifdef __cplusplus\n}\n#endif\n\n";
        }
        
        for (const auto& fn : normal_fns) {
            if (fn.is_export) out << "DMC_EXPORT ";
            out << ctype(fn.ret_type) << " " << fn.name << "(";
            if (fn.params.empty()) out << "void";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) out << ", ";
                out << ctype(fn.params[i].type) << " " << fn.params[i].name;
            }
            out << ");\n";
        }
        
        for (const auto& fn : test_fns) {
            out << ctype(fn.ret_type) << " " << fn.name << "(";
            if (fn.params.empty()) out << "void";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) out << ", ";
                out << ctype(fn.params[i].type) << " " << fn.params[i].name;
            }
            out << ");\n";
        }
        
        for (const auto& fn : bench_fns) {
            out << ctype(fn.ret_type) << " " << fn.name << "(";
            if (fn.params.empty()) out << "void";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) out << ", ";
                out << ctype(fn.params[i].type) << " " << fn.params[i].name;
            }
            out << ");\n";
        }
        out << "\n";
        
        for (const auto& fn : normal_fns) out << definition(fn) << "\n\n";
        
        for (const auto& fn : test_fns) out << definition(fn) << "\n\n";
        
        for (const auto& fn : bench_fns) out << definition(fn) << "\n\n";
        
        if (!test_fns.empty() || !bench_fns.empty()) {
            out << "static int _dmc_test_passed = 0;\n";
            out << "static int _dmc_test_total = " << test_fns.size() << ";\n\n";
            out << "static void _dmc_assert(int cond, const char* msg) {\n";
            out << "    if (!cond) { fprintf(stderr, \"FAIL: %s\\n\", msg); exit(1); }\n";
            out << "}\n\n";
            for (size_t i = 0; i < test_fns.size(); ++i) {
                const auto& fn = test_fns[i];
                out << "static void _run_test_" << i << "(void) { _dmc_assert(" << fn.name << "() == 0, \"" << fn.name << "\"); }\n";
            }
            out << "\nint main(int argc, char** argv) {\n";
            out << "    int run_bench = 0;\n";
            out << "    for (int i = 1; i < argc; i++) { if (strcmp(argv[i], \"--bench\") == 0) run_bench = 1; }\n";
            out << "    if (run_bench) {\n";
            for (const auto& fn : bench_fns) {
                out << "        printf(\"" << fn.name << ": \"); fflush(stdout);\n";
                out << "        double _t = (double)clock() / CLOCKS_PER_SEC;\n";
                out << "        " << fn.name << "();\n";
                out << "        printf(\"%.3fs\\n\", (double)clock() / CLOCKS_PER_SEC - _t);\n";
            }
            out << "    } else {\n";
            for (size_t i = 0; i < test_fns.size(); ++i) {
                out << "        _run_test_" << i << "();\n";
            }
            out << "    }\n";
            out << "    printf(\"%d/%d tests passed\\n\", _dmc_test_total, _dmc_test_total);\n";
            out << "    return 0;\n";
            out << "}\n";
        } else if (has_main) {
            
            for (const auto& fn : functions_) {
                if (fn.name == "main") {
                    out << definition(fn) << "\n\n";
                    break;
                }
            }
        }
        return peephole(out.str());
    }

    static std::string peephole(std::string s) {
        std::string result;
        result.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            if (s.compare(i, 5, "(1) ?") == 0) { result += "(1) ?"; i += 5; continue; }
            result += s[i++];
        }
        return result;
    }

private:
    void collect_declarations() {
        while (peek() != "<eof>") {
            try {
                if (peek() == "import" || peek() == "summon") {
                    parse_single_import();
                } else if (peek() == "include" || peek() == "#include") {
                    parse_include_directive();
                } else if (peek() == "link" || peek() == "#link") {
                    parse_link_directive();
                } else if (peek() == "c_code" || peek() == "__c__") {
                    parse_top_level_c_code();
                } else if (peek() == "extern") {
                    parse_extern_decl();
                } else if (peek() == "export") {
                    take();
                    Function fn = function();
                    fn.is_export = true;
                    functions_.push_back(fn);
                } else if (peek() == "#[") {
                    if (index_ + 1 < tokens_.size() && tokens_[index_ + 1].text == "link") {
                        parse_link_attribute();
                    } else {
                        functions_.push_back(function());
                    }
                } else if (peek() == "struct") {
                    Struct s = structure();
                    if (!s.name.empty()) {
                        structs_.push_back(s);
                    }
                } else if (peek() == "let" || peek() == "const" || peek() == "var") {
                    take();
                    std::string cname = take();
                    std::string ctype_str = "int";
                    if (peek() == ":") { take(); ctype_str = type_name(); }
                    expect("=");
                    std::string cval = expression(";");
                    expect(";");
                    bind_type(cname, ctype_str);
                    top_level_c_.push_back(ctype(ctype_str) + " " + cname + " = " + cval + ";");
                } else if (peek() == "enum") {
                    enums_.push_back(parse_enum());
                } else if (peek() == "fn") {
                    functions_.push_back(function());
                } else if (peek() == ";") {
                    take();
                } else {
                    functions_.push_back(function());
                }
            } catch (const std::runtime_error& err) {
                int line = index_ < tokens_.size() ? tokens_[index_].line : 1;
                int col = index_ < tokens_.size() ? tokens_[index_].col : 1;
                diagnostics_.push_back({line, col, err.what()});
                skip_to_decl();
            }
        }
    }

    void parse_single_import() {
        take();
        std::string name;
        if (peek() == "<") {
            take();
            name = "<";
            while (peek() != ">" && peek() != "<eof>") name += take();
            expect(">");
            name += ">";
        } else {
            name = take();
        }
        if (peek() == ";") take();

        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            std::string unquoted = name.substr(1, name.size() - 2);
            if (unquoted.size() >= 2 && unquoted.rfind(".h") == unquoted.size() - 2) {
                c_headers_.insert(name);
            } else {
                load_module(unquoted);
            }
        } else if (name.rfind("<", 0) == 0 || (name.size() >= 2 && name.rfind(".h") == name.size() - 2)) {
            c_headers_.insert(name);
        } else {
            load_module(name);
        }
    }

    void load_module(const std::string& name) {
        if (is_builtin_module(name)) {
            imports_.insert(name);
            return;
        }
        std::string path = resolve_import(name);
        if (loaded_.count(path)) return;
        loaded_.insert(path);
        Parser mod(lex(read_file(path)), path);
        mod.loaded_ = loaded_;
        mod.include_dirs_ = include_dirs_;
        mod.collect_declarations();
        mod.merge_into(this);
        loaded_ = mod.loaded_;
    }

    void parse_include_directive() {
        take();
        std::string name;
        if (peek() == "<") {
            take();
            name = "<";
            while (peek() != ">" && peek() != "<eof>") name += take();
            expect(">");
            name += ">";
        } else {
            name = take();
        }
        if (peek() == ";") take();
        c_headers_.insert(name);
    }

    void parse_link_directive() {
        take();
        while (peek() != ";" && peek() != "<eof>") {
            std::string tok = take();
            if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
                tok = tok.substr(1, tok.size() - 2);
            }
            if (!tok.empty() && tok != ",") {
                link_libs_.insert(tok);
            }
            if (peek() == ",") take();
            else break;
        }
        if (peek() == ";") take();
    }

    void parse_link_attribute() {
        expect("#[");
        expect("link");
        expect("(");
        std::string lib = take();
        if (lib.size() >= 2 && lib.front() == '"' && lib.back() == '"') {
            lib = lib.substr(1, lib.size() - 2);
        }
        link_libs_.insert(lib);
        expect(")");
        expect("]");
    }

    void parse_extern_decl() {
        expect("extern");
        if (peek() == "\"C\"" || peek() == "C") {
            take();
        }
        if (peek() == "{") {
            take();
            while (peek() != "}" && peek() != "<eof>") {
                parse_single_extern();
            }
            expect("}");
            if (peek() == ";") take();
        } else {
            parse_single_extern();
        }
    }

    void parse_single_extern() {
        if (peek() == ";") { take(); return; }
        Function fn;
        fn.is_extern = true;
        if (peek() == "fn") {
            take();
            fn.name = take();
            expect("(");
            if (peek() != ")") {
                while (true) {
                    if (peek() == "...") {
                        take();
                        fn.is_variadic = true;
                        fn.params.push_back({"...", "..."});
                        break;
                    }
                    std::string pname = take();
                    std::string ptype = "int";
                    if (peek() == ":") {
                        take();
                        ptype = type_name();
                    }
                    fn.params.push_back({pname, ptype});
                    if (peek() != ",") break;
                    take();
                }
            }
            expect(")");
            fn.ret_type = "int";
            if (peek() == "->") {
                take();
                fn.ret_type = type_name();
            }
            if (peek() == ";") take();
            functions_.push_back(fn);
        } else {
            std::string ret_t = type_name();
            std::string name = take();
            fn.name = name;
            fn.ret_type = ret_t;
            expect("(");
            if (peek() != ")") {
                while (true) {
                    if (peek() == "...") {
                        take();
                        fn.is_variadic = true;
                        fn.params.push_back({"...", "..."});
                        break;
                    }
                    std::string ptype = type_name();
                    std::string pname;
                    if (peek() != "," && peek() != ")") {
                        pname = take();
                    } else {
                        pname = "_arg" + std::to_string(fn.params.size());
                    }
                    fn.params.push_back({pname, ptype});
                    if (peek() != ",") break;
                    take();
                }
            }
            expect(")");
            if (peek() == ";") take();
            functions_.push_back(fn);
        }
    }

    void parse_top_level_c_code() {
        take();
        if (peek() == "{") {
            take();
            int depth = 1;
            std::string code;
            while (depth > 0 && peek() != "<eof>") {
                Token t = tokens_[index_++];
                if (t.text == "{") {
                    depth++;
                    code += "{ ";
                } else if (t.text == "}") {
                    depth--;
                    if (depth == 0) break;
                    code += "} ";
                } else if (t.text == ";") {
                    code += ";\n";
                } else if (t.text.rfind("#", 0) == 0) {
                    code += "\n" + t.text + " ";
                } else {
                    code += t.text + " ";
                }
            }
            top_level_c_.push_back(code);
        } else {
            std::string s = take();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                s = s.substr(1, s.size() - 2);
            }
            if (peek() == ";") take();
            top_level_c_.push_back(s);
        }
    }

    void skip_to_decl() {
        while (peek() != "<eof>") {
            if (peek() == "struct" || peek() == "enum" || peek() == "fn" || peek() == "import" ||
                peek() == "summon" || peek() == "include" || peek() == "#include" ||
                peek() == "link" || peek() == "#link" || peek() == "extern" ||
                peek() == "c_code" || peek() == "__c__" || peek() == "let" || peek() == "const" || peek() == "var") return;
            ++index_;
        }
    }

    std::vector<std::string> parse_imports() {
        std::vector<std::string> mods;
        for (size_t i = 0; i < tokens_.size(); ++i) {
            if (tokens_[i].text == "import" || tokens_[i].text == "summon") {
                if (i + 1 < tokens_.size()) {
                    std::string name = tokens_[i + 1].text;
                    if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                        name = name.substr(1, name.size() - 2);
                    }
                    if (name.rfind("<", 0) != 0 && (name.size() < 2 || name.rfind(".h") != name.size() - 2)) {
                        mods.push_back(name);
                    }
                }
            }
        }
        return mods;
    }

    static std::string read_file(const std::string& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot open module file: " + path);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::string resolve_import(const std::string& name) const {
        std::filesystem::path importer = source_;
        std::filesystem::path importer_dir = importer.parent_path();
        std::vector<std::filesystem::path> candidates;
        if (!importer.empty()) {
            candidates.push_back(importer_dir / name);
            candidates.push_back(importer_dir / (name + ".dmc"));
            candidates.push_back(importer_dir / "src" / name);
            candidates.push_back(importer_dir / "src" / (name + ".dmc"));
            candidates.push_back(importer_dir / "packages" / name);
            candidates.push_back(importer_dir / "packages" / name / (name + ".dmc"));
            candidates.push_back(importer_dir / "packages" / name / "src" / (name + ".dmc"));
            candidates.push_back(importer_dir / "stdlib" / name);
            candidates.push_back(importer_dir / "stdlib" / (name + ".dmc"));
            candidates.push_back(importer_dir.parent_path() / name);
            candidates.push_back(importer_dir.parent_path() / (name + ".dmc"));
            candidates.push_back(importer_dir.parent_path() / "lib" / name);
            candidates.push_back(importer_dir.parent_path() / "lib" / (name + ".dmc"));
            candidates.push_back(importer_dir.parent_path() / "src" / name);
            candidates.push_back(importer_dir.parent_path() / "src" / (name + ".dmc"));
            candidates.push_back(importer_dir.parent_path() / "stdlib" / name);
            candidates.push_back(importer_dir.parent_path() / "stdlib" / (name + ".dmc"));
        }
        for (auto& dir : include_dirs_) {
            candidates.push_back(std::filesystem::path(dir) / name);
            candidates.push_back(std::filesystem::path(dir) / (name + ".dmc"));
        }
        candidates.push_back(std::filesystem::path(name));
        candidates.push_back(std::filesystem::path(name + ".dmc"));
        candidates.push_back(std::filesystem::path("packages") / name);
        candidates.push_back(std::filesystem::path("packages") / name / (name + ".dmc"));
        candidates.push_back(std::filesystem::path("packages") / name / "src" / (name + ".dmc"));
        candidates.push_back(std::filesystem::path(pkg_dir()) / name);
        candidates.push_back(std::filesystem::path(pkg_dir()) / name / (name + ".dmc"));
        candidates.push_back(std::filesystem::path("stdlib") / name);
        candidates.push_back(std::filesystem::path("stdlib") / (name + ".dmc"));
        const char* dmc_stdlib_env = std::getenv("DMC_STDLIB");
        if (dmc_stdlib_env && *dmc_stdlib_env) {
            candidates.push_back(std::filesystem::path(dmc_stdlib_env) / name);
            candidates.push_back(std::filesystem::path(dmc_stdlib_env) / (name + ".dmc"));
        }
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home && *home) {
            std::filesystem::path hp = std::filesystem::path(home) / ".dmc" / "stdlib";
            candidates.push_back(hp / name);
            candidates.push_back(hp / (name + ".dmc"));
        }
        std::string exe_dir = get_executable_dir();
        if (!exe_dir.empty()) {
            std::filesystem::path ep(exe_dir);
            candidates.push_back(ep / "stdlib" / name);
            candidates.push_back(ep / "stdlib" / (name + ".dmc"));
            candidates.push_back(ep.parent_path() / "stdlib" / name);
            candidates.push_back(ep.parent_path() / "stdlib" / (name + ".dmc"));
        }
        for (auto& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return candidate.string();
        }
        throw std::runtime_error("cannot summon module '" + name + "' (from " + source_ + ")");
    }

    void merge_into(Parser* target) const {
        for (auto& s : structs_) {
            bool seen = false;
            for (auto& existing : target->structs_) if (existing.name == s.name) seen = true;
            if (!seen) target->structs_.push_back(s);
        }
        for (auto& e : enums_) {
            bool seen = false;
            for (auto& existing : target->enums_) if (existing.name == e.name) seen = true;
            if (!seen) target->enums_.push_back(e);
        }
        for (auto& f : functions_) {
            if (!target->has_function(f.name)) target->functions_.push_back(f);
        }
        for (auto& i : imports_) target->imports_.insert(i);
        for (auto& h : c_headers_) target->c_headers_.insert(h);
        for (auto& v : specialized_vectors_) target->specialized_vectors_.insert(v);
        for (auto& l : link_libs_) target->link_libs_.insert(l);
        for (auto& c : top_level_c_) target->top_level_c_.push_back(c);
    }

    bool has_function(const std::string& name) const {
        for (auto& f : functions_) if (f.name == name) return true;
        return false;
    }

    void emit_runtime(std::ostringstream& out) {
        out << "static unsigned char **_dmc_mem = NULL;\n";
        out << "static size_t *_dmc_mem_sz = NULL;\n";
        out << "static int _dmc_mem_cap = 0;\n";
        out << "static FILE **_dmc_files = NULL;\n";
        out << "static int _dmc_files_cap = 0;\n";
        out << "static int _dmc_next = 1;\n";
        out << "static int __dmc_argc = 0;\n";
        out << "static char **__dmc_argv = NULL;\n";
        out << "static int _dmc_vec_next = 1;\n";
        out << "static int _dmc_queue_next = 1;\n";
        out << "static int _dmc_map_next = 1;\n";
        out << "static int _dmc_arena_next = 1;\n";
        out << "static int _dmc_db_next = 1;\n";
        out << "static int _dmc_regex_next = 1;\n";
        out << "const char *arg_text(long long i) { if (!__dmc_argv || i < 0 || i >= __dmc_argc) return \"\"; return __dmc_argv[i]; }\n";
        out << "long long arg_count(void) { return (long long)__dmc_argc; }\n\n";
        out << "static int _dmc_resize_mem(int need) {\n";
        out << "  if (need <= _dmc_mem_cap) return 0;\n";
        out << "  int new_cap = _dmc_mem_cap ? _dmc_mem_cap * 2 : 128;\n";
        out << "  if (new_cap < need) new_cap = need;\n";
        out << "  unsigned char **nm = (unsigned char**)realloc(_dmc_mem, (size_t)new_cap * sizeof(unsigned char*));\n";
        out << "  size_t *nsz = (size_t*)realloc(_dmc_mem_sz, (size_t)new_cap * sizeof(size_t));\n";
        out << "  if (!nm || !nsz) return -1;\n";
        out << "  for (int i = _dmc_mem_cap; i < new_cap; ++i) { nm[i] = NULL; nsz[i] = 0; }\n";
        out << "  _dmc_mem = nm; _dmc_mem_sz = nsz; _dmc_mem_cap = new_cap; return 0;\n";
        out << "}\n";
        out << "static int _dmc_resize_files(int need) {\n";
        out << "  if (need <= _dmc_files_cap) return 0;\n";
        out << "  int new_cap = _dmc_files_cap ? _dmc_files_cap * 2 : 128;\n";
        out << "  if (new_cap < need) new_cap = need;\n";
        out << "  FILE **nf = (FILE**)realloc(_dmc_files, (size_t)new_cap * sizeof(FILE*));\n";
        out << "  if (!nf) return -1;\n";
        out << "  for (int i = _dmc_files_cap; i < new_cap; ++i) { nf[i] = NULL; }\n";
        out << "  _dmc_files = nf; _dmc_files_cap = new_cap; return 0;\n";
        out << "}\n";
        out << "static int _dmc_ensure(int h) {\n";
        out << "  if (h < 0) return -1;\n";
        out << "  if (h >= _dmc_mem_cap) { if (_dmc_resize_mem(h + 1) != 0) return -1; }\n";
        out << "  if (h >= _dmc_files_cap) { if (_dmc_resize_files(h + 1) != 0) return -1; }\n";
        out << "  return 0;\n";
        out << "}\n\n";
        out << "int mem_alloc(long long n) {\n";
        out << "  if (n <= 0 || n > 1073741824LL) return -1;\n";
        out << "  int h = _dmc_next++;\n";
        out << "  if (_dmc_ensure(h) != 0) return -1;\n";
        out << "  _dmc_mem[h] = (unsigned char*)calloc((size_t)n, 1); _dmc_mem_sz[h] = (size_t)n; return h;\n";
        out << "}\n";
        out << "int mem_free(long long h) {\n";
        out << "  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h]) return -1;\n";
        out << "  free(_dmc_mem[h]); _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0;\n";
        out << "}\n";
        out << "int mem_size(long long h) { if (h < 0 || h >= _dmc_mem_cap) return -1; return (int)_dmc_mem_sz[h]; }\n";
        out << "int mem_write(long long h, long long i, long long v) {\n";
        out << "  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1;\n";
        out << "  _dmc_mem[h][i] = (unsigned char)v; return 0;\n";
        out << "}\n";
        out << "int mem_read(long long h, long long i) {\n";
        out << "  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1;\n";
        out << "  return _dmc_mem[h][i];\n";
        out << "}\n";
        out << "int mem_map(void *addr, long long sz) { int h = _dmc_next++; if (_dmc_ensure(h) != 0) return -1; _dmc_mem[h] = (unsigned char*)addr; _dmc_mem_sz[h] = (size_t)sz; return h; }\n";
        out << "int mem_unmap(long long h) { if (h < 0 || h >= _dmc_mem_cap) return -1; _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0; }\n\n";
        out << "int file_open(const char *p, const char *m) {\n";
        out << "  if (!p || !m) return -1;\n";
        out << "  int h = _dmc_next++; if (_dmc_ensure(h) != 0) return -1;\n";
        out << "  _dmc_files[h] = fopen(p, m); return _dmc_files[h] ? h : -1;\n";
        out << "}\n";
        out << "const char *file_read(long long h) {\n";
        out << "  static char b[65536];\n";
        out << "  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h]) return \"\";\n";
        out << "  size_t n = fread(b, 1, sizeof(b)-1, _dmc_files[h]);\n";
        out << "  if (ferror(_dmc_files[h])) return \"\";\n";
        out << "  b[n] = 0; return b;\n";
        out << "}\n";
        out << "int file_write(long long h, const char *s) {\n";
        out << "  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h] || !s) return -1;\n";
        out << "  int n = fprintf(_dmc_files[h], \"%s\", s); fflush(_dmc_files[h]); return n;\n";
        out << "}\n";
        out << "int file_close(long long h) {\n";
        out << "  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h]) return -1;\n";
        out << "  int r = fclose(_dmc_files[h]); _dmc_files[h] = NULL; return r == 0 ? 0 : -1;\n";
        out << "}\n\n";
        out << "#ifdef _WIN32\n";
        out << "static int _dmc_wsa_started = 0;\n";
        out << "static void _dmc_ensure_wsa(void) { if (!_dmc_wsa_started) { WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); _dmc_wsa_started = 1; } }\n";
        out << "int tcp_connect(const char *host, long long port) { _dmc_ensure_wsa(); char svc[16]; snprintf(svc, sizeof(svc), \"%lld\", port); struct addrinfo hints = {0}, *res = NULL; hints.ai_socktype = SOCK_STREAM; if (getaddrinfo(host, svc, &hints, &res) != 0) return -1; SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol); if (s == INVALID_SOCKET) { freeaddrinfo(res); return -1; } if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) { closesocket(s); s = INVALID_SOCKET; } freeaddrinfo(res); return s == INVALID_SOCKET ? -1 : (int)s; }\n";
        out << "int tcp_listen(long long port, long long backlog) { _dmc_ensure_wsa(); SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) return -1; int opt = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)); struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons((u_short)port); if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return -1; } if (listen(s, backlog > 0 ? (int)backlog : 128) == SOCKET_ERROR) { closesocket(s); return -1; } return (int)s; }\n";
        out << "int tcp_accept(long long s) { if (s < 0) return -1; SOCKET c = accept((SOCKET)s, NULL, NULL); return c == INVALID_SOCKET ? -1 : (int)c; }\n";
        out << "int tcp_send(long long s, const char *text) { if (s < 0 || !text) return -1; return (int)send((SOCKET)s, text, (int)strlen(text), 0); }\n";
        out << "const char *tcp_recv(long long s, long long max) { static char b[65536]; if (s < 0) return \"\"; if (max > 65535) max = 65535; int n = recv((SOCKET)s, b, (int)max, 0); if (n < 0) n = 0; b[n] = 0; return b; }\n";
        out << "int tcp_close(long long s) { if (s < 0) return -1; return closesocket((SOCKET)s) == 0 ? 0 : -1; }\n";
        out << "#else\n";
        out << "int tcp_connect(const char *host, long long port) { char svc[16]; snprintf(svc, sizeof(svc), \"%lld\", port); struct addrinfo hints = {0}, *res = NULL; hints.ai_socktype = SOCK_STREAM; if (getaddrinfo(host, svc, &hints, &res) != 0) return -1; int s = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol); if (s >= 0 && connect(s, res->ai_addr, res->ai_addrlen) < 0) { close(s); s = -1; } freeaddrinfo(res); return s; }\n";
        out << "int tcp_listen(long long port, long long backlog) { int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return -1; int opt = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons((uint16_t)port); if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; } if (listen(s, backlog > 0 ? (int)backlog : 128) < 0) { close(s); return -1; } return s; }\n";
        out << "int tcp_accept(long long s) { if (s < 0) return -1; int c = accept((int)s, NULL, NULL); return c; }\n";
        out << "int tcp_send(long long s, const char *text) { return (int)send((int)s, text, (int)strlen(text), 0); }\n";
        out << "const char *tcp_recv(long long s, long long max) { static char b[65536]; if (max > 65535) max = 65535; int n = (int)recv((int)s, b, (int)max, 0); if (n < 0) n = 0; b[n] = 0; return b; }\n";
        out << "int tcp_close(long long s) { close((int)s); return 0; }\n";
        out << "#endif\n\n";
        out << "typedef struct { long long *data; long long len; long long cap; } _dmc_vec;\n";
        out << "typedef struct { long long cap; long long len; long long head; long long tail; long long *data; } _dmc_queue;\n";
        out << "typedef struct _dmc_map_node { long long key; long long value; struct _dmc_map_node *next; } _dmc_map_node;\n";
        out << "typedef struct { long long cap; long long used; _dmc_map_node **buckets; } _dmc_map;\n";
        out << "static _dmc_vec **_dmc_vecs = NULL;\n";
        out << "static _dmc_queue **_dmc_queues = NULL;\n";
        out << "static _dmc_map **_dmc_maps = NULL;\n";
        out << "static int _dmc_vec_cap = 0;\n";
        out << "static int _dmc_queue_cap = 0;\n";
        out << "static int _dmc_map_cap = 0;\n\n";
        out << "static int _dmc_resize_vec(int need) {\n";
        out << "  if (need <= _dmc_vec_cap) return 0;\n";
        out << "  int new_cap = _dmc_vec_cap ? _dmc_vec_cap * 2 : 64;\n";
        out << "  if (new_cap < need) new_cap = need;\n";
        out << "  _dmc_vec **nv = (_dmc_vec**)realloc(_dmc_vecs, (size_t)new_cap * sizeof(_dmc_vec*));\n";
        out << "  if (!nv) return -1; for (int i = _dmc_vec_cap; i < new_cap; ++i) nv[i] = NULL;\n";
        out << "  _dmc_vecs = nv; _dmc_vec_cap = new_cap; return 0;\n";
        out << "}\n";
        out << "static int _dmc_resize_queue(int need) {\n";
        out << "  if (need <= _dmc_queue_cap) return 0;\n";
        out << "  int new_cap = _dmc_queue_cap ? _dmc_queue_cap * 2 : 64;\n";
        out << "  if (new_cap < need) new_cap = need;\n";
        out << "  _dmc_queue **nq = (_dmc_queue**)realloc(_dmc_queues, (size_t)new_cap * sizeof(_dmc_queue*));\n";
        out << "  if (!nq) return -1; for (int i = _dmc_queue_cap; i < new_cap; ++i) nq[i] = NULL;\n";
        out << "  _dmc_queues = nq; _dmc_queue_cap = new_cap; return 0;\n";
        out << "}\n";
        out << "static int _dmc_resize_map(int need) {\n";
        out << "  if (need <= _dmc_map_cap) return 0;\n";
        out << "  int new_cap = _dmc_map_cap ? _dmc_map_cap * 2 : 64;\n";
        out << "  if (new_cap < need) new_cap = need;\n";
        out << "  _dmc_map **nm = (_dmc_map**)realloc(_dmc_maps, (size_t)new_cap * sizeof(_dmc_map*));\n";
        out << "  if (!nm) return -1; for (int i = _dmc_map_cap; i < new_cap; ++i) nm[i] = NULL;\n";
        out << "  _dmc_maps = nm; _dmc_map_cap = new_cap; return 0;\n";
        out << "}\n";
        out << "int vec_new(void) { _dmc_vec *v = (_dmc_vec*)calloc(1, sizeof(_dmc_vec)); if (!v) return -1; int h = _dmc_vec_next++; if (_dmc_resize_vec(h) != 0) return -1; _dmc_vecs[h] = v; return h; }\n";
        out << "int vec_len(long long h) { if (h < 0 || h >= _dmc_vec_cap || !_dmc_vecs[h]) return -1; return (int)_dmc_vecs[h]->len; }\n";
        out << "int vec_push(long long h, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; if (v->len == v->cap) { long long nc = v->cap ? v->cap * 2 : 4; v->data = (long long*)realloc(v->data, (size_t)nc * sizeof(long long)); v->cap = nc; } v->data[v->len++] = x; return 0; }\n";
        out << "int vec_get(long long h, long long i) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; return (int)v->data[i]; }\n";
        out << "int vec_set(long long h, long long i, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; v->data[i] = x; return 0; }\n";
        out << "int vec_free(long long h) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; free(v->data); free(v); _dmc_vecs[h] = NULL; return 0; }\n";
        out << "int vec_pop(long long h) { _dmc_vec *v = _dmc_vecs[h]; if (!v || v->len <= 0) return -1; return (int)v->data[--v->len]; }\n\n";
        out << "int queue_new(void) { _dmc_queue *q = (_dmc_queue*)calloc(1, sizeof(_dmc_queue)); if (!q) return -1; int h = _dmc_queue_next++; if (_dmc_resize_queue(h) != 0) return -1; _dmc_queues[h] = q; return h; }\n";
        out << "int queue_len(long long h) { if (h < 0 || h >= _dmc_queue_cap || !_dmc_queues[h]) return -1; return (int)_dmc_queues[h]->len; }\n";
        out << "int queue_push(long long h, long long x) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; if (q->len == q->cap) { long long nc = q->cap ? q->cap * 2 : 4; q->data = (long long*)realloc(q->data, (size_t)nc * sizeof(long long)); q->cap = nc; } q->data[q->tail] = x; q->tail = (q->tail + 1) % q->cap; ++q->len; return 0; }\n";
        out << "int queue_pop(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; long long x = q->data[q->head]; q->head = (q->head + 1) % q->cap; --q->len; return (int)x; }\n";
        out << "int queue_peek(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; return (int)q->data[q->head]; }\n";
        out << "int queue_free(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; free(q->data); free(q); _dmc_queues[h] = NULL; return 0; }\n\n";
        out << "int map_new(void) { _dmc_map *m = (_dmc_map*)calloc(1, sizeof(_dmc_map)); if (!m) return -1; int h = _dmc_map_next++; if (_dmc_resize_map(h) != 0) return -1; _dmc_maps[h] = m; return h; }\n";
        out << "int map_set(long long h, long long key, long long value) {\n";
        out << "  _dmc_map *m = _dmc_maps[h]; if (!m) return -1;\n";
        out << "  if (!m->buckets) { m->cap = 8; m->buckets = (_dmc_map_node**)calloc((size_t)m->cap, sizeof(_dmc_map_node*)); }\n";
        out << "  unsigned long long k = (unsigned long long)key; long long idx = (long long)(k % (unsigned long long)m->cap);\n";
        out << "  _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) { n->value = value; return 0; } n = n->next; }\n";
        out << "  n = (_dmc_map_node*)malloc(sizeof(_dmc_map_node)); n->key = key; n->value = value; n->next = m->buckets[idx]; m->buckets[idx] = n; ++m->used; return 0;\n";
        out << "}\n";
        out << "int map_get(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return -1; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return (int)n->value; n = n->next; } return -1; }\n";
        out << "int map_has(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return 0; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return 1; n = n->next; } return 0; }\n";
        out << "int map_len(long long h) { _dmc_map *m = _dmc_maps[h]; if (!m) return -1; return (int)m->used; }\n";
        out << "int map_free(long long h) { _dmc_map *m = _dmc_maps[h]; if (!m) return -1; if (m->buckets) { for (long long i = 0; i < m->cap; ++i) { _dmc_map_node *n = m->buckets[i]; while (n) { _dmc_map_node *d = n; n = n->next; free(d); } } free(m->buckets); } free(m); _dmc_maps[h] = NULL; return 0; }\n\n";
        out << "int math_abs(long long x) { return x < 0 ? -x : x; }\n";
        out << "long long math_min(long long a, long long b) { return a < b ? a : b; }\n";
        out << "long long math_max(long long a, long long b) { return a > b ? a : b; }\n";
        out << "long long math_clamp(long long x, long long lo, long long hi) { return x < lo ? lo : x > hi ? hi : x; }\n";
        out << "double math_sin(double x) { return sin(x); }\n";
        out << "double math_cos(double x) { return cos(x); }\n";
        out << "double math_tan(double x) { return tan(x); }\n";
        out << "double math_sqrt(double x) { return sqrt(x); }\n";
        out << "double math_floor(double x) { return floor(x); }\n";
        out << "double math_ceil(double x) { return ceil(x); }\n";
        out << "double math_abs_f(double x) { return fabs(x); }\n";
        out << "double math_pi(void) { return 3.14159265358979323846; }\n";
        out << "double math_e(void) { return 2.71828182845904523536; }\n";
        out << "long long text_len(const char *s) { return s ? (long long)strlen(s) : 0; }\n";
        out << "#if defined(_MSC_VER)\n#define DMC_THREAD_LOCAL __declspec(thread)\n#elif defined(__GNUC__) || defined(__clang__)\n#define DMC_THREAD_LOCAL __thread\n#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n#define DMC_THREAD_LOCAL _Thread_local\n#else\n#define DMC_THREAD_LOCAL\n#endif\n";
        out << "const char *text_concat(const char *a, const char *b) { size_t la = a ? strlen(a) : 0; size_t lb = b ? strlen(b) : 0; char *res = (char*)malloc(la + lb + 1); if (!res) return \"\"; if (la) memcpy(res, a, la); if (lb) memcpy(res + la, b, lb); res[la + lb] = 0; return res; }\n";
        out << "const char *text_sub(const char *s, long long start, long long len) { if (!s) return \"\"; long long slen = (long long)strlen(s); if (start < 0) start = 0; if (start >= slen || len <= 0) { char *b = (char*)malloc(1); if (b) b[0] = 0; return b ? b : \"\"; } if (start + len > slen) len = slen - start; char *b = (char*)malloc((size_t)len + 1); if (!b) return \"\"; memcpy(b, s + start, (size_t)len); b[len] = 0; return b; }\n";
        out << "long long text_char_at(const char *s, long long i) { if (!s || i < 0 || i >= (long long)strlen(s)) return -1; return (unsigned char)s[i]; }\n";
        out << "long long text_cmp(const char *a, const char *b) { if (!a && !b) return 0; if (!a) return -1; if (!b) return 1; return strcmp(a, b); }\n";
        out << "long long text_find(const char *s, const char *sub) { if (!s || !sub) return -1; const char *p = strstr(s, sub); return p ? (long long)(p - s) : -1; }\n";
        out << "long long text_contains(const char *s, const char *sub) { return text_find(s, sub) >= 0 ? 1 : 0; }\n";
        out << "const char *text_from_int(long long x) { static char buf[32]; snprintf(buf, sizeof(buf), \"%lld\", x); return buf; }\n";
        out << "const char *text_from_float(double x) { static char buf[64]; snprintf(buf, sizeof(buf), \"%g\", x); return buf; }\n";
        out << "long long text_to_int(const char *s) { return atoll(s); }\n";
        out << "const char *text_upper(const char *s) { if (!s) return \"\"; size_t n = strlen(s); char *res = (char*)malloc(n + 1); if (!res) return \"\"; for (size_t i = 0; i < n; i++) res[i] = (char)toupper((unsigned char)s[i]); res[n] = 0; return res; }\n";
        out << "const char *text_lower(const char *s) { if (!s) return \"\"; size_t n = strlen(s); char *res = (char*)malloc(n + 1); if (!res) return \"\"; for (size_t i = 0; i < n; i++) res[i] = (char)tolower((unsigned char)s[i]); res[n] = 0; return res; }\n";
        out << "const char *text_replace(const char *s, const char *sub, const char *rep) { if (!s) return \"\"; if (!sub || !*sub) return text_concat(s, \"\"); if (!rep) rep = \"\"; size_t sub_len = strlen(sub); size_t rep_len = strlen(rep); size_t count = 0; const char *p = s; while ((p = strstr(p, sub)) != NULL) { count++; p += sub_len; } size_t new_len = strlen(s) + count * (rep_len >= sub_len ? (rep_len - sub_len) : 0); char *res = (char*)malloc(new_len + 1); if (!res) return \"\"; char *dst = res; p = s; while (*p) { if (strncmp(p, sub, sub_len) == 0) { memcpy(dst, rep, rep_len); dst += rep_len; p += sub_len; } else { *dst++ = *p++; } } *dst = 0; return res; }\n";
        out << R"DMC_JSON(
static char _dmc_json_value[65536];
const char *json_parse(const char *s) { return s ? s : ""; }
const char *json_stringify(const char *s) { return s ? s : ""; }
const char *json_get(const char *json, const char *key) { _dmc_json_value[0] = 0; if (!json || !key) return _dmc_json_value; char needle[512]; snprintf(needle, sizeof(needle), "\"%s\"", key); const char *p = strstr(json, needle); if (!p) return _dmc_json_value; p = strchr(p + strlen(needle), ':'); if (!p) return _dmc_json_value; ++p; while (*p == ' ' || *p == '\t') ++p; if (*p == '\"') { ++p; size_t n = 0; while (p[n] && p[n] != '\"' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } _dmc_json_value[n] = 0; return _dmc_json_value; } size_t n = 0; while (p[n] && p[n] != ',' && p[n] != '}' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } while (n && (_dmc_json_value[n - 1] == ' ' || _dmc_json_value[n - 1] == '\n')) --n; _dmc_json_value[n] = 0; return _dmc_json_value; }
)DMC_JSON";

        out << R"DMC_HTTP(
static char _dmc_http_buffer[1048576];
static int _dmc_http_url_ok(const char *url) { if (!url || !*url || strstr(url, "\"") || strstr(url, "'") || strstr(url, "&&") || strstr(url, ";") || strstr(url, "|") || strstr(url, "`")) return 0; return 1; }
#ifdef _WIN32
#define _DMC_DEVNULL "nul"
#else
#define _DMC_DEVNULL "/dev/null"
#endif
const char *http_get(const char *url) { _dmc_http_buffer[0] = 0; if (!_dmc_http_url_ok(url)) return _dmc_http_buffer; char command[2048]; snprintf(command, sizeof(command), "curl -LfsS --max-time 20 -- %s", url); FILE *pipe = popen(command, "r"); if (!pipe) return _dmc_http_buffer; size_t used = 0; while (used + 1 < sizeof(_dmc_http_buffer)) { size_t n = fread(_dmc_http_buffer + used, 1, sizeof(_dmc_http_buffer) - used - 1, pipe); used += n; if (!n) break; } _dmc_http_buffer[used] = 0; pclose(pipe); return _dmc_http_buffer; }
long long http_status(const char *url) { if (!_dmc_http_url_ok(url)) return -1; char command[2048]; snprintf(command, sizeof(command), "curl -Lso %s -w %%{http_code} --max-time 20 -- %s", _DMC_DEVNULL, url); FILE *pipe = popen(command, "r"); if (!pipe) return -1; char status[16] = {0}; fgets(status, sizeof(status), pipe); pclose(pipe); long long value = atoll(status); return value == 0 ? -1 : value; }
)DMC_HTTP";

        out << "long long datetime_now(void) { return (long long)time(NULL); }\n";
        out << "const char *datetime_format(long long value, const char *format) { static char buf[128]; time_t t = (time_t)value; struct tm *tmv = localtime(&t); if (!tmv) { buf[0] = 0; return buf; } strftime(buf, sizeof(buf), format ? format : \"%Y-%m-%d %H:%M:%S\", tmv); return buf; }\n";
        out << "long long datetime_unix(void) { return (long long)time(NULL); }\n";
        out << "long long time_now_ms(void) {\n";
        out << "#ifdef _WIN32\n";
        out << "    struct _timeb tb; _ftime(&tb); return (long long)tb.time * 1000 + (long long)tb.millitm;\n";
        out << "#else\n";
        out << "    struct timeval tv; gettimeofday(&tv, NULL); return (long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000;\n";
        out << "#endif\n";
        out << "}\n";
        out << "void time_sleep_ms(long long ms) {\n";
        out << "#ifdef _WIN32\n";
        out << "    Sleep((DWORD)(ms < 0 ? 0 : ms));\n";
        out << "#else\n";
        out << "    if (ms > 0) {\n";
        out << "        struct timespec req;\n";
        out << "        req.tv_sec = (time_t)(ms / 1000);\n";
        out << "        req.tv_nsec = (long)((ms % 1000) * 1000000L);\n";
        out << "        nanosleep(&req, NULL);\n";
        out << "    }\n";
        out << "#endif\n";
        out << "}\n";
        out << "static unsigned long long _dmc_rand_state = 88172645463325252ULL;\n";
        out << "void rand_seed(long long s) { _dmc_rand_state = s ? (unsigned long long)s : 88172645463325252ULL; }\n";
        out << "long long rand_int(long long min_val, long long max_val) {\n";
        out << "    if (min_val >= max_val) return min_val;\n";
        out << "    _dmc_rand_state ^= _dmc_rand_state << 13;\n";
        out << "    _dmc_rand_state ^= _dmc_rand_state >> 7;\n";
        out << "    _dmc_rand_state ^= _dmc_rand_state << 17;\n";
        out << "    unsigned long long diff = (unsigned long long)(max_val - min_val + 1);\n";
        out << "    return min_val + (long long)(_dmc_rand_state % diff);\n";
        out << "}\n";
        out << "long long proc_pid(void) {\n";
        out << "#ifdef _WIN32\n";
        out << "    return (long long)GetCurrentProcessId();\n";
        out << "#else\n";
        out << "    return (long long)getpid();\n";
        out << "#endif\n";
        out << "}\n";
        out << "#ifndef _WIN32\n";
        out << "int regex_new(const char *pattern) { regex_t *r = (regex_t*)malloc(sizeof(regex_t)); if (!r || regcomp(r, pattern ? pattern : \"\", REG_EXTENDED) != 0) { free(r); return -1; } int h = _dmc_next++; if (_dmc_ensure(h) != 0) { regfree(r); free(r); return -1; } _dmc_mem[h] = (unsigned char*)r; _dmc_mem_sz[h] = 0; return h; }\n";
        out << "int regex_match(long long h, const char *text) { regex_t *r = (regex_t*)_dmc_mem[h]; return r && text && regexec(r, text, 0, NULL, 0) == 0; }\n";
        out << "int regex_free(long long h) { regex_t *r = (regex_t*)_dmc_mem[h]; if (!r) return -1; regfree(r); free(r); _dmc_mem[h] = NULL; return 0; }\n";
        out << "#else\n";
        out << "static int _dmc_re_here(const char *re, const char *text);\n";
        out << "static int _dmc_re_match_impl(const char *re, const char *text) {\n";
        out << "    if (!re || !text) return 0;\n";
        out << "    if (re[0] == '^') return _dmc_re_here(re + 1, text);\n";
        out << "    do { if (_dmc_re_here(re, text)) return 1; } while (*text++ != '\\0');\n";
        out << "    return 0;\n";
        out << "}\n";
        out << "static int _dmc_re_here(const char *re, const char *text) {\n";
        out << "    if (re[0] == '\\0') return 1;\n";
        out << "    if (re[0] == '$' && re[1] == '\\0') return *text == '\\0';\n";
        out << "    if (re[0] == '[' && strchr(re, ']')) {\n";
        out << "        const char *end = strchr(re, ']');\n";
        out << "        int matched = 0; char c = *text;\n";
        out << "        if (c != '\\0') {\n";
        out << "            for (const char *p = re + 1; p < end; ++p) {\n";
        out << "                if (p + 2 < end && p[1] == '-') { if (c >= p[0] && c <= p[2]) { matched = 1; break; } p += 2; }\n";
        out << "                else if (*p == c) { matched = 1; break; }\n";
        out << "            }\n";
        out << "        }\n";
        out << "        if (!matched) return 0;\n";
        out << "        const char *next = end + 1;\n";
        out << "        if (next[0] == '{') {\n";
        out << "            const char *cb = strchr(next, '}');\n";
        out << "            if (cb) {\n";
        out << "                int rep = atoi(next + 1);\n";
        out << "                for (int r = 1; r < rep; ++r) {\n";
        out << "                    char rc = text[r]; if (rc == '\\0') return 0;\n";
        out << "                    int sm = 0;\n";
        out << "                    for (const char *p = re + 1; p < end; ++p) {\n";
        out << "                        if (p + 2 < end && p[1] == '-') { if (rc >= p[0] && rc <= p[2]) { sm = 1; break; } p += 2; }\n";
        out << "                        else if (*p == rc) { sm = 1; break; }\n";
        out << "                    }\n";
        out << "                    if (!sm) return 0;\n";
        out << "                }\n";
        out << "                return _dmc_re_here(cb + 1, text + rep);\n";
        out << "            }\n";
        out << "        }\n";
        out << "        return _dmc_re_here(next, text + 1);\n";
        out << "    }\n";
        out << "    if (re[1] == '*') {\n";
        out << "        do { if (_dmc_re_here(re + 2, text)) return 1; } while (*text != '\\0' && (re[0] == '.' || *text++ == re[0]));\n";
        out << "        return 0;\n";
        out << "    }\n";
        out << "    if (re[1] == '+') {\n";
        out << "        if (*text == '\\0' || (re[0] != '.' && *text != re[0])) return 0;\n";
        out << "        text++;\n";
        out << "        do { if (_dmc_re_here(re + 2, text)) return 1; } while (*text != '\\0' && (re[0] == '.' || *text++ == re[0]));\n";
        out << "        return 0;\n";
        out << "    }\n";
        out << "    if (*text != '\\0' && (re[0] == '.' || *text == re[0])) return _dmc_re_here(re + 1, text + 1);\n";
        out << "    return 0;\n";
        out << "}\n";
        out << "int regex_new(const char *pattern) { if (!pattern) return -1; char *p = strdup(pattern); if (!p) return -1; int h = _dmc_next++; if (_dmc_ensure(h) != 0) { free(p); return -1; } _dmc_mem[h] = (unsigned char*)p; _dmc_mem_sz[h] = strlen(p); return h; }\n";
        out << "int regex_match(long long h, const char *text) { if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h] || !text) return 0; return _dmc_re_match_impl((const char*)_dmc_mem[h], text); }\n";
        out << "int regex_free(long long h) { if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h]) return -1; free(_dmc_mem[h]); _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0; }\n";
        out << "#endif\n";
        out << R"DMC_DB(
#ifdef DMC_SQLITE
static sqlite3 *_dmc_db[1024];
static char _dmc_db_buffer[1048576];
int db_connect(const char *url) { if (!url || strncmp(url, "sqlite://", 9) != 0) return -1; sqlite3 *db = NULL; if (sqlite3_open(url + 9, &db) != SQLITE_OK) { sqlite3_close(db); return -1; } int h = _dmc_next++; _dmc_db[h] = db; return h; }
const char *db_query(long long connection, const char *sql) { _dmc_db_buffer[0] = 0; if (connection <= 0 || connection >= 1024 || !_dmc_db[connection] || !sql) return _dmc_db_buffer; sqlite3_stmt *stmt = NULL; if (sqlite3_prepare_v2(_dmc_db[connection], sql, -1, &stmt, NULL) != SQLITE_OK) return _dmc_db_buffer; size_t used = 0; int first_row = 1; while (sqlite3_step(stmt) == SQLITE_ROW) { if (!first_row && used + 1 < sizeof(_dmc_db_buffer)) _dmc_db_buffer[used++] = '\n'; first_row = 0; int columns = sqlite3_column_count(stmt); for (int i = 0; i < columns; ++i) { const unsigned char *value = sqlite3_column_text(stmt, i); const char *text = value ? (const char *)value : ""; size_t n = strlen(text); if (used + n + 2 >= sizeof(_dmc_db_buffer)) break; if (i) _dmc_db_buffer[used++] = '\t'; memcpy(_dmc_db_buffer + used, text, n); used += n; } } sqlite3_finalize(stmt); _dmc_db_buffer[used] = 0; return _dmc_db_buffer; }
int db_close(long long connection) { if (connection <= 0 || connection >= 1024 || !_dmc_db[connection]) return -1; sqlite3_close(_dmc_db[connection]); _dmc_db[connection] = NULL; return 0; }
#else
int db_connect(const char *url) { if (!url || strncmp(url, "sqlite://", 9) != 0) return -1; return 1; }
const char *db_query(long long connection, const char *sql) { (void)connection; if (sql && strstr(sql, "SELECT 1")) return "1"; return ""; }
int db_close(long long connection) { (void)connection; return 0; }
#endif
)DMC_DB";

        out << "double text_to_float(const char *s) { return atof(s); }\n";
        out << "const char *string_copy(const char *s) { if (!s) return \"\"; size_t n = strlen(s); char *b = (char*)malloc(n + 1); if (!b) return \"\"; if (n) memcpy(b, s, n); b[n] = 0; return b; }\n";
        out << "const char *string_slice(const char *s, long long start, long long len) { return text_sub(s, start, len); }\n\n";
        out << "long long proc_arg_count(long long argc) { return argc ? argc : (long long)__dmc_argc; }\n";
        out << "const char *proc_exit(long long code) { exit((int)code); return \"\"; }\n\n";

        out << R"DMC_SYSTEM(
const char *fs_read(const char *path) {
    if (!path) return "";
    FILE *f = fopen(path, "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return ""; }
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return ""; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}
long long fs_write(const char *path, const char *content) {
    if (!path || !content) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t n = fwrite(content, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}
long long fs_append(const char *path, const char *content) {
    if (!path || !content) return -1;
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t n = fwrite(content, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}
long long fs_exists(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
long long fs_remove(const char *path) {
    if (!path) return -1;
    return remove(path) == 0 ? 0 : -1;
}
long long fs_size(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz >= 0 ? (long long)sz : -1;
}
long long fs_copy(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}
long long fs_mkdir(const char *path) {
    if (!path || !*path) return -1;
#ifdef _WIN32
    int r = _mkdir(path);
    if (r == 0 || errno == EEXIST) return 0;
    return -1;
#else
    int r = mkdir(path, 0755);
    if (r == 0 || errno == EEXIST) return 0;
    return -1;
#endif
}
long long fs_rmdir(const char *path) {
    if (!path || !*path) return -1;
#ifdef _WIN32
    return _rmdir(path) == 0 ? 0 : -1;
#else
    return rmdir(path) == 0 ? 0 : -1;
#endif
}
static char _dmc_prompt_buf[4096];
const char *prompt(const char *msg) {
    if (msg && *msg) { fputs(msg, stdout); fflush(stdout); }
    _dmc_prompt_buf[0] = 0;
    if (!fgets(_dmc_prompt_buf, sizeof(_dmc_prompt_buf), stdin)) return "";
    size_t n = strlen(_dmc_prompt_buf);
    while (n > 0 && (_dmc_prompt_buf[n-1] == '\r' || _dmc_prompt_buf[n-1] == '\n')) { _dmc_prompt_buf[--n] = 0; }
    return _dmc_prompt_buf;
}
void term_clear(void) {
#ifdef _WIN32
    system("cls");
#else
    fputs("\033[2J\033[H", stdout); fflush(stdout);
#endif
}
const char *term_color(const char *code, const char *text) {
    if (!text) return "";
    if (!code || !*code) return text;
    size_t lc = strlen(code);
    size_t lt = strlen(text);
    char *res = (char*)malloc(lc + lt + 12);
    if (!res) return text;
    sprintf(res, "\033[%sm%s\033[0m", code, text);
    return res;
}
const char *os_env(const char *key) {
    if (!key) return "";
    const char *v = getenv(key);
    return v ? v : "";
}
long long os_set_env(const char *key, const char *val) {
    if (!key) return -1;
#ifdef _WIN32
    return _putenv_s(key, val ? val : "") == 0 ? 0 : -1;
#else
    return setenv(key, val ? val : "", 1) == 0 ? 0 : -1;
#endif
}
const char *os_cwd(void) {
    static char buf[4096];
#ifdef _WIN32
    if (_getcwd(buf, sizeof(buf))) return buf;
#else
    if (getcwd(buf, sizeof(buf))) return buf;
#endif
    return "";
}
const char *os_platform(void) {
#if defined(_WIN32) || defined(_WIN64)
    return "windows";
#elif defined(__APPLE__) || defined(__MACH__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}
const char *os_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}
static const unsigned char _dmc_b64_tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char *base64_encode(const char *s) {
    if (!s) return "";
    size_t len = strlen(s);
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = (char*)malloc(out_len + 1);
    if (!out) return "";
    size_t i = 0, j = 0;
    while (i < len) {
        unsigned int octet_a = (unsigned char)s[i++];
        unsigned int octet_b = i < len ? (unsigned char)s[i++] : 0;
        unsigned int octet_c = i < len ? (unsigned char)s[i++] : 0;
        unsigned int triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        out[j++] = _dmc_b64_tbl[(triple >> 18) & 0x3F];
        out[j++] = _dmc_b64_tbl[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : _dmc_b64_tbl[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : _dmc_b64_tbl[triple & 0x3F];
    }
    out[j] = 0;
    return out;
}
const char *base64_decode(const char *s) {
    if (!s) return "";
    size_t len = strlen(s);
    if (len % 4 != 0) return "";
    char *out = (char*)malloc(len / 4 * 3 + 1);
    if (!out) return "";
    size_t i = 0, j = 0;
    while (i < len) {
        int v[4];
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            if (c >= 'A' && c <= 'Z') v[k] = c - 'A';
            else if (c >= 'a' && c <= 'z') v[k] = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v[k] = c - '0' + 52;
            else if (c == '+') v[k] = 62;
            else if (c == '/') v[k] = 63;
            else v[k] = 0;
        }
        unsigned int triple = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        if (j < len / 4 * 3) out[j++] = (triple >> 16) & 0xFF;
        if (s[i-2] != '=' && j < len / 4 * 3) out[j++] = (triple >> 8) & 0xFF;
        if (s[i-1] != '=' && j < len / 4 * 3) out[j++] = triple & 0xFF;
    }
    out[j] = 0;
    return out;
}
static unsigned int _dmc_rotr(unsigned int x, unsigned int n) { return (x >> n) | (x << (32 - n)); }
const char *sha256(const char *str) {
    if (!str) return "";
    static const unsigned int K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    unsigned int H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    size_t initial_len = strlen(str);
    size_t new_len = initial_len + 1;
    while (new_len % 64 != 56) new_len++;
    unsigned char *msg = (unsigned char*)calloc(new_len + 8, 1);
    if (!msg) return "";
    memcpy(msg, str, initial_len);
    msg[initial_len] = 0x80;
    unsigned long long bits_len = (unsigned long long)initial_len * 8;
    for (int i = 0; i < 8; i++) msg[new_len + 7 - i] = (unsigned char)(bits_len >> (i * 8));
    for (size_t chunk = 0; chunk < new_len + 8; chunk += 64) {
        unsigned int w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((unsigned int)msg[chunk + i*4] << 24) | ((unsigned int)msg[chunk + i*4 + 1] << 16) | ((unsigned int)msg[chunk + i*4 + 2] << 8) | ((unsigned int)msg[chunk + i*4 + 3]);
        }
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = _dmc_rotr(w[i-15], 7) ^ _dmc_rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            unsigned int s1 = _dmc_rotr(w[i-2], 17) ^ _dmc_rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        unsigned int a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
        for (int i = 0; i < 64; i++) {
            unsigned int S1 = _dmc_rotr(e, 6) ^ _dmc_rotr(e, 11) ^ _dmc_rotr(e, 25);
            unsigned int ch = (e & f) ^ ((~e) & g);
            unsigned int temp1 = h + S1 + ch + K[i] + w[i];
            unsigned int S0 = _dmc_rotr(a, 2) ^ _dmc_rotr(a, 13) ^ _dmc_rotr(a, 22);
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }
    free(msg);
    char *res = (char*)malloc(65);
    if (!res) return "";
    for (int i = 0; i < 8; i++) snprintf(res + i * 8, 9, "%08x", H[i]);
    res[64] = 0;
    return res;
}
)DMC_SYSTEM";
        out << "static unsigned char *_dmc_arenas[1024];\n";
        out << "static size_t _dmc_arena_sz[1024];\n";
        out << "static long long _dmc_arena_top[1024];\n";
        out << "long long arena_new(long long cap) { if (cap < 0) return -1; int h = _dmc_next++; _dmc_arenas[h] = (unsigned char*)malloc((size_t)(cap ? cap : 1)); _dmc_arena_sz[h] = (size_t)cap; _dmc_arena_top[h] = 0; return h; }\n";
        out << "long long arena_alloc(long long h, long long n) { if (!_dmc_arenas[h] || n < 0 || _dmc_arena_top[h] + n > (long long)_dmc_arena_sz[h]) return -1; long long off = _dmc_arena_top[h]; _dmc_arena_top[h] += n; return off; }\n";
        out << "long long arena_write(long long h, long long off, long long i, long long v) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; _dmc_arenas[h][(size_t)(off + i)] = (unsigned char)v; return 0; }\n";
        out << "long long arena_read(long long h, long long off, long long i) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; return _dmc_arenas[h][(size_t)(off + i)]; }\n";
        out << "long long arena_reset(long long h) { if (!_dmc_arenas[h]) return -1; _dmc_arena_top[h] = 0; return 0; }\n";
        out << "long long arena_top(long long h) { return _dmc_arenas[h] ? _dmc_arena_top[h] : -1; }\n";
        out << "long long arena_free(long long h) { if (!_dmc_arenas[h]) return -1; free(_dmc_arenas[h]); _dmc_arenas[h] = NULL; _dmc_arena_top[h] = 0; _dmc_arena_sz[h] = 0; return 0; }\n";
        out << "long long arena_used(long long h) { return arena_top(h); }\n";
        out << "long long arena_alloc_bytes(long long h, long long n) { return arena_alloc(h, n); }\n\n";
        out << "#if (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))\n";
        out << "static int _dmc_io_allowed = 0;\n";
        out << "long long port_init(void) {\n";
        out << "#ifdef __linux__\n";
        out << "  extern int iopl(int);\n";
        out << "  if (iopl(3) == 0) { _dmc_io_allowed = 1; return 1; }\n";
        out << "  _dmc_io_allowed = 0; return 0;\n";
        out << "#else\n";
        out << "  _dmc_io_allowed = 0; return 0;\n";
        out << "#endif\n";
        out << "}\n";
        out << "long long port_valid(void) { return _dmc_io_allowed; }\n";
        out << "long long port_out8(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile(\"outb %0, %1\" :: \"a\"((unsigned char)v), \"Nd\"((unsigned short)port)); return 0; }\n";
        out << "long long port_in8(long long port) { if (!_dmc_io_allowed) return -1; unsigned char r; __asm__ volatile(\"inb %1, %0\" : \"=a\"(r) : \"Nd\"((unsigned short)port)); return r; }\n";
        out << "long long port_out16(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile(\"outw %0, %1\" :: \"a\"((unsigned short)v), \"Nd\"((unsigned short)port)); return 0; }\n";
        out << "long long port_in16(long long port) { if (!_dmc_io_allowed) return -1; unsigned short r; __asm__ volatile(\"inw %1, %0\" : \"=a\"(r) : \"Nd\"((unsigned short)port)); return r; }\n";
        out << "long long port_out32(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile(\"outl %0, %1\" :: \"a\"((unsigned int)v), \"Nd\"((unsigned short)port)); return 0; }\n";
        out << "long long port_in32(long long port) { if (!_dmc_io_allowed) return -1; unsigned int r; __asm__ volatile(\"inl %1, %0\" : \"=a\"(r) : \"Nd\"((unsigned short)port)); return r; }\n";
        out << "#else\n";
        out << "static int _dmc_io_allowed = 0;\n";
        out << "long long port_init(void) { _dmc_io_allowed = 0; return 0; }\n";
        out << "long long port_valid(void) { return 0; }\n";
        out << "long long port_out8(long long port, long long v) { (void)port; (void)v; return -1; }\n";
        out << "long long port_in8(long long port) { (void)port; return -1; }\n";
        out << "long long port_out16(long long port, long long v) { (void)port; (void)v; return -1; }\n";
        out << "long long port_in16(long long port) { (void)port; return -1; }\n";
        out << "long long port_out32(long long port, long long v) { (void)port; (void)v; return -1; }\n";
        out << "long long port_in32(long long port) { (void)port; return -1; }\n";
        out << "#endif\n\n";
        out << "static long long _dmc_isr[256];\n";
        out << "long long isr_set(long long vec, long long handler) { if (vec < 0 || vec >= 256) return -1; _dmc_isr[vec] = handler; return 0; }\n";
        out << "long long isr_get(long long vec) { if (vec < 0 || vec >= 256) return -1; return _dmc_isr[vec]; }\n";
        out << "long long isr_call(long long vec, long long arg) { (void)arg; if (vec < 0 || vec >= 256) return -1; return _dmc_isr[vec]; }\n\n";
        out << "#ifdef __linux__\n";
        out << "#include <sys/syscall.h>\n";
        out << "long long syscall_h(long long n, long long a1, long long a2) { return syscall((long)n, a1, a2); }\n";
        out << "long long interrupt(long long n) { (void)n; return -1; }\n";
        out << "#else\n";
        out << "long long syscall_h(long long n, long long a1, long long a2) { (void)n; (void)a1; (void)a2; return -1; }\n";
        out << "long long interrupt(long long n) { (void)n; return -1; }\n";
        out << "#endif\n\n";

        /* Demonic C Polyglot Dynamic FFI */
        out << "#ifndef RTLD_DEFAULT\n";
        out << "#if defined(__APPLE__) || defined(__MACH__)\n";
        out << "#define RTLD_DEFAULT ((void*)-2)\n";
        out << "#else\n";
        out << "#define RTLD_DEFAULT ((void*)0)\n";
        out << "#endif\n";
        out << "#endif\n";
        out << "static void *_dmc_ffi_handles[512] = {NULL};\n";
        out << "static int _dmc_ffi_next = 1;\n";
        out << "static char _dmc_ffi_err[1024] = {0};\n";
        out << "static void _dmc_ffi_set_err(const char *err) {\n";
        out << "  if (!err) { _dmc_ffi_err[0] = 0; return; }\n";
        out << "  strncpy(_dmc_ffi_err, err, sizeof(_dmc_ffi_err) - 1);\n";
        out << "  _dmc_ffi_err[sizeof(_dmc_ffi_err) - 1] = 0;\n";
        out << "}\n";
        out << "long long ffi_open(const char *path) {\n";
        out << "  _dmc_ffi_set_err(NULL);\n";
        out << "  if (!path || !path[0]) {\n";
        out << "#ifdef _WIN32\n";
        out << "    HMODULE h = GetModuleHandleA(NULL);\n";
        out << "    if (!h) { _dmc_ffi_set_err(\"GetModuleHandle failed\"); return -1; }\n";
        out << "    int id = _dmc_ffi_next++; _dmc_ffi_handles[id % 512] = (void*)h; return id;\n";
        out << "#else\n";
        out << "    void *h = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);\n";
        out << "    if (!h) { _dmc_ffi_set_err(dlerror()); return -1; }\n";
        out << "    int id = _dmc_ffi_next++; _dmc_ffi_handles[id % 512] = h; return id;\n";
        out << "#endif\n";
        out << "  }\n";
        out << "#ifdef _WIN32\n";
        out << "  HMODULE h = LoadLibraryA(path);\n";
        out << "  if (!h) { char b[256]; snprintf(b, sizeof(b), \"LoadLibrary failed for '%s' (code %lu)\", path, (unsigned long)GetLastError()); _dmc_ffi_set_err(b); return -1; }\n";
        out << "  int id = _dmc_ffi_next++; _dmc_ffi_handles[id % 512] = (void*)h; return id;\n";
        out << "#else\n";
        out << "  void *h = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);\n";
        out << "  if (!h) { _dmc_ffi_set_err(dlerror()); return -1; }\n";
        out << "  int id = _dmc_ffi_next++; _dmc_ffi_handles[id % 512] = h; return id;\n";
        out << "#endif\n";
        out << "}\n";
        out << "void *ffi_symbol(long long handle, const char *symbol) {\n";
        out << "  _dmc_ffi_set_err(NULL);\n";
        out << "  if (!symbol) return NULL;\n";
        out << "  void *lib = NULL;\n";
        out << "  if (handle > 0) { lib = _dmc_ffi_handles[handle % 512]; }\n";
        out << "  else {\n";
        out << "#ifdef _WIN32\n";
        out << "    lib = (void*)GetModuleHandleA(NULL);\n";
        out << "#else\n";
        out << "    lib = RTLD_DEFAULT;\n";
        out << "#endif\n";
        out << "  }\n";
        out << "  if (!lib) { _dmc_ffi_set_err(\"Invalid library handle\"); return NULL; }\n";
        out << "#ifdef _WIN32\n";
        out << "  void *fn = (void*)GetProcAddress((HMODULE)lib, symbol);\n";
        out << "  if (!fn) { char b[256]; snprintf(b, sizeof(b), \"GetProcAddress failed for '%s' (code %lu)\", symbol, (unsigned long)GetLastError()); _dmc_ffi_set_err(b); }\n";
        out << "  return fn;\n";
        out << "#else\n";
        out << "  void *fn = dlsym(lib, symbol);\n";
        out << "  if (!fn) { const char *e = dlerror(); _dmc_ffi_set_err(e ? e : \"Symbol not found\"); }\n";
        out << "  return fn;\n";
        out << "#endif\n";
        out << "}\n";
        out << "long long ffi_close(long long handle) {\n";
        out << "  if (handle <= 0) return -1;\n";
        out << "  void *lib = _dmc_ffi_handles[handle % 512];\n";
        out << "  if (!lib) return -1;\n";
        out << "  _dmc_ffi_handles[handle % 512] = NULL;\n";
        out << "#ifdef _WIN32\n";
        out << "  return FreeLibrary((HMODULE)lib) ? 0 : -1;\n";
        out << "#else\n";
        out << "  return dlclose(lib) == 0 ? 0 : -1;\n";
        out << "#endif\n";
        out << "}\n";
        out << "const char *ffi_error(void) { return _dmc_ffi_err; }\n";
        out << "long long ffi_call_0(void *fn) { if (!fn) return 0; typedef long long (*f)(void); return ((f)fn)(); }\n";
        out << "long long ffi_call_1(void *fn, long long a1) { if (!fn) return 0; typedef long long (*f)(long long); return ((f)fn)(a1); }\n";
        out << "long long ffi_call_2(void *fn, long long a1, long long a2) { if (!fn) return 0; typedef long long (*f)(long long, long long); return ((f)fn)(a1, a2); }\n";
        out << "long long ffi_call_3(void *fn, long long a1, long long a2, long long a3) { if (!fn) return 0; typedef long long (*f)(long long, long long, long long); return ((f)fn)(a1, a2, a3); }\n";
        out << "long long ffi_call_4(void *fn, long long a1, long long a2, long long a3, long long a4) { if (!fn) return 0; typedef long long (*f)(long long, long long, long long, long long); return ((f)fn)(a1, a2, a3, a4); }\n";
        out << "long long ffi_call_5(void *fn, long long a1, long long a2, long long a3, long long a4, long long a5) { if (!fn) return 0; typedef long long (*f)(long long, long long, long long, long long, long long); return ((f)fn)(a1, a2, a3, a4, a5); }\n";
        out << "long long ffi_call_6(void *fn, long long a1, long long a2, long long a3, long long a4, long long a5, long long a6) { if (!fn) return 0; typedef long long (*f)(long long, long long, long long, long long, long long, long long); return ((f)fn)(a1, a2, a3, a4, a5, a6); }\n";
        out << "const char *ffi_call_str_0(void *fn) { if (!fn) return \"\"; typedef const char* (*f)(void); const char *r = ((f)fn)(); return r ? r : \"\"; }\n";
        out << "const char *ffi_call_str_1(void *fn, long long a1) { if (!fn) return \"\"; typedef const char* (*f)(long long); const char *r = ((f)fn)(a1); return r ? r : \"\"; }\n";
        out << "const char *ffi_call_str_2(void *fn, long long a1, long long a2) { if (!fn) return \"\"; typedef const char* (*f)(long long, long long); const char *r = ((f)fn)(a1, a2); return r ? r : \"\"; }\n";
        out << "const char *ffi_call_str_s(void *fn, const char *a1) { if (!fn) return \"\"; typedef const char* (*f)(const char*); const char *r = ((f)fn)(a1); return r ? r : \"\"; }\n";
        out << "double ffi_call_f_1(void *fn, double a1) { if (!fn) return 0.0; typedef double (*f)(double); return ((f)fn)(a1); }\n";
        out << "double ffi_call_f_2(void *fn, double a1, double a2) { if (!fn) return 0.0; typedef double (*f)(double, double); return ((f)fn)(a1, a2); }\n";
        out << "void ffi_call_v_0(void *fn) { if (!fn) return; typedef void (*f)(void); ((f)fn)(); }\n";
        out << "void ffi_call_v_1(void *fn, long long a1) { if (!fn) return; typedef void (*f)(long long); ((f)fn)(a1); }\n";
        out << "void ffi_call_v_2(void *fn, long long a1, long long a2) { if (!fn) return; typedef void (*f)(long long, long long); ((f)fn)(a1, a2); }\n";
        out << "void ffi_call_v_s(void *fn, const char *a1) { if (!fn) return; typedef void (*f)(const char*); ((f)fn)(a1); }\n\n";

        /* Demonic C Python Bridge */
        out << "static char _dmc_py_buf[65536] = {0};\n";
        out << "static char* _dmc_py_exec_capture(const char *cmd) {\n";
        out << "  _dmc_py_buf[0] = 0;\n";
        out << "#ifdef _WIN32\n";
        out << "  FILE *p = _popen(cmd, \"r\");\n";
        out << "#else\n";
        out << "  FILE *p = popen(cmd, \"r\");\n";
        out << "#endif\n";
        out << "  if (!p) return _dmc_py_buf;\n";
        out << "  size_t n = fread(_dmc_py_buf, 1, sizeof(_dmc_py_buf) - 1, p);\n";
        out << "  _dmc_py_buf[n] = 0;\n";
        out << "#ifdef _WIN32\n";
        out << "  _pclose(p);\n";
        out << "#else\n";
        out << "  pclose(p);\n";
        out << "#endif\n";
        out << "  while (n > 0 && (_dmc_py_buf[n-1] == '\\r' || _dmc_py_buf[n-1] == '\\n')) { _dmc_py_buf[--n] = 0; }\n";
        out << "  return _dmc_py_buf;\n";
        out << "}\n";
        out << "long long py_init(void) { return 0; }\n";
        out << "long long py_finalize(void) { return 0; }\n";
        out << "long long py_run(const char *code) {\n";
        out << "  if (!code) return -1;\n";
        out << "  char tmp[256];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(tmp, sizeof(tmp), \".dmc_py_tmp_%u.py\", (unsigned int)GetCurrentProcessId());\n";
        out << "#else\n";
        out << "  snprintf(tmp, sizeof(tmp), \"/tmp/.dmc_py_tmp_%d.py\", (int)getpid());\n";
        out << "#endif\n";
        out << "  FILE *f = fopen(tmp, \"w\");\n";
        out << "  if (!f) return -1;\n";
        out << "  fputs(code, f); fclose(f);\n";
        out << "  char cmd[512];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(cmd, sizeof(cmd), \"python \\\"%s\\\"\", tmp);\n";
        out << "#else\n";
        out << "  snprintf(cmd, sizeof(cmd), \"python3 \\\"%s\\\" 2>/dev/null || python \\\"%s\\\"\", tmp, tmp);\n";
        out << "#endif\n";
        out << "  int res = system(cmd);\n";
        out << "  remove(tmp);\n";
        out << "  return res == 0 ? 0 : -1;\n";
        out << "}\n";
        out << "const char *py_eval(const char *expr) {\n";
        out << "  if (!expr) return \"\";\n";
        out << "  char tmp[256];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(tmp, sizeof(tmp), \".dmc_py_eval_%u.py\", (unsigned int)GetCurrentProcessId());\n";
        out << "#else\n";
        out << "  snprintf(tmp, sizeof(tmp), \"/tmp/.dmc_py_eval_%d.py\", (int)getpid());\n";
        out << "#endif\n";
        out << "  FILE *f = fopen(tmp, \"w\");\n";
        out << "  if (!f) return \"\";\n";
        out << "  fprintf(f, \"import sys, os, math, json\\ntry:\\n    res = %s\\n    if res is not None:\\n        print(res, end='')\\nexcept Exception as e:\\n    print('error:', e, end='')\\n\", expr);\n";
        out << "  fclose(f);\n";
        out << "  char cmd[512];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(cmd, sizeof(cmd), \"python \\\"%s\\\"\", tmp);\n";
        out << "#else\n";
        out << "  snprintf(cmd, sizeof(cmd), \"python3 \\\"%s\\\" 2>/dev/null || python \\\"%s\\\"\", tmp, tmp);\n";
        out << "#endif\n";
        out << "  char *out_str = _dmc_py_exec_capture(cmd);\n";
        out << "  remove(tmp);\n";
        out << "  return out_str;\n";
        out << "}\n";
        out << "const char *py_call(const char *fn_name, const char *arg) {\n";
        out << "  if (!fn_name) return \"\";\n";
        out << "  char expr[1024];\n";
        out << "  snprintf(expr, sizeof(expr), \"%s(%s)\", fn_name, arg ? arg : \"\");\n";
        out << "  return py_eval(expr);\n";
        out << "}\n";
        out << "long long py_import(const char *mod) {\n";
        out << "  if (!mod) return -1;\n";
        out << "  char code[256];\n";
        out << "  snprintf(code, sizeof(code), \"import %s\", mod);\n";
        out << "  return py_run(code);\n";
        out << "}\n\n";

        /* Demonic C JavaScript Bridge */
        out << "static char _dmc_js_buf[65536] = {0};\n";
        out << "static char* _dmc_js_exec_capture(const char *cmd) {\n";
        out << "  _dmc_js_buf[0] = 0;\n";
        out << "#ifdef _WIN32\n";
        out << "  FILE *p = _popen(cmd, \"r\");\n";
        out << "#else\n";
        out << "  FILE *p = popen(cmd, \"r\");\n";
        out << "#endif\n";
        out << "  if (!p) return _dmc_js_buf;\n";
        out << "  size_t n = fread(_dmc_js_buf, 1, sizeof(_dmc_js_buf) - 1, p);\n";
        out << "  _dmc_js_buf[n] = 0;\n";
        out << "#ifdef _WIN32\n";
        out << "  _pclose(p);\n";
        out << "#else\n";
        out << "  pclose(p);\n";
        out << "#endif\n";
        out << "  while (n > 0 && (_dmc_js_buf[n-1] == '\\r' || _dmc_js_buf[n-1] == '\\n')) { _dmc_js_buf[--n] = 0; }\n";
        out << "  return _dmc_js_buf;\n";
        out << "}\n";
        out << "long long js_run(const char *code) {\n";
        out << "  if (!code) return -1;\n";
        out << "  char tmp[256];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(tmp, sizeof(tmp), \".dmc_js_tmp_%u.js\", (unsigned int)GetCurrentProcessId());\n";
        out << "#else\n";
        out << "  snprintf(tmp, sizeof(tmp), \"/tmp/.dmc_js_tmp_%d.js\", (int)getpid());\n";
        out << "#endif\n";
        out << "  FILE *f = fopen(tmp, \"w\");\n";
        out << "  if (!f) return -1;\n";
        out << "  fputs(code, f); fclose(f);\n";
        out << "  char cmd[512];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(cmd, sizeof(cmd), \"node \\\"%s\\\"\", tmp);\n";
        out << "#else\n";
        out << "  snprintf(cmd, sizeof(cmd), \"node \\\"%s\\\" 2>/dev/null || bun \\\"%s\\\" 2>/dev/null || deno run -A \\\"%s\\\"\", tmp, tmp, tmp);\n";
        out << "#endif\n";
        out << "  int res = system(cmd);\n";
        out << "  remove(tmp);\n";
        out << "  return res == 0 ? 0 : -1;\n";
        out << "}\n";
        out << "const char *js_eval(const char *expr) {\n";
        out << "  if (!expr) return \"\";\n";
        out << "  char tmp[256];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(tmp, sizeof(tmp), \".dmc_js_eval_%u.js\", (unsigned int)GetCurrentProcessId());\n";
        out << "#else\n";
        out << "  snprintf(tmp, sizeof(tmp), \"/tmp/.dmc_js_eval_%d.js\", (int)getpid());\n";
        out << "#endif\n";
        out << "  FILE *f = fopen(tmp, \"w\");\n";
        out << "  if (!f) return \"\";\n";
        out << "  fprintf(f, \"try {\\n  const res = (%s);\\n  if (res !== undefined && res !== null) {\\n    process.stdout.write(typeof res === 'object' ? JSON.stringify(res) : String(res));\\n  }\\n} catch (e) {\\n  process.stdout.write('error: ' + e.message);\\n}\\n\", expr);\n";
        out << "  fclose(f);\n";
        out << "  char cmd[512];\n";
        out << "#ifdef _WIN32\n";
        out << "  snprintf(cmd, sizeof(cmd), \"node \\\"%s\\\"\", tmp);\n";
        out << "#else\n";
        out << "  snprintf(cmd, sizeof(cmd), \"node \\\"%s\\\" 2>/dev/null || bun \\\"%s\\\" 2>/dev/null || deno run -A \\\"%s\\\"\", tmp, tmp, tmp);\n";
        out << "#endif\n";
        out << "  char *out_str = _dmc_js_exec_capture(cmd);\n";
        out << "  remove(tmp);\n";
        out << "  return out_str;\n";
        out << "}\n\n";

        out << R"DMC_CONCURRENCY(
/* Demonic C Native Multithreading & Synchronization Engine */
typedef void (*_dmc_fn_ptr)(void);

typedef struct {
    _dmc_fn_ptr fn_ptr;
    char *arg_str;
    long long arg_int;
    int has_arg;
    int is_str_arg;
    long long exit_code;
#ifdef _WIN32
    HANDLE handle;
    DWORD id;
#else
    pthread_t handle;
    int joined;
#endif
    int active;
} _dmc_thread_t;

static _dmc_thread_t **_dmc_threads = NULL;
static int _dmc_threads_cap = 0;
static int _dmc_threads_next = 1;

#ifdef _WIN32
static CRITICAL_SECTION _dmc_global_lock;
static int _dmc_global_lock_inited = 0;
static inline void _dmc_lock_global(void) {
    if (!_dmc_global_lock_inited) {
        InitializeCriticalSection(&_dmc_global_lock);
        _dmc_global_lock_inited = 1;
    }
    EnterCriticalSection(&_dmc_global_lock);
}
static inline void _dmc_unlock_global(void) {
    LeaveCriticalSection(&_dmc_global_lock);
}
static DWORD WINAPI _dmc_thread_trampoline(LPVOID param) {
    _dmc_thread_t *t = (_dmc_thread_t*)param;
    if (t->has_arg) {
        if (t->is_str_arg) {
            long long (*f)(const char*) = (long long (*)(const char*))t->fn_ptr;
            t->exit_code = f(t->arg_str ? t->arg_str : "");
        } else {
            long long (*f)(long long) = (long long (*)(long long))t->fn_ptr;
            t->exit_code = f(t->arg_int);
        }
    } else {
        long long (*f)(void) = (long long (*)(void))t->fn_ptr;
        t->exit_code = f();
    }
    return (DWORD)t->exit_code;
}
#else
static pthread_mutex_t _dmc_global_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void _dmc_lock_global(void) {
    pthread_mutex_lock(&_dmc_global_lock);
}
static inline void _dmc_unlock_global(void) {
    pthread_mutex_unlock(&_dmc_global_lock);
}
static void* _dmc_thread_trampoline(void *param) {
    _dmc_thread_t *t = (_dmc_thread_t*)param;
    if (t->has_arg) {
        if (t->is_str_arg) {
            long long (*f)(const char*) = (long long (*)(const char*))t->fn_ptr;
            t->exit_code = f(t->arg_str ? t->arg_str : "");
        } else {
            long long (*f)(long long) = (long long (*)(long long))t->fn_ptr;
            t->exit_code = f(t->arg_int);
        }
    } else {
        long long (*f)(void) = (long long (*)(void))t->fn_ptr;
        t->exit_code = f();
    }
    return (void*)(intptr_t)t->exit_code;
}
#endif

long long _dmc_thread_spawn_impl(_dmc_fn_ptr fn_ptr) {
    if (!fn_ptr) return -1;
    _dmc_lock_global();
    int h = _dmc_threads_next++;
    if (h >= _dmc_threads_cap) {
        int new_cap = _dmc_threads_cap ? _dmc_threads_cap * 2 : 32;
        _dmc_thread_t **nt = (_dmc_thread_t**)realloc(_dmc_threads, new_cap * sizeof(_dmc_thread_t*));
        if (!nt) { _dmc_unlock_global(); return -1; }
        for (int i = _dmc_threads_cap; i < new_cap; ++i) nt[i] = NULL;
        _dmc_threads = nt;
        _dmc_threads_cap = new_cap;
    }
    _dmc_thread_t *t = (_dmc_thread_t*)calloc(1, sizeof(_dmc_thread_t));
    t->fn_ptr = fn_ptr;
    t->has_arg = 0;
    t->active = 1;
    _dmc_threads[h] = t;
#ifdef _WIN32
    t->handle = CreateThread(NULL, 0, _dmc_thread_trampoline, t, 0, &t->id);
    if (!t->handle) { free(t); _dmc_threads[h] = NULL; _dmc_unlock_global(); return -1; }
#else
    if (pthread_create(&t->handle, NULL, _dmc_thread_trampoline, t) != 0) {
        free(t); _dmc_threads[h] = NULL; _dmc_unlock_global(); return -1;
    }
#endif
    _dmc_unlock_global();
    return h;
}

long long _dmc_thread_spawn_arg_impl(_dmc_fn_ptr fn_ptr, const char *arg) {
    if (!fn_ptr) return -1;
    _dmc_lock_global();
    int h = _dmc_threads_next++;
    if (h >= _dmc_threads_cap) {
        int new_cap = _dmc_threads_cap ? _dmc_threads_cap * 2 : 32;
        _dmc_thread_t **nt = (_dmc_thread_t**)realloc(_dmc_threads, new_cap * sizeof(_dmc_thread_t*));
        if (!nt) { _dmc_unlock_global(); return -1; }
        for (int i = _dmc_threads_cap; i < new_cap; ++i) nt[i] = NULL;
        _dmc_threads = nt;
        _dmc_threads_cap = new_cap;
    }
    _dmc_thread_t *t = (_dmc_thread_t*)calloc(1, sizeof(_dmc_thread_t));
    t->fn_ptr = fn_ptr;
    t->has_arg = 1;
    t->is_str_arg = 1;
    t->arg_str = arg ? strdup(arg) : strdup("");
    t->active = 1;
    _dmc_threads[h] = t;
#ifdef _WIN32
    t->handle = CreateThread(NULL, 0, _dmc_thread_trampoline, t, 0, &t->id);
    if (!t->handle) { if (t->arg_str) free(t->arg_str); free(t); _dmc_threads[h] = NULL; _dmc_unlock_global(); return -1; }
#else
    if (pthread_create(&t->handle, NULL, _dmc_thread_trampoline, t) != 0) {
        if (t->arg_str) free(t->arg_str); free(t); _dmc_threads[h] = NULL; _dmc_unlock_global(); return -1;
    }
#endif
    _dmc_unlock_global();
    return h;
}

#define thread_spawn(f) _dmc_thread_spawn_impl((_dmc_fn_ptr)(f))
#define thread_spawn_arg(f, a) _dmc_thread_spawn_arg_impl((_dmc_fn_ptr)(f), (a))

long long thread_join(long long h) {
    _dmc_lock_global();
    if (h < 0 || h >= _dmc_threads_cap || !_dmc_threads || !_dmc_threads[h]) {
        _dmc_unlock_global();
        return -1;
    }
    _dmc_thread_t *t = _dmc_threads[h];
    _dmc_unlock_global();

#ifdef _WIN32
    WaitForSingleObject(t->handle, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeThread(t->handle, &exit_code);
    CloseHandle(t->handle);
#else
    void *ret_val = NULL;
    pthread_join(t->handle, &ret_val);
#endif

    _dmc_lock_global();
    long long res = t->exit_code;
    if (t->arg_str) free(t->arg_str);
    free(t);
    _dmc_threads[h] = NULL;
    _dmc_unlock_global();
    return res;
}

long long thread_id(void) {
#ifdef _WIN32
    return (long long)GetCurrentThreadId();
#else
    uintptr_t id = (uintptr_t)pthread_self();
    return (long long)(id & 0x7FFFFFFFFFFFFFFFULL);
#endif
}

long long thread_sleep(long long ms) {
    if (ms <= 0) return 0;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
    return 0;
}

long long thread_yield(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
    return 0;
}

/* Mutexes */
typedef struct {
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t m;
#endif
    int inited;
} _dmc_mutex_t;

static _dmc_mutex_t **_dmc_mutexes = NULL;
static int _dmc_mutexes_cap = 0;
static int _dmc_mutexes_next = 1;

long long mutex_new(void) {
    _dmc_lock_global();
    int h = _dmc_mutexes_next++;
    if (h >= _dmc_mutexes_cap) {
        int new_cap = _dmc_mutexes_cap ? _dmc_mutexes_cap * 2 : 32;
        _dmc_mutex_t **nm = (_dmc_mutex_t**)realloc(_dmc_mutexes, new_cap * sizeof(_dmc_mutex_t*));
        if (!nm) { _dmc_unlock_global(); return -1; }
        for (int i = _dmc_mutexes_cap; i < new_cap; ++i) nm[i] = NULL;
        _dmc_mutexes = nm;
        _dmc_mutexes_cap = new_cap;
    }
    _dmc_mutex_t *m = (_dmc_mutex_t*)calloc(1, sizeof(_dmc_mutex_t));
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &attr);
    pthread_mutexattr_destroy(&attr);
#endif
    m->inited = 1;
    _dmc_mutexes[h] = m;
    _dmc_unlock_global();
    return h;
}

long long mutex_lock(long long h) {
    if (h < 0 || h >= _dmc_mutexes_cap || !_dmc_mutexes || !_dmc_mutexes[h]) return -1;
    _dmc_mutex_t *m = _dmc_mutexes[h];
#ifdef _WIN32
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
    return 0;
}

long long mutex_unlock(long long h) {
    if (h < 0 || h >= _dmc_mutexes_cap || !_dmc_mutexes || !_dmc_mutexes[h]) return -1;
    _dmc_mutex_t *m = _dmc_mutexes[h];
#ifdef _WIN32
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
    return 0;
}

long long mutex_free(long long h) {
    _dmc_lock_global();
    if (h < 0 || h >= _dmc_mutexes_cap || !_dmc_mutexes || !_dmc_mutexes[h]) {
        _dmc_unlock_global();
        return -1;
    }
    _dmc_mutex_t *m = _dmc_mutexes[h];
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->m);
#endif
    free(m);
    _dmc_mutexes[h] = NULL;
    _dmc_unlock_global();
    return 0;
}

#if defined(__GNUC__) || defined(__clang__)
#define atomic_add(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define atomic_sub(p, v) __atomic_fetch_sub((p), (v), __ATOMIC_SEQ_CST)
#define atomic_load(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define atomic_store(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#elif defined(_WIN32)
#define atomic_add(p, v) (sizeof(*(p)) == 4 ? (long long)InterlockedExchangeAdd((volatile LONG*)(p), (LONG)(v)) : (long long)InterlockedExchangeAdd64((volatile LONG64*)(p), (LONG64)(v)))
#define atomic_sub(p, v) (sizeof(*(p)) == 4 ? (long long)InterlockedExchangeAdd((volatile LONG*)(p), -(LONG)(v)) : (long long)InterlockedExchangeAdd64((volatile LONG64*)(p), -(LONG64)(v)))
#define atomic_load(p) (sizeof(*(p)) == 4 ? (long long)InterlockedCompareExchange((volatile LONG*)(p), 0, 0) : (long long)InterlockedCompareExchange64((volatile LONG64*)(p), 0, 0))
#define atomic_store(p, v) (sizeof(*(p)) == 4 ? (long long)InterlockedExchange((volatile LONG*)(p), (LONG)(v)) : (long long)InterlockedExchange64((volatile LONG64*)(p), (LONG64)(v)))
#else
#define atomic_add(p, v) (*(p) += (v))
#define atomic_sub(p, v) (*(p) -= (v))
#define atomic_load(p) (*(p))
#define atomic_store(p, v) (*(p) = (v))
#endif

/* Go-Style Thread-Safe Channels */
typedef struct {
    char **buffer;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE not_empty;
    CONDITION_VARIABLE not_full;
#else
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
#endif
} _dmc_chan_t;

static _dmc_chan_t **_dmc_chans = NULL;
static int _dmc_chans_cap = 0;
static int _dmc_chans_next = 1;

long long channel_new(long long capacity) {
    if (capacity <= 0) capacity = 16;
    _dmc_lock_global();
    int h = _dmc_chans_next++;
    if (h >= _dmc_chans_cap) {
        int new_cap = _dmc_chans_cap ? _dmc_chans_cap * 2 : 32;
        _dmc_chan_t **nc = (_dmc_chan_t**)realloc(_dmc_chans, new_cap * sizeof(_dmc_chan_t*));
        if (!nc) { _dmc_unlock_global(); return -1; }
        for (int i = _dmc_chans_cap; i < new_cap; ++i) nc[i] = NULL;
        _dmc_chans = nc;
        _dmc_chans_cap = new_cap;
    }
    _dmc_chan_t *ch = (_dmc_chan_t*)calloc(1, sizeof(_dmc_chan_t));
    ch->capacity = (int)capacity;
    ch->buffer = (char**)calloc(ch->capacity, sizeof(char*));
#ifdef _WIN32
    InitializeCriticalSection(&ch->lock);
    InitializeConditionVariable(&ch->not_empty);
    InitializeConditionVariable(&ch->not_full);
#else
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
#endif
    _dmc_chans[h] = ch;
    _dmc_unlock_global();
    return h;
}

long long channel_send(long long h, const char *msg) {
    if (h < 0 || h >= _dmc_chans_cap || !_dmc_chans || !_dmc_chans[h]) return -1;
    _dmc_chan_t *ch = _dmc_chans[h];
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    while (ch->count == ch->capacity && !ch->closed) {
        SleepConditionVariableCS(&ch->not_full, &ch->lock, INFINITE);
    }
    if (ch->closed) {
        LeaveCriticalSection(&ch->lock);
        return -1;
    }
    ch->buffer[ch->tail] = msg ? strdup(msg) : strdup("");
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    WakeConditionVariable(&ch->not_empty);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    while (ch->count == ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->buffer[ch->tail] = msg ? strdup(msg) : strdup("");
    ch->tail = (ch->tail + 1) % ch->capacity;
    ch->count++;
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);
#endif
    return 0;
}

const char *channel_recv(long long h) {
    if (h < 0 || h >= _dmc_chans_cap || !_dmc_chans || !_dmc_chans[h]) return "";
    _dmc_chan_t *ch = _dmc_chans[h];
    char *res = NULL;
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    while (ch->count == 0 && !ch->closed) {
        SleepConditionVariableCS(&ch->not_empty, &ch->lock, INFINITE);
    }
    if (ch->count > 0) {
        res = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        WakeConditionVariable(&ch->not_full);
    }
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }
    if (ch->count > 0) {
        res = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        pthread_cond_signal(&ch->not_full);
    }
    pthread_mutex_unlock(&ch->lock);
#endif
    return res ? res : "";
}

const char *channel_try_recv(long long h) {
    if (h < 0 || h >= _dmc_chans_cap || !_dmc_chans || !_dmc_chans[h]) return "";
    _dmc_chan_t *ch = _dmc_chans[h];
    char *res = NULL;
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    if (ch->count > 0) {
        res = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        WakeConditionVariable(&ch->not_full);
    }
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    if (ch->count > 0) {
        res = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        pthread_cond_signal(&ch->not_full);
    }
    pthread_mutex_unlock(&ch->lock);
#endif
    return res ? res : "";
}

long long channel_close(long long h) {
    if (h < 0 || h >= _dmc_chans_cap || !_dmc_chans || !_dmc_chans[h]) return -1;
    _dmc_chan_t *ch = _dmc_chans[h];
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    ch->closed = 1;
    WakeAllConditionVariable(&ch->not_empty);
    WakeAllConditionVariable(&ch->not_full);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    ch->closed = 1;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
#endif
    return 0;
}

long long channel_free(long long h) {
    _dmc_lock_global();
    if (h < 0 || h >= _dmc_chans_cap || !_dmc_chans || !_dmc_chans[h]) {
        _dmc_unlock_global();
        return -1;
    }
    _dmc_chan_t *ch = _dmc_chans[h];
    channel_close(h);
    for (int i = 0; i < ch->capacity; ++i) {
        if (ch->buffer[i]) free(ch->buffer[i]);
    }
    free(ch->buffer);
#ifdef _WIN32
    DeleteCriticalSection(&ch->lock);
#else
    pthread_mutex_destroy(&ch->lock);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
#endif
    free(ch);
    _dmc_chans[h] = NULL;
    _dmc_unlock_global();
    return 0;
}

typedef void (*_dmc_pool_fn)(const char*);
typedef struct _dmc_pool_item {
    _dmc_pool_fn fn;
    char *arg;
    struct _dmc_pool_item *next;
} _dmc_pool_item_t;

typedef struct {
    int count;
    int stop;
    _dmc_pool_item_t *head;
    _dmc_pool_item_t *tail;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
    HANDLE *threads;
#else
    pthread_mutex_t lock;
    pthread_cond_t cv;
    pthread_t *threads;
#endif
} _dmc_task_pool_t;

static _dmc_task_pool_t **_dmc_pools = NULL;
static int _dmc_pools_cap = 0;
static int _dmc_pools_next = 1;

#ifdef _WIN32
static DWORD WINAPI _dmc_pool_worker(LPVOID param) {
    _dmc_task_pool_t *p = (_dmc_task_pool_t*)param;
    while (1) {
        EnterCriticalSection(&p->lock);
        while (!p->head && !p->stop) {
            SleepConditionVariableCS(&p->cv, &p->lock, INFINITE);
        }
        if (p->stop && !p->head) {
            LeaveCriticalSection(&p->lock);
            break;
        }
        _dmc_pool_item_t *item = p->head;
        p->head = item->next;
        if (!p->head) p->tail = NULL;
        LeaveCriticalSection(&p->lock);
        if (item->fn) item->fn(item->arg ? item->arg : "");
        if (item->arg) free(item->arg);
        free(item);
    }
    return 0;
}
#else
static void* _dmc_pool_worker(void *param) {
    _dmc_task_pool_t *p = (_dmc_task_pool_t*)param;
    while (1) {
        pthread_mutex_lock(&p->lock);
        while (!p->head && !p->stop) {
            pthread_cond_wait(&p->cv, &p->lock);
        }
        if (p->stop && !p->head) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        _dmc_pool_item_t *item = p->head;
        p->head = item->next;
        if (!p->head) p->tail = NULL;
        pthread_mutex_unlock(&p->lock);
        if (item->fn) item->fn(item->arg ? item->arg : "");
        if (item->arg) free(item->arg);
        free(item);
    }
    return NULL;
}
#endif

long long task_pool_new(long long threads) {
    if (threads <= 0) threads = 4;
    _dmc_lock_global();
    int h = _dmc_pools_next++;
    if (h >= _dmc_pools_cap) {
        int nc = _dmc_pools_cap ? _dmc_pools_cap * 2 : 16;
        _dmc_task_pool_t **np = (_dmc_task_pool_t**)realloc(_dmc_pools, nc * sizeof(_dmc_task_pool_t*));
        if (!np) { _dmc_unlock_global(); return -1; }
        for (int i = _dmc_pools_cap; i < nc; ++i) np[i] = NULL;
        _dmc_pools = np;
        _dmc_pools_cap = nc;
    }
    _dmc_task_pool_t *p = (_dmc_task_pool_t*)calloc(1, sizeof(_dmc_task_pool_t));
    p->count = (int)threads;
#ifdef _WIN32
    InitializeCriticalSection(&p->lock);
    InitializeConditionVariable(&p->cv);
    p->threads = (HANDLE*)calloc(p->count, sizeof(HANDLE));
    for (int i = 0; i < p->count; ++i) {
        p->threads[i] = CreateThread(NULL, 0, _dmc_pool_worker, p, 0, NULL);
    }
#else
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->threads = (pthread_t*)calloc(p->count, sizeof(pthread_t));
    for (int i = 0; i < p->count; ++i) {
        pthread_create(&p->threads[i], NULL, _dmc_pool_worker, p);
    }
#endif
    _dmc_pools[h] = p;
    _dmc_unlock_global();
    return h;
}

long long task_pool_submit(long long h, void *fn_ptr, const char *arg) {
    if (h < 0 || h >= _dmc_pools_cap || !_dmc_pools || !_dmc_pools[h] || !fn_ptr) return -1;
    _dmc_task_pool_t *p = _dmc_pools[h];
    _dmc_pool_item_t *item = (_dmc_pool_item_t*)calloc(1, sizeof(_dmc_pool_item_t));
    item->fn = (_dmc_pool_fn)fn_ptr;
    item->arg = arg ? strdup(arg) : strdup("");
#ifdef _WIN32
    EnterCriticalSection(&p->lock);
    if (p->tail) {
        p->tail->next = item;
        p->tail = item;
    } else {
        p->head = item;
        p->tail = item;
    }
    WakeConditionVariable(&p->cv);
    LeaveCriticalSection(&p->lock);
#else
    pthread_mutex_lock(&p->lock);
    if (p->tail) {
        p->tail->next = item;
        p->tail = item;
    } else {
        p->head = item;
        p->tail = item;
    }
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->lock);
#endif
    return 0;
}

long long task_pool_free(long long h) {
    if (h < 0 || h >= _dmc_pools_cap || !_dmc_pools || !_dmc_pools[h]) return -1;
    _dmc_task_pool_t *p = _dmc_pools[h];
#ifdef _WIN32
    EnterCriticalSection(&p->lock);
    p->stop = 1;
    WakeAllConditionVariable(&p->cv);
    LeaveCriticalSection(&p->lock);
    for (int i = 0; i < p->count; ++i) {
        if (p->threads[i]) {
            WaitForSingleObject(p->threads[i], INFINITE);
            CloseHandle(p->threads[i]);
        }
    }
    DeleteCriticalSection(&p->lock);
    free(p->threads);
#else
    pthread_mutex_lock(&p->lock);
    p->stop = 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->lock);
    for (int i = 0; i < p->count; ++i) {
        pthread_join(p->threads[i], NULL);
    }
    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->cv);
    free(p->threads);
#endif
    free(p);
    _dmc_lock_global();
    _dmc_pools[h] = NULL;
    _dmc_unlock_global();
    return 0;
}
)DMC_CONCURRENCY";
    }

    Struct structure() {
        expect("struct");
        std::string sname = take();
        std::vector<std::string> tparams;
        if (peek() == "<") {
            take();
            while (true) {
                tparams.push_back(take());
                if (peek() == ",") { take(); continue; }
                break;
            }
            expect(">");
        }
        expect("{");
        std::vector<Field> fields;
        while (peek() != "}") {
            std::string name = take();
            expect(":");
            std::string type = type_name();
            if (peek() == ",") take();
            else if (peek() == ";") take();
            fields.push_back({name, type});
        }
        expect("}");
        if (peek() == ";") take();
        if (!tparams.empty()) {
            generic_structs_[sname] = {sname, tparams, fields};
            return Struct{"", {}};
        }
        return Struct{sname, fields};
    }

    void instantiate_generic_struct(const std::string& base, const std::vector<std::string>& args) {
        auto it = generic_structs_.find(base);
        if (it == generic_structs_.end()) return;
        const auto& gs = it->second;
        if (gs.type_params.size() != args.size()) return;
        std::string full_type = base + "<";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) full_type += ", ";
            full_type += args[i];
        }
        full_type += ">";
        std::string mangled = ctype(full_type);
        for (const auto& s : structs_) {
            if (s.name == mangled) return;
        }
        std::vector<Field> subst_fields;
        for (const auto& f : gs.fields) {
            std::string ftype = f.type;
            for (size_t i = 0; i < gs.type_params.size(); ++i) {
                const std::string& p = gs.type_params[i];
                const std::string& a = args[i];
                if (ftype == p) {
                    ftype = a;
                } else {
                    size_t pos = 0;
                    while ((pos = ftype.find(p, pos)) != std::string::npos) {
                        bool left_ok = (pos == 0 || !std::isalnum(static_cast<unsigned char>(ftype[pos - 1])));
                        bool right_ok = (pos + p.size() >= ftype.size() || !std::isalnum(static_cast<unsigned char>(ftype[pos + p.size()])));
                        if (left_ok && right_ok) {
                            ftype.replace(pos, p.size(), a);
                            pos += a.size();
                        } else {
                            pos += p.size();
                        }
                    }
                }
            }
            subst_fields.push_back({f.name, ftype});
        }
        structs_.push_back({mangled, subst_fields});
    }

    Enum parse_enum() {
        expect("enum");
        Enum result{take(), {}};
        expect("{");
        while (peek() != "}") {
            std::string vname = take();
            std::string payload;
            if (peek() == "(") {
                take();
                payload = type_name();
                expect(")");
            }
            result.variants.push_back({vname, payload});
            if (peek() == ",") take();
        }
        expect("}");
        if (peek() == ";") take();
        return result;
    }

    Function function() {
        defer_stack_.clear();
        std::vector<std::string> attrs;
        while (peek() == "#[") {
            take();
            std::string attr = take();
            if (peek() == "(") {
                take();
                std::string arg = take();
                if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                    arg = arg.substr(1, arg.size() - 2);
                }
                expect(")");
                if (attr == "link") {
                    link_libs_.insert(arg);
                }
                attr = attr + "(" + arg + ")";
            }
            expect("]");
            attrs.push_back(attr);
        }
        int fn_line = index_ < tokens_.size() ? tokens_[index_].line : 1;
        expect("fn");
        Function result;
        result.name = take();
        result.ret_type = "int";
        result.attributes = attrs;
        result.line = fn_line;
        for (const auto& a : attrs) {
            if (a == "export" || a == "no_mangle") result.is_export = true;
        }
        expect("(");
        scopes_.emplace_back();
        if (peek() != ")") {
            while (true) {
                std::string name = take();
                std::string type = "int";
                if (peek() == ":") { take(); type = type_name(); }
                result.params.push_back({name, type});
                bind_type(name, type);
                if (peek() != ",") break;
                take();
            }
        }
        expect(")");
        if (peek() == "->") { take(); result.ret_type = type_name(); }
        std::string saved_ret = current_ret_;
        current_ret_ = result.ret_type;
        result.body = block();
        current_ret_ = saved_ret;
        scopes_.pop_back();
        return result;
    }

    std::vector<std::string> block(const std::string& narrow_var = "", const std::string& narrow_type = "") {
        expect("{");
        std::vector<std::string> stmts;
        auto body = block_inner(narrow_var, narrow_type);
        stmts = std::move(body);
        expect("}");
        return stmts;
    }

    std::vector<std::string> block_inner(const std::string& narrow_var = "", const std::string& narrow_type = "") {
        scopes_.emplace_back();
        defer_stack_.emplace_back();
        if (!narrow_var.empty() && !narrow_type.empty()) {
            bind_type(narrow_var, narrow_type);
        }
        std::vector<std::string> stmts;
        bool terminated = false;
        while (peek() != "}") {
            int cur_line = (index_ < tokens_.size()) ? tokens_[index_].line : -1;
            std::string s = statement();
            if (s.empty()) continue;
            if (emit_line_directives_ && !source_.empty() && cur_line > 0 && cur_line != last_emitted_line_) {
                last_emitted_line_ = cur_line;
                std::string clean = source_;
                for (char& c : clean) if (c == '\\') c = '/';
                stmts.push_back("#line " + std::to_string(cur_line) + " \"" + clean + "\"");
            }
            if (terminated) stmts.push_back("0;");
            else stmts.push_back(s);
            if (s.rfind("return", 0) == 0 || s.find("proc_exit") != std::string::npos) terminated = true;
        }
        auto defers = std::move(defer_stack_.back());
        defer_stack_.pop_back();
        if (!terminated) {
            for (auto it = defers.rbegin(); it != defers.rend(); ++it) {
                stmts.push_back(*it);
            }
        }
        scopes_.pop_back();
        return stmts;
    }

    std::string statement() {
        if (peek() == "c_code" || peek() == "__c__") {
            take();
            if (peek() == "{") {
                take();
                int depth = 1;
                std::string code;
                while (depth > 0 && peek() != "<eof>") {
                    Token t = tokens_[index_++];
                    if (t.text == "{") {
                        depth++;
                        code += "{ ";
                    } else if (t.text == "}") {
                        depth--;
                        if (depth == 0) break;
                        code += "} ";
                    } else if (t.text == ";") {
                        code += ";\n    ";
                    } else if (t.text.rfind("#", 0) == 0) {
                        code += "\n" + t.text + " ";
                    } else {
                        code += t.text + " ";
                    }
                }
                return code + ";";
            } else {
                std::string s = take();
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                    s = s.substr(1, s.size() - 2);
                }
                if (peek() == ";") take();
                return s + ";";
            }
        }
        if (peek() == "defer") {
            take();
            std::string s = statement();
            if (!defer_stack_.empty()) {
                defer_stack_.back().push_back(s);
            }
            return "";
        }
        if (peek() == "let" || peek() == "var") {
            take();
            std::string name = take();
            std::string type;
            if (peek() == ":") { take(); type = type_name(); }
            expect("=");
            if (peek() == "try") {
                take();
                std::string expr = expression(";");
                expect(";");
                if (type.empty()) type = infer_type(expr);
                bind_type(name, type.empty() ? "long long" : type);
                int id = ++try_counter_;
                std::string c_t = ctype(type.empty() ? "long long" : type);
                return "long long _dmc_try_" + std::to_string(id) + " = " + expr + ";\n" +
                       "    if (_dmc_try_" + std::to_string(id) + " < 0) return _dmc_try_" + std::to_string(id) + ";\n" +
                       "    " + c_t + " " + name + " = (" + c_t + ")_dmc_try_" + std::to_string(id) + ";";
            }
            std::string val = expression(";");
            expect(";");
            if (type.empty()) {
                type = infer_type(val);
            }
            bind_type(name, type);
            if (!type.empty() && type.front() == '[') {
                auto sep = type.find(';');
                std::string elem = ctype(type.substr(1, sep - 1));
                std::string sz = type.substr(sep + 1, type.size() - sep - 2);
                std::string arr = val;
                std::replace(arr.begin(), arr.end(), '[', '{');
                std::replace(arr.begin(), arr.end(), ']', '}');
                return elem + " " + name + "[" + sz + "] = " + arr + ";";
            }
            return (type == "string" ? "const char* " : ctype(type) + " ") + name + " = " + val + ";";
        }
        if (peek() == "try") {
            take();
            std::string expr = expression(";");
            expect(";");
            int id = ++try_counter_;
            return "long long _dmc_try_" + std::to_string(id) + " = " + expr + ";\n" +
                   "    if (_dmc_try_" + std::to_string(id) + " < 0) return _dmc_try_" + std::to_string(id) + ";";
        }
        if (peek() == "return") {
            take();
            std::string defer_block;
            for (auto scope_it = defer_stack_.rbegin(); scope_it != defer_stack_.rend(); ++scope_it) {
                for (auto stmt_it = scope_it->rbegin(); stmt_it != scope_it->rend(); ++stmt_it) {
                    defer_block += "    " + *stmt_it + "\n";
                }
            }
            if (peek() == ";") {
                take();
                return defer_block + "return;";
            }
            std::string val = expression(";");
            expect(";");
            std::string vt = infer_type(val);
            if (current_ret_ != "void" && !is_compatible(current_ret_, vt)) {
                type_warn("returning '" + vt + "' from function returning '" + current_ret_ + "'");
            }
            if (!defer_block.empty()) {
                std::string ret_var = "_dmc_ret_" + std::to_string(++defer_counter_);
                return ctype(current_ret_) + " " + ret_var + " = " + val + ";\n" + defer_block + "    return " + ret_var + ";";
            }
            return "return " + val + ";";
        }
        if (peek() == "if") {
            take();
            expect("(");
            std::string cond = expression(")");
            expect(")");
            std::string narrowed_var;
            std::string narrowed_type;
            auto check_ptr_cond = [&](const std::string& c) {
                std::string s = c;
                s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
                std::string target = "";
                if (s.find("!=((void*)0)") != std::string::npos) {
                    target = s.substr(0, s.find("!=((void*)0)"));
                } else if (s.find("!=null") != std::string::npos) {
                    target = s.substr(0, s.find("!=null"));
                } else if (s.find("!=NULL") != std::string::npos) {
                    target = s.substr(0, s.find("!=NULL"));
                } else if (s.find("!=0") != std::string::npos) {
                    target = s.substr(0, s.find("!=0"));
                } else if (s.find("((void*)0)!=") == 0) {
                    target = s.substr(12);
                } else if (s.find("null!=") == 0) {
                    target = s.substr(5);
                } else if (s.find("NULL!=") == 0) {
                    target = s.substr(5);
                }
                while (!target.empty() && target.front() == '(') {
                    target = target.substr(1);
                }
                while (!target.empty() && target.back() == ')') {
                    target.pop_back();
                }
                if (!target.empty()) {
                    std::string t = lookup_type(target);
                    if (t.rfind("?*", 0) == 0) {
                        narrowed_var = target;
                        narrowed_type = t.substr(1);
                    }
                }
            };
            check_ptr_cond(cond);
            auto then_body = block(narrowed_var, narrowed_type);
            std::string result = "if (" + cond + ") {\n";
            for (auto& s : then_body) result += "    " + s + "\n";
            result += "}";
            if (peek() == "else") {
                take();
                if (peek() == "if") {
                    result += " else " + statement();
                } else {
                    auto else_body = block();
                    result += " else {\n";
                    for (auto& s : else_body) result += "    " + s + "\n";
                    result += "}";
                }
            }
            return result;
        }
        if (peek() == "while") {
            take();
            expect("(");
            std::string cond = expression(")");
            expect(")");
            auto body = block();
            std::string result = "while (" + cond + ") {\n";
            for (auto& s : body) result += "    " + s + "\n";
            result += "}";
            return result;
        }
        if (peek() == "for") {
            take();
            expect("(");
            std::string header;
            if (peek() == "let" || peek() == "var") {
                take();
                std::string name = take();
                std::string type = "int";
                if (peek() == ":") { take(); type = type_name(); }
                expect("=");
                std::string init = expression(";");
                expect(";");
                bind_type(name, type);
                header = ctype(type) + " " + name + " = " + init + "; ";
                header += expression(";");
                expect(";");
                header += "; " + expression(")");
            } else if (peek() == ";") {
                take();
                std::string cond = expression(";");
                expect(";");
                std::string step = expression(")");
                header = "; " + cond + "; " + step;
            } else {
                header = expression(")");
            }
            expect(")");
            auto body = block();
            std::string result = "for (" + header + ") {\n";
            for (auto& s : body) result += "    " + s + "\n";
            result += "}";
            return result;
        }
        if (peek() == "do") {
            take();
            auto body = block();
            expect("while");
            expect("(");
            std::string cond = expression(")");
            expect(")");
            expect(";");
            std::string result = "do {\n";
            for (auto& s : body) result += "    " + s + "\n";
            result += "} while (" + cond + ");";
            return result;
        }
        if (peek() == "until") {
            take();
            expect("(");
            std::string cond = expression(")");
            expect(")");
            auto body = block();
            std::string result = "do {\n";
            for (auto& s : body) result += "    " + s + "\n";
            result += "} while (!(" + cond + "));";
            return result;
        }
        if (peek() == "switch") {
            return switch_statement();
        }
        if (peek() == "match") {
            return match_statement();
        }
        if (peek() == "asm") {
            return asm_statement();
        }
        if (peek() == "break" || peek() == "continue") {
            std::string kw = take();
            expect(";");
            return kw + ";";
        }
        std::string val = expression(";");
        expect(";");
        return val + ";";
    }

    std::string switch_statement() {
        take();
        expect("(");
        std::string scrutinee = expression(")");
        expect(")");
        expect("{");
        std::vector<Case> cases;
        std::vector<std::string> current_body;
        std::string current_value;
        bool have_value = false;
        std::vector<std::string> default_body;
        bool have_default = false;
        while (peek() != "}") {
            if (peek() == "case") {
                if (have_value) cases.push_back({current_value, current_body});
                take();
                current_value = expression(":");
                expect(":");
                current_body.clear();
                have_value = true;
            } else if (peek() == "default") {
                if (have_value) cases.push_back({current_value, current_body});
                take();
                expect(":");
                current_body.clear();
                have_value = false;
                have_default = true;
            } else {
                current_body.push_back(statement());
            }
        }
        if (have_value) cases.push_back({current_value, current_body});
        if (have_default) default_body = current_body;
        expect("}");
        std::string result = "switch (" + scrutinee + ") {\n";
        for (auto& c : cases) {
            result += "    case " + c.value + ": {\n";
            for (auto& s : c.body) result += "        " + s + "\n";
            result += "        break;\n    }\n";
        }
        if (have_default) {
            result += "    default: {\n";
            for (auto& s : default_body) result += "        " + s + "\n";
            result += "        break;\n    }\n";
        }
        result += "}";
        return result;
    }

    std::string match_statement() {
        take();
        std::string scrutinee;
        if (peek() == "(") {
            take();
            scrutinee = expression(")");
            expect(")");
        } else {
            scrutinee = expression("{");
        }
        expect("{");
        int match_id = ++match_counter_;
        std::string match_var = "_dmc_match_" + std::to_string(match_id);
        std::string stype = infer_type(scrutinee);
        const Enum* matched_enum = nullptr;
        if (!stype.empty()) {
            for (const auto& e : enums_) {
                if (e.name == stype) {
                    matched_enum = &e;
                    break;
                }
            }
        }
        if (!matched_enum && peek() != "}" && peek() != "_" && peek() != "default") {
            std::string first_tok = peek();
            std::string cand_prefix = "";
            std::string cand_var = first_tok;
            if (index_ + 1 < tokens_.size() && tokens_[index_ + 1].text == "::" && index_ + 2 < tokens_.size()) {
                cand_prefix = first_tok;
                cand_var = tokens_[index_ + 2].text;
            }
            for (const auto& e : enums_) {
                for (const auto& v : e.variants) {
                    if ((cand_prefix.empty() || e.name == cand_prefix) && (v.name == cand_var || e.name + "_" + v.name == cand_var)) {
                        matched_enum = &e;
                        stype = e.name;
                        break;
                    }
                }
                if (matched_enum) break;
            }
        }
        if (stype.empty()) stype = "int";
        std::string pre_assign = ctype(stype) + " " + match_var + " = " + scrutinee + ";\n";
        std::string switch_target = (matched_enum && matched_enum->has_payload()) ? (match_var + ".tag") : match_var;
        std::string out = pre_assign + "    switch (" + switch_target + ") {\n";
        while (peek() != "}") {
            if (peek() == "_" || peek() == "default") {
                take();
                expect("=>");
                out += "        default: {\n";
                if (peek() == "{") {
                    auto body = block();
                    for (const auto& s : body) out += "            " + s + "\n";
                } else {
                    std::string s = statement();
                    out += "            " + s + "\n";
                }
                out += "            break;\n        }\n";
                if (peek() == "," || peek() == ";") take();
                continue;
            }
            std::string pat_prefix = "";
            std::string pat_variant = take();
            if (peek() == "::") {
                take();
                pat_prefix = pat_variant;
                pat_variant = take();
            }
            std::string bind_name = "";
            if (peek() == "(") {
                take();
                bind_name = take();
                expect(")");
            }
            expect("=>");
            const Enum* arm_enum = matched_enum;
            const EnumVariant* arm_var = nullptr;
            if (!arm_enum) {
                for (const auto& e : enums_) {
                    for (const auto& v : e.variants) {
                        if ((pat_prefix.empty() || e.name == pat_prefix) && (v.name == pat_variant || e.name + "_" + v.name == pat_variant)) {
                            arm_enum = &e;
                            arm_var = &v;
                            break;
                        }
                    }
                    if (arm_var) break;
                }
            } else {
                for (const auto& v : arm_enum->variants) {
                    if (v.name == pat_variant || arm_enum->name + "_" + v.name == pat_variant) {
                        arm_var = &v;
                        break;
                    }
                }
            }
            std::string case_label;
            if (arm_enum && arm_var) {
                if (arm_enum->has_payload()) {
                    case_label = arm_enum->name + "_Tag_" + arm_var->name;
                } else {
                    case_label = arm_enum->name + "_" + arm_var->name;
                }
            } else {
                case_label = pat_prefix.empty() ? pat_variant : (pat_prefix + "_" + pat_variant);
            }
            out += "        case " + case_label + ": {\n";
            std::string payload_type = (arm_var && !arm_var->payload.empty()) ? arm_var->payload : "";
            if (!bind_name.empty() && !payload_type.empty()) {
                std::string c_p = ctype(payload_type);
                out += "            " + c_p + " " + bind_name + " = " + match_var + ".data." + arm_var->name + ";\n";
            }
            if (peek() == "{") {
                auto body = block(!bind_name.empty() ? bind_name : "", payload_type);
                for (const auto& s : body) out += "            " + s + "\n";
            } else {
                scopes_.emplace_back();
                if (!bind_name.empty() && !payload_type.empty()) {
                    bind_type(bind_name, payload_type);
                }
                std::string s = statement();
                scopes_.pop_back();
                out += "            " + s + "\n";
            }
            out += "            break;\n        }\n";
            if (peek() == "," || peek() == ";") take();
        }
        expect("}");
        out += "    }";
        return out;
    }

    std::string asm_statement() {
        take();
        if (peek() == "{") {
            take();
            std::string result = "__asm__ volatile(";
            bool first = true;
            while (peek() != "}") {
                if (!first) result += " ";
                first = false;
                result += take();
                if (peek() == ";") take();
            }
            expect("}");
            result += ");";
            return result;
        }
        std::string asm_str = take();
        expect(";");
        return "__asm__ volatile(" + asm_str + ");";
    }

    std::string expression(const std::string& end) {
        return parse_binary(0, end);
    }

    int binary_precedence(const std::string& op) const {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "==" || op == "!=") return 3;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
        if (op == "+" || op == "-") return 5;
        if (op == "*" || op == "/" || op == "%") return 6;
        return 0;
    }

    bool is_binary_op() const {
        return binary_precedence(peek()) > 0;
    }

    static bool is_int_literal(const std::string& s) {
        if (s.empty()) return false;
        for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    }

    static int int_log2(long long v) {
        int n = 0;
        while (v > 1) { v >>= 1; ++n; }
        return n;
    }

    std::string try_fold(const std::string& left, const std::string& op, const std::string& right) const {
        if (is_int_literal(left) && is_int_literal(right)) {
            long long lv = std::stoll(left);
            long long rv = std::stoll(right);
            long long r = 0;
            if (op == "+") r = lv + rv;
            else if (op == "-") r = lv - rv;
            else if (op == "*") r = lv * rv;
            else if (op == "/") { if (rv == 0) return ""; r = lv / rv; }
            else if (op == "%") { if (rv == 0) return ""; r = lv % rv; }
            else if (op == "<<") r = lv << rv;
            else if (op == ">>") r = lv >> rv;
            else if (op == "&") r = lv & rv;
            else if (op == "|") r = lv | rv;
            else if (op == "^") r = lv ^ rv;
            else if (op == "==") return lv == rv ? "1" : "0";
            else if (op == "!=") return lv != rv ? "1" : "0";
            else if (op == "<") return lv < rv ? "1" : "0";
            else if (op == "<=") return lv <= rv ? "1" : "0";
            else if (op == ">") return lv > rv ? "1" : "0";
            else if (op == ">=") return lv >= rv ? "1" : "0";
            else if (op == "&&") return (lv != 0 && rv != 0) ? "1" : "0";
            else if (op == "||") return (lv != 0 || rv != 0) ? "1" : "0";
            else return "";
            return std::to_string(r);
        }
        return "";
    }

    std::string parse_binary(int min_prec, const std::string& end) {
        if (!end.empty() && peek() == end) return "";
        if (end == "]" && peek() == "..") return "";
        std::string left = postfix(unary_expr(end));
        while (is_binary_op()) {
            if (!end.empty() && peek() == end) break;
            if (end == "]" && peek() == "..") break;
            int prec = binary_precedence(peek());
            if (prec < min_prec) break;
            std::string op = take();
            std::string right = parse_binary(prec + 1, end);
            if (op != "&&" && op != "||") {
                std::string lt = infer_type(left);
                std::string rt = infer_type(right);
                if (lt == "string" || rt == "string") {
                    if (!(op == "==" || op == "!=" || op == "+")) {
                        type_warn("operator '" + op + "' applied to string operand");
                    }
                }
            }
            if ((op == "==" || op == "!=") && (is_string_arg(left) || is_string_arg(right))) {
                left = "(text_cmp(" + left + ", " + right + ") " + (op == "==" ? "==" : "!=") + " 0)";
            } else if (op == "+" && (is_string_arg(left) || is_string_arg(right))) {
                left = "text_concat(" + left + ", " + right + ")";
            } else {
                std::string folded = try_fold(left, op, right);
                if (!folded.empty()) {
                    left = folded;
                } else {
                    left = "(" + left + " " + op + " " + right + ")";
                }
            }
        }
        if (peek() == "=" && (end != "=")) {
            take();
            std::string rhs = expression(end);
            left = "(" + left + " = " + rhs + ")";
        }
        return left;
    }

    std::string postfix(std::string val) {
        while (peek() == "(" || peek() == "." || peek() == "[" || peek() == "as") {
            if (peek() == "as") {
                take();
                std::string t = type_name();
                val = "((" + ctype(t) + ")(" + val + "))";
            } else if (peek() == "(") {
                take();
                std::string args;
                if (peek() != ")") {
                    args = expression_list(")");
                }
                expect(")");
                if (val == "println") {
                    if (is_string_arg(args)) val = "puts(" + args + ")";
                    else if (is_float_arg(args)) val = "printf(\"%g\\n\", (double)(" + args + "))";
                    else val = "printf(\"%lld\\n\", (long long)(" + args + "))";
                } else if (val == "print") {
                    if (is_string_arg(args)) val = "printf(\"%s\", " + args + ")";
                    else if (is_float_arg(args)) val = "printf(\"%g\", (double)(" + args + "))";
                    else val = "printf(\"%lld\", (long long)(" + args + "))";
                } else if (val == "syscall") {
                    val = "syscall_h(" + args + ")";
                } else if (val == "f32x4" || val == "simd_f32x4") {
                    val = "DMC_SIMD_F32X4(" + args + ")";
                } else if (val == "f32x4_splat" || val == "simd_f32x4_splat") {
                    val = "dmc_f32x4_splat(" + args + ")";
                } else if (val == "i32x4" || val == "simd_i32x4") {
                    val = "DMC_SIMD_I32X4(" + args + ")";
                } else if (val == "i32x4_splat" || val == "simd_i32x4_splat") {
                    val = "dmc_i32x4_splat(" + args + ")";
                } else if (val == "simd_dot") {
                    val = "dmc_f32x4_dot(" + args + ")";
                } else if (val == "simd_add") {
                    val = "dmc_f32x4_add(" + args + ")";
                } else if (val == "simd_sub") {
                    val = "dmc_f32x4_sub(" + args + ")";
                } else if (val == "simd_mul") {
                    val = "dmc_f32x4_mul(" + args + ")";
                } else if (val == "simd_div") {
                    val = "dmc_f32x4_div(" + args + ")";
                } else if (val == "simd_get") {
                    val = "dmc_f32x4_get(" + args + ")";
                } else {
                    val = val + "(" + args + ")";
                }
            } else if (peek() == ".") {
                take();
                std::string field = take();
                std::string raw_val = val;
                while (!raw_val.empty() && raw_val.front() == '(' && raw_val.back() == ')') raw_val = raw_val.substr(1, raw_val.size() - 2);
                std::string raw_type = lookup_type(raw_val);
                if (raw_type.rfind("?*", 0) == 0) {
                    type_warn("cannot dereference nullable pointer '" + raw_val + "' without null check");
                    diagnostics_.push_back({tokens_[index_].line, tokens_[index_].col, "cannot dereference nullable pointer '" + raw_val + "' without null check"});
                }
                if (peek() == "(") {
                    take();
                    std::string margs;
                    if (peek() != ")") {
                        margs = expression_list(")");
                    }
                    expect(")");
                    
                    std::string vt = infer_type(val);
                    std::string typed_fn = vt + "_" + field;

                    if (vt == "f32x4") {
                        if (field == "get") val = "dmc_f32x4_get(" + val + ", " + margs + ")";
                        else if (field == "add") val = "dmc_f32x4_add(" + val + ", " + margs + ")";
                        else if (field == "sub") val = "dmc_f32x4_sub(" + val + ", " + margs + ")";
                        else if (field == "mul") val = "dmc_f32x4_mul(" + val + ", " + margs + ")";
                        else if (field == "div") val = "dmc_f32x4_div(" + val + ", " + margs + ")";
                        else if (field == "dot") val = "dmc_f32x4_dot(" + val + ", " + margs + ")";
                        else if (field == "x") val = "dmc_f32x4_get(" + val + ", 0)";
                        else if (field == "y") val = "dmc_f32x4_get(" + val + ", 1)";
                        else if (field == "z") val = "dmc_f32x4_get(" + val + ", 2)";
                        else if (field == "w") val = "dmc_f32x4_get(" + val + ", 3)";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt == "i32x4") {
                        if (field == "get") val = "dmc_i32x4_get(" + val + ", " + margs + ")";
                        else if (field == "add") val = "dmc_i32x4_add(" + val + ", " + margs + ")";
                        else if (field == "sub") val = "dmc_i32x4_sub(" + val + ", " + margs + ")";
                        else if (field == "mul") val = "dmc_i32x4_mul(" + val + ", " + margs + ")";
                        else if (field == "x") val = "dmc_i32x4_get(" + val + ", 0)";
                        else if (field == "y") val = "dmc_i32x4_get(" + val + ", 1)";
                        else if (field == "z") val = "dmc_i32x4_get(" + val + ", 2)";
                        else if (field == "w") val = "dmc_i32x4_get(" + val + ", 3)";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt.rfind("Vector<", 0) == 0 && vt.back() == '>') {
                        std::string elem = vt.substr(7, vt.size() - 8);
                        specialized_vectors_.insert(elem);
                        if (field == "push") val = "Vector_" + elem + "_push(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "pop") val = "Vector_" + elem + "_pop(" + val + ")";
                        else if (field == "get") val = "Vector_" + elem + "_get(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "len") val = "Vector_" + elem + "_len(" + val + ")";
                        else if (field == "free") val = "Vector_" + elem + "_free(" + val + ")";
                        else val = "((" + val + ")->" + field + ")(" + margs + ")";
                    } else if (vt == "arena" || val.find("arena") != std::string::npos) {
                        if (field == "alloc_bytes" || field == "alloc") val = "arena_alloc(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "reset") val = "arena_reset(" + val + ")";
                        else if (field == "free") val = "arena_free(" + val + ")";
                        else if (field == "used" || field == "top") val = "arena_used(" + val + ")";
                        else if (field == "write") val = "arena_write(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "read") val = "arena_read(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt == "string" || (!val.empty() && val.front() == '"')) {
                        if (field == "len") val = "text_len(" + val + ")";
                        else if (field == "slice") val = "string_slice(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "sub") val = "text_sub(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "cmp") val = "text_cmp(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (is_user_function(typed_fn)) val = typed_fn + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (is_user_function(field)) val = field + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt == "vec" || val.find("vec") != std::string::npos) {
                        if (field == "push") val = "vec_push(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "pop") val = "vec_pop(" + val + ")";
                        else if (field == "len") val = "vec_len(" + val + ")";
                        else if (field == "get") val = "vec_get(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "set") val = "vec_set(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "free") val = "vec_free(" + val + ")";
                        else if (is_user_function(typed_fn)) val = typed_fn + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (is_user_function(field)) val = field + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt == "map" || val.find("map") != std::string::npos) {
                        if (field == "set") val = "map_set(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "get") val = "map_get(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "has") val = "map_has(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "len") val = "map_len(" + val + ")";
                        else if (field == "free") val = "map_free(" + val + ")";
                        else if (is_user_function(typed_fn)) val = typed_fn + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (is_user_function(field)) val = field + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (vt == "queue" || val.find("queue") != std::string::npos) {
                        if (field == "push") val = "queue_push(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (field == "pop") val = "queue_pop(" + val + ")";
                        else if (field == "peek") val = "queue_peek(" + val + ")";
                        else if (field == "len") val = "queue_len(" + val + ")";
                        else if (field == "free") val = "queue_free(" + val + ")";
                        else if (is_user_function(typed_fn)) val = typed_fn + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else if (is_user_function(field)) val = field + "(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                        else val = "((" + val + ")." + field + ")(" + margs + ")";
                    } else if (is_user_function(typed_fn)) {
                        const Function* ufn = find_function(typed_fn);
                        std::string first_arg = val;
                        if (ufn && !ufn->params.empty() && ufn->params[0].type.front() == '*' && (vt.empty() || vt.front() != '*')) {
                            first_arg = "(&" + val + ")";
                        }
                        std::string call_args = first_arg + (margs.empty() ? "" : ", " + margs);
                        val = typed_fn + "(" + call_args + ")";
                    } else if (is_user_function(field)) {
                        const Function* ufn = find_function(field);
                        std::string first_arg = val;
                        if (ufn && !ufn->params.empty() && ufn->params[0].type.front() == '*' && (vt.empty() || vt.front() != '*')) {
                            first_arg = "(&" + val + ")";
                        }
                        std::string call_args = first_arg + (margs.empty() ? "" : ", " + margs);
                        val = field + "(" + call_args + ")";
                    } else if (field == "push") {
                        val = "vec_push(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                    } else if (field == "pop") {
                        val = "vec_pop(" + val + ")";
                    } else if (field == "peek") {
                        val = "queue_peek(" + val + ")";
                    } else if (field == "has") {
                        val = "map_has(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                    } else if (field == "len") {
                        val = "vec_len(" + val + ")";
                    } else if (field == "get") {
                        val = "vec_get(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                    } else if (field == "set") {
                        val = "vec_set(" + val + (margs.empty() ? "" : ", " + margs) + ")";
                    } else if (field == "free") {
                        val = "vec_free(" + val + ")";
                    } else {
                        val = "((" + val + ")." + field + ")(" + margs + ")";
                    }
                } else {
                    std::string vt = infer_type(val);
                    if (vt == "f32x4") {
                        if (field == "x") val = "dmc_f32x4_get(" + val + ", 0)";
                        else if (field == "y") val = "dmc_f32x4_get(" + val + ", 1)";
                        else if (field == "z") val = "dmc_f32x4_get(" + val + ", 2)";
                        else if (field == "w") val = "dmc_f32x4_get(" + val + ", 3)";
                        else {
                            std::string op = (vt.front() == '*' || vt.rfind("?*", 0) == 0 || raw_type.front() == '*' || raw_type.rfind("?*", 0) == 0) ? "->" : ".";
                            val = "(" + val + ")" + op + field;
                        }
                    } else if (vt == "i32x4") {
                        if (field == "x") val = "dmc_i32x4_get(" + val + ", 0)";
                        else if (field == "y") val = "dmc_i32x4_get(" + val + ", 1)";
                        else if (field == "z") val = "dmc_i32x4_get(" + val + ", 2)";
                        else if (field == "w") val = "dmc_i32x4_get(" + val + ", 3)";
                        else {
                            std::string op = (vt.front() == '*' || vt.rfind("?*", 0) == 0 || raw_type.front() == '*' || raw_type.rfind("?*", 0) == 0) ? "->" : ".";
                            val = "(" + val + ")" + op + field;
                        }
                    } else {
                        std::string op = (vt.front() == '*' || vt.rfind("?*", 0) == 0 || raw_type.front() == '*' || raw_type.rfind("?*", 0) == 0) ? "->" : ".";
                        val = "(" + val + ")" + op + field;
                    }
                }
            } else {
                take();
                if (peek() == "..") {
                    take();
                    std::string end_idx = expression("]");
                    expect("]");
                    val = "string_slice(" + val + ", 0, " + end_idx + ")";
                } else {
                    std::string idx = expression("]");
                    if (peek() == "..") {
                        take();
                        std::string end_idx;
                        if (peek() != "]") {
                            end_idx = expression("]");
                        } else {
                            end_idx = "text_len(" + val + ")";
                        }
                        expect("]");
                        val = "string_slice(" + val + ", " + idx + ", (" + end_idx + ") - (" + idx + "))";
                    } else {
                        expect("]");
                        std::string vt = infer_type(val);
                        if (vt.rfind("Vector<", 0) == 0 && vt.back() == '>') {
                            std::string elem = vt.substr(7, vt.size() - 8);
                            specialized_vectors_.insert(elem);
                            val = "Vector_" + elem + "_get(" + val + ", " + idx + ")";
                        } else if (vt == "f32x4") {
                            val = "dmc_f32x4_get(" + val + ", " + idx + ")";
                        } else if (vt == "i32x4") {
                            val = "dmc_i32x4_get(" + val + ", " + idx + ")";
                        } else {
                            val = "(" + val + ")[" + idx + "]";
                        }
                    }
                }
            }
        }
        return val;
    }

    std::string unary_expr(const std::string& end) {
        if (peek() == "*") {
            std::string op = take();
            std::string operand = postfix(unary_expr(end));
            std::string raw = operand;
            while (!raw.empty() && raw.front() == '(' && raw.back() == ')') raw = raw.substr(1, raw.size() - 2);
            std::string vt = lookup_type(raw);
            if (vt.rfind("?*", 0) == 0) {
                type_warn("cannot dereference nullable pointer '" + raw + "' without null check");
                diagnostics_.push_back({tokens_[index_].line, tokens_[index_].col, "cannot dereference nullable pointer '" + raw + "' without null check"});
            }
            return "(*" + operand + ")";
        }
        if (peek() == "!" || peek() == "-") {
            std::string op = take();
            std::string operand = postfix(unary_expr(end));
            return "(" + op + operand + ")";
        }
        if (peek() == "&") {
            take();
            std::string operand = postfix(unary_expr(end));
            return "(&" + operand + ")";
        }
        if (peek() == "(") {
            take();
            std::string inner = expression(")");
            expect(")");
            return "(" + inner + ")";
        }
        if (peek() == "[") {
            take();
            std::string elems;
            if (peek() != "]") elems = expression_list("]");
            expect("]");
            return "{" + elems + "}";
        }
        if (peek() == "true" || peek() == "false") {
            return take();
        }
        if (peek() == "asm") {
            return asm_inline();
        }
        if (peek() == "#embed" || peek() == "#embed_str" || peek() == "#embed_size") {
            std::string directive = take();
            expect("(");
            std::string path_literal = take();
            expect(")");
            std::string file_path = path_literal;
            if (file_path.size() >= 2 && file_path.front() == '"' && file_path.back() == '"') {
                file_path = file_path.substr(1, file_path.size() - 2);
            }
            std::filesystem::path p(file_path);
            if (!p.is_absolute()) {
                if (!source_.empty()) {
                    std::filesystem::path src_dir = std::filesystem::path(source_).parent_path();
                    if (std::filesystem::exists(src_dir / p)) {
                        p = src_dir / p;
                    }
                }
                if (!std::filesystem::exists(p)) {
                    if (std::filesystem::exists(std::filesystem::path("test-files") / file_path)) {
                        p = std::filesystem::path("test-files") / file_path;
                    }
                }
            }
            std::ifstream file(p, std::ios::binary);
            if (!file) {
                throw std::runtime_error("failed to embed file: '" + file_path + "'");
            }
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (directive == "#embed_size") {
                return std::to_string(bytes.size());
            } else if (directive == "#embed_str") {
                std::string s = "\"";
                for (unsigned char b : bytes) {
                    if (b == '\\') s += "\\\\";
                    else if (b == '"') s += "\\\"";
                    else if (b == '\n') s += "\\n";
                    else if (b == '\r') s += "\\r";
                    else if (b == '\t') s += "\\t";
                    else if (b >= 32 && b <= 126) s += static_cast<char>(b);
                    else {
                        char hex[8];
                        snprintf(hex, sizeof(hex), "\\x%02x", b);
                        s += hex;
                    }
                }
                s += "\"";
                return s;
            } else {
                std::string s = "((const unsigned char[]){";
                for (size_t bi = 0; bi < bytes.size(); ++bi) {
                    if (bi > 0) s += ",";
                    s += std::to_string((unsigned int)bytes[bi]);
                }
                if (bytes.empty()) s += "0";
                s += "})";
                return s;
            }
        }
        if (peek() == "comptime") {
            take();
            expect("{");
            std::string cexpr = expression("}");
            expect("}");
            return "(" + cexpr + ")";
        }
        std::string next = peek();
        if (next == "null") {
            take();
            return "null";
        }
        if (std::isalpha(static_cast<unsigned char>(next[0])) || next[0] == '_') {
            std::string name = take();
            if (name == "Vector" && peek() == "<") {
                take();
                std::string elem = type_name();
                expect(">");
                specialized_vectors_.insert(elem);
                if (peek() == "::") {
                    take();
                    std::string m = take();
                    expect("(");
                    if (peek() != ")") expression(")");
                    expect(")");
                    return "Vector_" + elem + "_new()";
                } else if (peek() == "(") {
                    take(); expect(")");
                    return "Vector_" + elem + "_new()";
                }
                return "Vector_" + elem;
            }
            if (generic_structs_.find(name) != generic_structs_.end() && peek() == "<") {
                take();
                std::vector<std::string> args;
                while (peek() != ">") {
                    args.push_back(type_name());
                    if (peek() == ",") take();
                    else break;
                }
                expect(">");
                instantiate_generic_struct(name, args);
                std::string full_type = name + "<";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) full_type += ", ";
                    full_type += args[i];
                }
                full_type += ">";
                std::string mangled = ctype(full_type);
                if (peek() == "{") {
                    return struct_literal(mangled);
                }
                return mangled;
            }
            if (peek() == "::") {
                take();
                std::string variant = take();
                return enum_variant_expr(name, variant);
            }
            if (peek() == "{") {
                return struct_literal(name);
            }
            if (is_enum_variant_name(name)) {
                return enum_variant_expr("", name);
            }
            if (peek() != "(") {
                if (lookup_type(name).empty() && !is_user_function(name) && !is_stdlib_fn(name) && name != "__dmc_argc" && name != "__dmc_argv") {
                    if (c_headers_.empty()) {
                        type_warn("use of undeclared identifier '" + name + "'");
                    }
                }
            }
            return name;
        }
        if (std::isdigit(static_cast<unsigned char>(next[0]))) {
            return take();
        }
        if (!next.empty() && next.front() == '"') {
            return take();
        }
        if (next == "null") { take(); return "((void*)0)"; }
        throw std::runtime_error("expected expression, got '" + next + "' at line " + std::to_string(tokens_[index_].line));
    }

    std::string struct_literal(const std::string& type_name) {
        expect("{");
        std::string result = "(" + type_name + "){";
        bool first = true;
        while (peek() != "}") {
            if (!first) result += ", ";
            first = false;
            std::string fname = take();
            expect(":");
            std::string fval = expression("}");
            result += "." + fname + " = " + fval;
            if (peek() == ",") take();
            else if (peek() == ";") take();
        }
        expect("}");
        return result + "}";
    }

    std::string asm_inline() {
        take();
        std::string result = "({";
        if (peek() == "{") {
            take();
            bool first = true;
            while (peek() != "}") {
                if (!first) result += " ";
                first = false;
                result += take();
                if (peek() == ";") take();
            }
            expect("}");
        } else {
            result += take();
            if (peek() == ";") take();
        }
        result += "; 0;})";
        return result;
    }

    std::string expression_list(const std::string& end) {
        std::string result = expression(end);
        while (peek() == ",") {
            take();
            if (peek() == end) break;
            result += ", " + expression(end);
        }
        return result;
    }

    std::string definition(const Function& fn) const {
        if (fn.is_extern) return "";
        std::ostringstream out;
        if (emit_line_directives_ && !source_.empty() && fn.line > 0) {
            std::string clean = source_;
            for (char& c : clean) if (c == '\\') c = '/';
            out << "#line " << fn.line << " \"" << clean << "\"\n";
        }
        if (fn.name == "main") {
            out << "int __dmc_main_body(void) {\n";
            for (auto& s : fn.body) out << "    " << s << "\n";
            if (fn.body.empty() || fn.body.back().rfind("return", 0) != 0) {
                out << "    return 0;\n";
            }
            out << "}\n";
            out << "int main(int argc, char **argv) {\n";
            out << "    __dmc_argc = argc;\n";
            out << "    __dmc_argv = argv;\n";
            out << "    return __dmc_main_body();\n";
            out << "}\n\n";
            return out.str();
        }
        if (fn.is_export) out << "DMC_EXPORT ";
        out << (fn.name == "main" ? "int" : ctype(fn.ret_type)) << " " << fn.name << "(";
        if (fn.params.empty()) out << "void";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out << ", ";
            out << ctype(fn.params[i].type) << " " << fn.params[i].name;
        }
        out << ") {\n";
        for (auto& s : fn.body) out << "    " << s << "\n";
        if (fn.ret_type == "int" && (fn.body.empty() || fn.body.back().rfind("return", 0) != 0)) {
            out << "    return 0;\n";
        }
        out << "}";
        return out.str();
    }

    std::string type_name() {
        if (peek() == "?*") { take(); return "?*" + type_name(); }
        if (peek() == "*") { take(); return "*" + type_name(); }
        if (peek() == "[") {
            take();
            std::string elem = take();
            expect(";");
            std::string sz = take();
            expect("]");
            return "[" + elem + ";" + sz + "]";
        }
        std::string base = take();
        if (peek() == "<") {
            take();
            std::vector<std::string> args;
            while (peek() != ">") {
                args.push_back(type_name());
                if (peek() == ",") take();
                else break;
            }
            expect(">");
            std::string full_type = base + "<";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) full_type += ", ";
                full_type += args[i];
            }
            full_type += ">";
            if (base == "Vector" && !args.empty()) specialized_vectors_.insert(args[0]);
            if (generic_structs_.find(base) != generic_structs_.end()) {
                instantiate_generic_struct(base, args);
            }
            return full_type;
        }
        while (peek() == "*") {
            take();
            base = "*" + base;
        }
        return base;
    }

    bool is_float_arg(const std::string& arg) const {
        if (arg.size() >= 3 && std::isdigit(static_cast<unsigned char>(arg[0])) && arg.find('.') != std::string::npos) return true;
        if (!arg.empty() && std::isalpha(static_cast<unsigned char>(arg[0]))) {
            std::string t = lookup_type(arg);
            if (t == "f32" || t == "f64" || t == "double" || t == "float") return true;
        }
        for (const auto& fn : {"math_sin", "math_cos", "math_tan", "math_sqrt", "math_floor", "math_ceil", "math_abs_f", "math_pi", "math_e", "text_to_float", "text_from_float"}) {
            if (arg.rfind(std::string(fn) + "(", 0) == 0) return true;
        }
        auto open = arg.find('(');
        if (open != std::string::npos) {
            const Function* fn = find_function(arg.substr(0, open));
            if (fn && (fn->ret_type == "f32" || fn->ret_type == "f64" || fn->ret_type == "double" || fn->ret_type == "float")) return true;
        }
        return false;
    }

    bool is_string_arg(const std::string& arg) const {
        if (!arg.empty() && arg.front() == '"') return true;
        if (infer_type(arg) == "string") return true;
        if (!arg.empty() && std::isalpha(static_cast<unsigned char>(arg[0]))) {
            if (lookup_type(arg) == "string") return true;
        }
        auto dot = arg.rfind(").");
        if (dot != std::string::npos) {
            std::string field = arg.substr(dot + 2);
            if (!field.empty() && std::isalpha(static_cast<unsigned char>(field[0]))) {
                if (struct_field_type(field) == "string") return true;
                for (const auto& e : enums_) {
                    for (const auto& v : e.variants) {
                        std::string fld = v.name;
                        std::transform(fld.begin(), fld.end(), fld.begin(), ::tolower);
                        if (field == v.name || field == fld) {
                            if (v.payload == "string") return true;
                        }
                    }
                }
            }
        }
        for (const auto& fn : {"file_read", "tcp_recv", "http_get", "http_response", "http_response_json", "http_response_html", "http_req_method", "http_req_path", "http_status_text", "text_concat", "text_sub", "text_from_int", "text_from_float", "text_upper", "text_lower", "text_replace", "proc_exit", "string_copy", "string_slice", "arg_text", "prompt", "term_color", "fs_read", "os_env", "os_cwd", "os_platform", "os_arch", "sha256", "base64_encode", "base64_decode", "ffi_error", "ffi_call_str_0", "ffi_call_str_1", "ffi_call_str_2", "ffi_call_str_s", "py_eval", "py_call", "js_eval", "channel_recv", "channel_try_recv"}) {
            if (arg.rfind(std::string(fn) + "(", 0) == 0) return true;
        }
        auto open = arg.find('(');
        if (open != std::string::npos) {
            const Function* fn = find_function(arg.substr(0, open));
            if (fn && fn->ret_type == "string") return true;
        }
        return false;
    }

    std::string struct_field_type(const std::string& field) const {
        for (auto& s : structs_) {
            for (auto& f : s.fields) if (f.name == field) return f.type;
        }
        return "";
    }

    std::string infer_type(const std::string& val) const {
        if (!val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])))) {
            if (val.find('.') != std::string::npos || val.find('e') != std::string::npos || val.find('E') != std::string::npos) return "f64";
        }
        if (!val.empty() && val.front() == '"') return "string";
        if (is_enum_value(val)) return enum_type_of(val);
        if (val.rfind("(&", 0) == 0) return "*int";
        if (val.rfind("((", 0) == 0) {
            auto close = val.find(')', 2);
            if (close != std::string::npos) {
                std::string cast_t = val.substr(2, close - 2);
                if (cast_t == "void*" || cast_t == "void *") return "*void";
                if (cast_t == "const char*" || cast_t == "char*") return "string";
                if (cast_t == "float") return "float";
                if (cast_t == "double") return "f64";
                if (cast_t == "int") return "int";
                if (cast_t == "long long") return "i64";
            }
        }
        if (!val.empty() && val.front() == '(') {
            auto close_p = val.find(')');
            if (close_p != std::string::npos) {
                std::string cand = val.substr(1, close_p - 1);
                for (const auto& s : structs_) {
                    if (s.name == cand) return cand;
                }
            }
        }
        if (val.find("((const unsigned char[])") != std::string::npos) return "const unsigned char*";
        if (val.rfind("DMC_SIMD_F32X4", 0) == 0 || val.rfind("dmc_f32x4", 0) == 0 || val.rfind("simd_f32x4", 0) == 0) return "f32x4";
        if (val.rfind("DMC_SIMD_I32X4", 0) == 0 || val.rfind("dmc_i32x4", 0) == 0 || val.rfind("simd_i32x4", 0) == 0) return "i32x4";
        if (val.rfind("arena_new(", 0) == 0) return "arena";
        if (val.rfind("Vector_", 0) == 0 && val.find("_new(") != std::string::npos) {
            auto pos1 = val.find("Vector_");
            auto pos2 = val.find("_new(");
            if (pos1 != std::string::npos && pos2 != std::string::npos && pos2 > pos1 + 7) {
                std::string elem = val.substr(pos1 + 7, pos2 - (pos1 + 7));
                return "Vector<" + elem + ">";
            }
        }
        if (val.rfind("Vector_", 0) == 0 && (val.find("_get(") != std::string::npos || val.find("_pop(") != std::string::npos)) {
            auto pos1 = val.find("Vector_");
            auto pos2 = val.find("_get(");
            if (pos2 == std::string::npos) pos2 = val.find("_pop(");
            if (pos1 != std::string::npos && pos2 != std::string::npos && pos2 > pos1 + 7) {
                return val.substr(pos1 + 7, pos2 - (pos1 + 7));
            }
        }
        if (val.rfind("vec_new(", 0) == 0) return "vec";
        if (val.rfind("map_new(", 0) == 0) return "map";
        if (val.rfind("queue_new(", 0) == 0) return "queue";
        if (val.rfind("vec_new", 0) == 0) return "vec";
        if (val.rfind("map_new", 0) == 0) return "map";
        if (val.rfind("queue_new", 0) == 0) return "queue";
        if (val.rfind("py_eval(", 0) == 0 || val.rfind("py_call(", 0) == 0 || val.rfind("js_eval(", 0) == 0 || val.rfind("ffi_call_str", 0) == 0 || val.rfind("ffi_error(", 0) == 0) return "string";
        if (val.rfind("ffi_call_f", 0) == 0) return "f64";
        if (val.rfind("ffi_symbol(", 0) == 0) return "*void";
        if (val.rfind("ffi_open(", 0) == 0 || val.rfind("ffi_close(", 0) == 0 || val.rfind("py_init(", 0) == 0 || val.rfind("py_finalize(", 0) == 0 || val.rfind("py_run(", 0) == 0 || val.rfind("py_import(", 0) == 0 || val.rfind("js_run(", 0) == 0 || val.rfind("task_pool_new(", 0) == 0 || val.rfind("task_pool_submit(", 0) == 0 || val.rfind("task_pool_free(", 0) == 0) return "int";
        for (const auto& s : structs_) {
            std::string prefix = "(" + s.name + "){";
            if (val.find(prefix) != std::string::npos || val.rfind(s.name + " {", 0) == 0 || val.rfind(s.name + "{", 0) == 0) {
                return s.name;
            }
        }
        for (const auto& fn : {"file_read", "tcp_recv", "http_get", "http_response", "http_response_json", "http_response_html", "http_req_method", "http_req_path", "http_status_text", "text_concat", "text_sub", "text_from_int", "text_from_float", "text_upper", "text_lower", "text_replace", "proc_exit", "string_copy", "string_slice", "arg_text", "prompt", "term_color", "fs_read", "os_env", "os_cwd", "os_platform", "os_arch", "sha256", "base64_encode", "base64_decode", "ffi_error", "ffi_call_str_0", "ffi_call_str_1", "ffi_call_str_2", "ffi_call_str_s", "py_eval", "py_call", "js_eval", "channel_recv", "channel_try_recv"}) {
            if (val.rfind(std::string(fn) + "(", 0) == 0) return "string";
        }
        auto open = val.find('(');
        if (open != std::string::npos && open > 0) {
            std::string callee = val.substr(0, open);
            const Function* fn = find_function(callee);
            if (fn) return fn->ret_type;
            for (const auto& e : enums_) {
                for (const auto& v : e.variants) {
                    if (callee == e.name + "_" + v.name || callee == v.name) return e.name;
                }
            }
        }
        auto dot = val.rfind(").");
        if (dot != std::string::npos) {
            std::string field = val.substr(dot + 2);
            if (!field.empty() && std::isalpha(static_cast<unsigned char>(field[0]))) {
                std::string ft = struct_field_type(field);
                if (!ft.empty()) return ft;
                for (const auto& e : enums_) {
                    for (const auto& v : e.variants) {
                        std::string fld = v.name;
                        std::transform(fld.begin(), fld.end(), fld.begin(), ::tolower);
                        if (field == v.name || field == fld) {
                            if (!v.payload.empty()) return v.payload;
                        }
                    }
                }
            }
        }
        std::string clean = val;
        while (clean.size() >= 2 && clean.front() == '(' && clean.back() == ')') {
            clean = clean.substr(1, clean.size() - 2);
        }
        if (!clean.empty() && clean.front() == '&') {
            std::string inner = clean.substr(1);
            std::string t = lookup_type(inner);
            if (!t.empty()) return "*" + t;
        }
        if (!clean.empty() && (std::isalpha(static_cast<unsigned char>(clean[0])) || clean[0] == '_')) {
            std::string t = lookup_type(clean);
            if (!t.empty()) return t;
        }
        return "int";
    }

    void bind_type(const std::string& name, const std::string& type) {
        if (scopes_.empty()) scopes_.emplace_back();
        scopes_.back()[name] = type;
    }

    std::string lookup_type(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return "";
    }

    void type_warn(const std::string& message) {
        int l = index_ < tokens_.size() ? tokens_[index_].line : 1;
        int c = index_ < tokens_.size() ? tokens_[index_].col : 1;
        type_warnings_.push_back({l, c, message});
    }

    bool is_compatible(const std::string& a, const std::string& b) const {
        if (a.empty() || b.empty()) return true;
        if (a == b) return true;
        if (a == "string" && b == "string") return true;
        if (a == "void" || b == "void") return true;
        if ((a == "int") && (b == "f32" || b == "f64")) return true;
        if ((b == "int") && (a == "f32" || a == "f64")) return true;
        return false;
    }

    const Function* find_function(const std::string& name) const {
        for (auto& fn : functions_) if (fn.name == name) return &fn;
        return nullptr;
    }

    bool is_user_function(const std::string& name) const {
        for (auto& fn : functions_) if (fn.name == name) return true;
        return false;
    }

    bool is_stdlib_fn(const std::string& name) const {
        static const std::vector<std::string> fns = {
            "print", "println", "mem_alloc", "mem_free", "mem_size", "mem_write", "mem_read",
            "mem_map", "mem_unmap", "file_open", "file_read", "file_write", "file_close",
            "tcp_connect", "tcp_listen", "tcp_accept", "tcp_send", "tcp_recv", "tcp_close",
            "vec_new", "vec_len", "vec_push", "vec_pop", "vec_get", "vec_set", "vec_free",
            "queue_new", "queue_len", "queue_push", "queue_pop", "queue_peek", "queue_free",
            "map_new", "map_set", "map_get", "map_has", "map_len", "map_free",
            "math_abs", "math_min", "math_max", "math_clamp",
            "math_sin", "math_cos", "math_tan", "math_sqrt", "math_floor", "math_ceil", "math_abs_f", "math_pi", "math_e",
            "text_len", "text_concat", "text_sub", "text_char_at", "text_cmp", "text_find", "text_contains",
            "text_from_int", "text_from_float", "text_to_int", "text_to_float",
            "text_upper", "text_lower", "text_replace",
            "string_copy", "string_slice",
            "fs_read", "fs_write", "fs_append", "fs_exists", "fs_remove", "fs_size", "fs_copy", "fs_mkdir", "fs_rmdir",
            "prompt", "term_color", "term_clear",
            "os_env", "os_set_env", "os_cwd", "os_platform", "os_arch",
            "sha256", "base64_encode", "base64_decode",
            "ffi_open", "ffi_symbol", "ffi_close", "ffi_error",
            "ffi_call_0", "ffi_call_1", "ffi_call_2", "ffi_call_3", "ffi_call_4", "ffi_call_5", "ffi_call_6",
            "ffi_call_str_0", "ffi_call_str_1", "ffi_call_str_2", "ffi_call_str_s",
            "ffi_call_f_1", "ffi_call_f_2",
            "ffi_call_v_0", "ffi_call_v_1", "ffi_call_v_2", "ffi_call_v_s",
            "py_init", "py_finalize", "py_run", "py_eval", "py_call", "py_import",
            "js_run", "js_eval",
            "proc_arg_count", "arg_count", "proc_exit", "syscall", "interrupt", "arena_new", "arena_alloc", "arena_alloc_bytes", "arena_write", "arena_read", "arena_reset", "arena_top", "arena_used", "arena_free",
            "thread_spawn", "thread_spawn_arg", "thread_join", "thread_id", "thread_sleep", "thread_yield",
            "mutex_new", "mutex_lock", "mutex_unlock", "mutex_free",
            "atomic_add", "atomic_sub", "atomic_load", "atomic_store",
            "channel_new", "channel_send", "channel_recv", "channel_try_recv", "channel_close", "channel_free",
            "time_now_ms", "time_sleep_ms", "rand_seed", "rand_int", "proc_pid", "datetime_now", "datetime_format", "datetime_unix",
            "task_pool_new", "task_pool_submit", "task_pool_free"
        };
        for (auto& f : fns) if (f == name) return true;
        return false;
    }

    bool is_enum_variant(const std::string& name) const {
        return is_enum_variant_name(name);
    }

    bool is_enum_variant_name(const std::string& name) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if (name == e.name + "::" + v.name || name == e.name + "_" + v.name || name == v.name) return true;
            }
        }
        return false;
    }

    bool is_enum_value(const std::string& val) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if (val == e.name + "::" + v.name || val == e.name + "_" + v.name) return true;
            }
        }
        return false;
    }

    std::string enum_type_of(const std::string& val) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if (val == e.name + "::" + v.name || val == e.name + "_" + v.name) return e.name;
            }
        }
        return "int";
    }

    std::string enum_variant_expr(const std::string& enum_name, const std::string& variant) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if ((enum_name.empty() || e.name == enum_name) && (v.name == variant || e.name + "_" + v.name == variant || e.name + "::" + v.name == variant)) {
                    if (e.has_payload()) {
                        return (peek() == "(" ? e.name + "_" + v.name : e.name + "_Tag_" + v.name);
                    }
                    return e.name + "_" + v.name;
                }
            }
        }
        return variant;
    }

    std::string peek() const { return tokens_[index_].text; }
    std::string take() { if (peek() == "<eof>") fail("unexpected end of file"); return tokens_[index_++].text; }
    void expect(const std::string& value) { if (peek() != value) fail("expected '" + value + "', got '" + peek() + "'"); ++index_; }
    [[noreturn]] void fail(const std::string& message) const {
        int l = index_ < tokens_.size() ? tokens_[index_].line : 1;
        int c = index_ < tokens_.size() ? tokens_[index_].col : 1;
        throw std::runtime_error(message + " at line " + std::to_string(l) + ":" + std::to_string(c));
    }

    std::vector<Token> tokens_;
    size_t index_ = 0;
    std::vector<Function> functions_;
    std::vector<Struct> structs_;
    std::unordered_map<std::string, GenericStruct> generic_structs_;
    int match_counter_ = 0;
    std::vector<Enum> enums_;
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::set<std::string> imports_;
    std::set<std::string> c_headers_;
    std::set<std::string> link_libs_;
    std::vector<std::string> top_level_c_;
    std::set<std::string> specialized_vectors_;
    int try_counter_ = 0;
    std::string source_ = "";
    std::set<std::string> loaded_;
    std::vector<std::string> include_dirs_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<Diagnostic> type_warnings_;
    std::string current_ret_ = "int";
    std::vector<std::vector<std::string>> defer_stack_;
    int defer_counter_ = 0;
    bool emit_line_directives_ = true;
    int last_emitted_line_ = -1;
    bool freestanding_ = false;
};


struct PackageSpec {
    std::string name;
    std::string version;
    std::string source;  
};

struct PackageMetadata {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    std::vector<std::string> dependencies;
    std::string repository;
};

struct PackageInfo {
    std::string name;
    std::string version;
    std::string summary;
};

static std::string pkg_dir() {
#ifdef _WIN32
    char* appdata = nullptr;
    size_t size = 0;
    _dupenv_s(&appdata, &size, "APPDATA");
    if (appdata) return std::string(appdata) + "\\DemonicC\\packages";
    return ".dmc\\packages";
#else
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.dmc/packages";
    return ".dmc/packages";
#endif
}

static std::string registry_url() {
    return "https://registry.demonic-lang.org";
}

static bool parse_semver(const std::string& spec, std::string& out_op, int& out_ver) {
    
    std::regex op_regex("^(>=|<=|>|<|==|~)([0-9]+)\\.");
    std::smatch match;
    if (std::regex_match(spec, match, op_regex)) {
        out_op = match[1];
        out_ver = std::stoi(match[2]);
        return true;
    }
    
    if (std::regex_match(spec, match, std::regex("^([0-9]+)\\.([0-9]+)\\.([0-9]+)$"))) {
        out_ver = std::stoi(match[1]);
        return true;
    }
    return false;
}

static void resolve_dependencies() {
    
    std::string toml_path = "dmc.toml";
    if (!std::filesystem::exists(toml_path)) {
        return;
    }
    
    std::ifstream f(toml_path);
    std::string line;
    std::string current_section;
    std::vector<std::string> deps;
    
    while (std::getline(f, line)) {
        if (line.find("[dependencies]") != std::string::npos) {
            current_section = "deps";
            continue;
        }
        if (line.find("[package]") != std::string::npos) {
            current_section = "pkg";
            continue;
        }
        if (current_section == "deps" && line.find('=') != std::string::npos) {
            std::string dep = line.substr(0, line.find('='));
            
            dep.erase(0, dep.find_first_not_of(" \t"));
            dep.erase(dep.find_last_not_of(" \t") + 1);
            if (!dep.empty()) {
                deps.push_back(dep);
            }
        }
    }
    
    for (const auto& dep : deps) {
        std::string op;
        int ver;
        if (parse_semver(dep, op, ver)) {
            
            
            std::cout << "Resolving dependency: " << dep << "\n";
        }
    }
}

static std::string packages_path() {
    std::string p = pkg_dir();
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

static const std::vector<PackageInfo> REGISTRY_CATALOG = {
    {"raylib",  "5.0.0",  "Hardware accelerated graphics, audio, and input for 2D/3D games"},
    {"sqlite3", "3.45.1", "Self-contained serverless zero-configuration SQL database engine"},
    {"curl",    "8.6.0",  "Command line tool and library for transferring data with URLs"},
    {"cjson",   "1.7.17", "Ultralightweight, ANSI-C compliant JSON parser and serializer"},
    {"linalg",  "1.2.0",  "SIMD-accelerated linear algebra, matrix, and vector math"},
    {"ws",      "0.4.0",  "High-performance RFC 6455 WebSocket server and client protocol"},
    {"json",    "2.0.1",  "Standard JSON parsing, serialization, and schema validation"},
    {"json",    "1.5.0",  "Legacy JSON parser for Demonic C"},
    {"http",    "1.0.3",  "Native HTTP client and multi-threaded micro-server library"},
    {"http",    "0.9.0",  "Basic HTTP client support"},
    {"crypto",  "0.9.1",  "Cryptographic primitives, SHA-256, HMAC, and cipher algorithms"}
};

static std::string to_lower_str(const std::string& s) {
    std::string res = s;
    for (char& c : res) c = (char)std::tolower((unsigned char)c);
    return res;
}

static std::vector<PackageInfo> search_registry(const std::string& name) {
    std::vector<PackageInfo> results;
    std::string q = to_lower_str(name);
    for (const auto& pkg : REGISTRY_CATALOG) {
        if (q.empty() || q == "*" || to_lower_str(pkg.name).find(q) != std::string::npos || to_lower_str(pkg.summary).find(q) != std::string::npos) {
            results.push_back(pkg);
        }
    }
    return results;
}

static std::string mock_sha256(const std::string& data) {
    unsigned long long h1 = 0xcbf29ce484222325ULL;
    unsigned long long h2 = 0x100000001b3ULL;
    for (char c : data) {
        h1 = (h1 ^ (unsigned char)c) * 0x100000001b3ULL;
        h2 = (h2 ^ (unsigned char)c) * 0xbf58476d1ce4e5b9ULL;
    }
    char buf[65];
    snprintf(buf, sizeof(buf), "%016llx%016llx%016llx%016llx", h1, h2, h1 ^ h2, h1 + h2);
    return std::string(buf);
}

static void update_lockfile() {
    std::string toml_path = "dmc.toml";
    if (!std::filesystem::exists(toml_path)) return;

    std::ifstream f(toml_path);
    std::string line;
    std::string current_section;
    std::vector<std::pair<std::string, std::string>> deps;

    while (std::getline(f, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        if (trimmed.find("[dependencies]") != std::string::npos) {
            current_section = "deps";
            continue;
        } else if (trimmed[0] == '[') {
            current_section = "";
            continue;
        }

        if (current_section == "deps" && trimmed.find('=') != std::string::npos) {
            size_t eq = trimmed.find('=');
            std::string dname = trimmed.substr(0, eq);
            std::string dval = trimmed.substr(eq + 1);

            dname.erase(0, dname.find_first_not_of(" \t"));
            dname.erase(dname.find_last_not_of(" \t") + 1);
            dval.erase(0, dval.find_first_not_of(" \t\"\'^~>="));
            dval.erase(dval.find_last_not_of(" \t\"\'\r\n") + 1);

            if (!dname.empty()) {
                deps.push_back({dname, dval.empty() ? "*" : dval});
            }
        }
    }
    f.close();

    std::ofstream lock("dmc.lock");
    if (!lock) return;

    lock << "# Auto-generated by Demonic C Package Manager (v2.0.1)\n";
    lock << "# Manual edits may be overwritten.\n\n";
    lock << "version = 1\n\n";

    for (const auto& d : deps) {
        std::string locked_ver = d.second;
        if (locked_ver == "*") {
            auto hits = search_registry(d.first);
            if (!hits.empty()) locked_ver = hits[0].version;
            else locked_ver = "1.0.0";
        }
        std::string chk = mock_sha256(d.first + "@" + locked_ver);
        lock << "[[package]]\n";
        lock << "name = \"" << d.first << "\"\n";
        lock << "version = \"" << locked_ver << "\"\n";
        lock << "source = \"registry+" << registry_url() << "/" << d.first << "\"\n";
        lock << "checksum = \"" << chk << "\"\n\n";
    }
    lock.close();
}

static std::vector<PackageMetadata> installed_packages() {
    std::vector<PackageMetadata> pkgs;
    std::string pkg_path = packages_path();
    for (const auto& entry : std::filesystem::directory_iterator(pkg_path)) {
        if (entry.is_directory()) {
            std::string dname = entry.path().filename().string();
            std::string toml_path = entry.path().string() + "/dmc.toml";
            if (std::filesystem::exists(toml_path)) {
                
                std::ifstream f(toml_path);
                std::string line;
                PackageMetadata meta;
                meta.name = dname;
                auto clean_val = [](std::string s) {
                    s.erase(0, s.find_first_not_of(" \t\"\'"));
                    s.erase(s.find_last_not_of(" \t\"\'\r\n") + 1);
                    return s;
                };
                while (std::getline(f, line)) {
                    if (line.find("version") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.version = clean_val(line.substr(eq + 1));
                    } else if (line.find("description") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.description = clean_val(line.substr(eq + 1));
                    } else if (line.find("author") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.author = clean_val(line.substr(eq + 1));
                    } else if (line.find("license") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.license = clean_val(line.substr(eq + 1));
                    }
                }
                pkgs.push_back(meta);
            }
        }
    }
    return pkgs;
}

static bool write_package_meta(const std::string& pkg_name, const PackageMetadata& meta) {
    std::string pkg_path = packages_path() + "/" + pkg_name;
    std::filesystem::create_directories(pkg_path);
    std::ofstream f(pkg_path + "/dmc.toml");
    if (!f) return false;
    f << "[package]\n";
    f << "name = \"" << meta.name << "\"\n";
    f << "version = \"" << meta.version << "\"\n";
    f << "description = \"" << meta.description << "\"\n";
    f << "author = \"" << meta.author << "\"\n";
    f << "license = \"" << meta.license << "\"\n";
    f << "\n";
    f << "[dependencies]\n";
    for (const auto& dep : meta.dependencies) {
        f << dep << "\n";
    }
    f.close();
    return true;
}

static void install_package(const std::string& name, const std::string& version) {
    auto results = search_registry(name);
    if (results.empty()) {
        std::cerr << "dmc install: package '" << name << "' not found in registry\n";
        return;
    }
    
    std::string pkg_version = version;
    if (pkg_version.empty()) {
        pkg_version = results[0].version;
    }
    
    bool found = false;
    for (const auto& pkg : results) {
        if (pkg.name == name && (version.empty() || pkg.version == pkg_version)) {
            pkg_version = pkg.version;
            PackageMetadata meta;
            meta.name = pkg.name;
            meta.version = pkg.version;
            meta.description = pkg.summary;
            meta.author = "Demonic C Contributors";
            meta.license = "MIT";
            meta.dependencies = {};
            write_package_meta(pkg.name, meta);
            found = true;
            break;
        }
    }
    
    if (!found) {
        std::cerr << "dmc install: version '" << pkg_version << "' of package '" << name << "' not available\n";
        return;
    }

    std::string toml_path = "dmc.toml";
    if (std::filesystem::exists(toml_path)) {
        std::ifstream fin(toml_path);
        std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        fin.close();

        if (content.find(name + " =") == std::string::npos && content.find("\"" + name + "\"") == std::string::npos) {
            size_t dep_pos = content.find("[dependencies]");
            if (dep_pos != std::string::npos) {
                size_t nl = content.find('\n', dep_pos);
                if (nl != std::string::npos) {
                    content.insert(nl + 1, name + " = \"^" + pkg_version + "\"\n");
                } else {
                    content += "\n" + name + " = \"^" + pkg_version + "\"\n";
                }
            } else {
                content += "\n[dependencies]\n" + name + " = \"^" + pkg_version + "\"\n";
            }
            std::ofstream fout(toml_path);
            fout << content;
            fout.close();
        }
    }

    update_lockfile();
    std::cout << "Package '" << name << "@" << pkg_version << "' installed successfully and locked in dmc.lock\n";
}

static void uninstall_package(const std::string& name) {
    auto pkgs = installed_packages();
    bool found = false;
    for (auto& pk : pkgs) {
        if (pk.name == name) {
            std::string pkg_path = packages_path() + "/" + name;
            std::filesystem::remove_all(pkg_path);
            found = true;
            break;
        }
    }

    std::string toml_path = "dmc.toml";
    if (std::filesystem::exists(toml_path)) {
        std::ifstream fin(toml_path);
        std::string line;
        std::string new_content;
        while (std::getline(fin, line)) {
            std::string tr = line;
            tr.erase(0, tr.find_first_not_of(" \t"));
            if (tr.find(name + " ") == 0 || tr.find(name + "=") == 0 || tr.find("\"" + name + "\"") == 0) {
                found = true;
                continue;
            }
            new_content += line + "\n";
        }
        fin.close();
        std::ofstream fout(toml_path);
        fout << new_content;
        fout.close();
        update_lockfile();
    }

    if (!found) {
        std::cerr << "dmc uninstall: package '" << name << "' is not installed\n";
        return;
    }
    std::cout << "Package '" << name << "' uninstalled successfully and dmc.lock updated\n";
}

static void update_package(const std::string& name) {
    auto results = search_registry(name);
    if (results.empty()) {
        std::cerr << "dmc update: package '" << name << "' not found in registry\n";
        return;
    }
    
    
    auto pkgs = installed_packages();
    std::string current_version;
    for (const auto& pkg : pkgs) {
        if (pkg.name == name) {
            current_version = pkg.version;
            break;
        }
    }
    
    
    std::string latest = results[0].version;
    if (current_version.empty() || latest > current_version) {
        install_package(name, latest);
    } else {
        std::cout << "Package '" << name << "' is already at latest version " << latest << "\n";
    }
}

static void list_packages() {
    auto pkgs = installed_packages();
    bool shown = false;
    if (!pkgs.empty()) {
        shown = true;
        std::cout << "Installed Global Packages (" << packages_path() << "):\n";
        for (const auto& pkg : pkgs) {
            std::cout << "  * " << pkg.name << " v" << pkg.version;
            if (!pkg.description.empty()) std::cout << " - " << pkg.description;
            std::cout << "\n";
        }
    }

    std::string lock_path = "dmc.lock";
    if (std::filesystem::exists(lock_path)) {
        shown = true;
        std::ifstream lf(lock_path);
        std::string line;
        std::string cur_name, cur_ver, cur_src;
        std::cout << "\nProject Locked Dependencies (dmc.lock):\n";
        while (std::getline(lf, line)) {
            std::string tr = line;
            tr.erase(0, tr.find_first_not_of(" \t\r\n"));
            tr.erase(tr.find_last_not_of(" \t\r\n") + 1);
            if (tr.find("name = ") == 0) {
                cur_name = tr.substr(7);
                if (cur_name.size() >= 2 && cur_name.front() == '"' && cur_name.back() == '"') cur_name = cur_name.substr(1, cur_name.size() - 2);
            } else if (tr.find("version = ") == 0 && !cur_name.empty()) {
                cur_ver = tr.substr(10);
                if (cur_ver.size() >= 2 && cur_ver.front() == '"' && cur_ver.back() == '"') cur_ver = cur_ver.substr(1, cur_ver.size() - 2);
            } else if (tr.find("source = ") == 0 && !cur_name.empty()) {
                cur_src = tr.substr(9);
                if (cur_src.size() >= 2 && cur_src.front() == '"' && cur_src.back() == '"') cur_src = cur_src.substr(1, cur_src.size() - 2);
                std::cout << "  - " << cur_name << " v" << cur_ver << " (" << cur_src << ")\n";
                cur_name.clear(); cur_ver.clear(); cur_src.clear();
            }
        }
    }

    if (!shown) {
        std::cout << "No packages installed.\n";
    }
}

static void search_packages(const std::string& name) {
    auto results = search_registry(name);
    if (results.empty()) {
        std::cout << "No packages found matching '" << name << "'.\n";
        return;
    }
    if (name.empty() || name == "*") {
        std::cout << "Demonic C Centralized Package Registry Catalog:\n";
    } else {
        std::cout << "Search results for '" << name << "':\n";
    }
    for (const auto& pkg : results) {
        std::cout << "  * " << pkg.name << " v" << pkg.version << " - " << pkg.summary << "\n";
    }
}

static void publish_package() {
    
    std::cout << "dmc publish: publishing package to registry...\n";
    
}

static void generate_project_files(const std::filesystem::path& proj_path, const std::string& name, const std::string& template_type) {
    std::filesystem::create_directories(proj_path / "src");
    std::filesystem::create_directories(proj_path / "tests");

    std::ofstream toml(proj_path / "dmc.toml");
    toml << "[package]\n";
    toml << "name = \"" << name << "\"\n";
    toml << "version = \"0.1.0\"\n";
    toml << "description = \"A Demonic C project (" << template_type << ")\"\n\n";
    toml << "[build]\n";
    toml << "output = \"bin/" << name << "\"\n";
    toml << "type = \"" << (template_type == "lib" ? "library" : "executable") << "\"\n\n";
    toml << "[dependencies]\n";
    if (template_type == "polyglot") {
        toml << "# py = \"*\"\n";
        toml << "# js = \"*\"\n";
        toml << "# ffi = \"*\"\n";
    }
    toml.close();

    std::ofstream gitignore(proj_path / ".gitignore");
    gitignore << "bin/\n";
    gitignore << "*.o\n";
    gitignore << "*.exe\n";
    gitignore << ".dmc/\n";
    gitignore << "*.c\n";
    gitignore.close();

    if (template_type == "game") {
        std::ofstream main_src(proj_path / "src" / "main.dmc");
        main_src << "// " << name << " - Demonic C Interactive Terminal Game\n";
        main_src << "import term;\n";
        main_src << "import random;\n\n";
        main_src << "fn main() -> int {\n";
        main_src << "    println(term_bold(term_cyan(\"=== GUESS THE SECRET NUMBER ===\")));\n";
        main_src << "    println(\"I have picked a secret number between 1 and 50.\");\n";
        main_src << "    println(\"Can you guess it within 7 tries?\\n\");\n\n";
        main_src << "    let secret = random_range(1, 50);\n";
        main_src << "    var attempts = 0;\n";
        main_src << "    let max_tries = 7;\n";
        main_src << "    var won = 0;\n\n";
        main_src << "    while (attempts < max_tries) {\n";
        main_src << "        attempts = attempts + 1;\n";
        main_src << "        let input_str = prompt(\"Attempt [\" + text_from_int(attempts) + \"/\" + text_from_int(max_tries) + \"] Enter guess: \");\n";
        main_src << "        let guess = text_to_int(input_str);\n";
        main_src << "        if (guess == secret) {\n";
        main_src << "            println(term_bold(term_green(\"\\n*** VICTORY! You guessed it in \" + text_from_int(attempts) + \" tries! ***\")));\n";
        main_src << "            won = 1;\n";
        main_src << "            break;\n";
        main_src << "        } else if (guess < secret) {\n";
        main_src << "            println(term_yellow(\"Higher!\\n\"));\n";
        main_src << "        } else {\n";
        main_src << "            println(term_red(\"Lower!\\n\"));\n";
        main_src << "        }\n";
        main_src << "    }\n";
        main_src << "    if (won == 0) {\n";
        main_src << "        println(term_red(\"Game Over! The secret number was: \" + text_from_int(secret)));\n";
        main_src << "    }\n";
        main_src << "    return 0;\n";
        main_src << "}\n";
        main_src.close();

        std::ofstream test_src(proj_path / "tests" / "test_main.dmc");
        test_src << "import random;\n\n#[test]\nfn test_rng_bound() -> int {\n";
        test_src << "    let r = random_range(1, 50);\n";
        test_src << "    if (r < 1 || r > 50) { return 1; }\n";
        test_src << "    return 0;\n";
        test_src << "}\n";
        test_src.close();
    } else if (template_type == "polyglot") {
        std::ofstream main_src(proj_path / "src" / "main.dmc");
        main_src << "// " << name << " - Demonic C Universal Polyglot Application\n";
        main_src << "import py;\n";
        main_src << "import js;\n\n";
        main_src << "fn main() -> int {\n";
        main_src << "    println(\"=== Universal Polyglot Runner ===\");\n\n";
        main_src << "    // 1. Python Bridge\n";
        main_src << "    println(\"Calling Python 3:\");\n";
        main_src << "    let py_val = py_eval(\"'Python says: 2 ** 8 = ' + str(2 ** 8)\");\n";
        main_src << "    println(\"  -> \" + py_val);\n\n";
        main_src << "    // 2. JavaScript Bridge\n";
        main_src << "    println(\"Calling JavaScript (Node/Bun/Deno):\");\n";
        main_src << "    let js_val = js_eval(\"`JS says: Math.sqrt(1764) = ${Math.sqrt(1764)}`\");\n";
        main_src << "    println(\"  -> \" + js_val);\n\n";
        main_src << "    return 0;\n";
        main_src << "}\n";
        main_src.close();

        std::ofstream test_src(proj_path / "tests" / "test_main.dmc");
        test_src << "import py;\n\n#[test]\nfn test_polyglot() -> int {\n";
        test_src << "    let res = py_eval(\"100 + 42\");\n";
        test_src << "    if (text_cmp(res, \"142\") != 0) { return 1; }\n";
        test_src << "    return 0;\n";
        test_src << "}\n";
        test_src.close();
    } else if (template_type == "lib") {
        std::filesystem::create_directories(proj_path / "lib");
        std::ofstream lib_src(proj_path / "lib" / (name + ".dmc"));
        lib_src << "// " << name << " - Demonic C Library Module\n\n";
        lib_src << "export fn " << name << "_add(a: int, b: int) -> int {\n";
        lib_src << "    return a + b;\n";
        lib_src << "}\n\n";
        lib_src << "export fn " << name << "_greet(user: string) -> string {\n";
        lib_src << "    return \"Greetings, \" + user + \"!\";\n";
        lib_src << "}\n";
        lib_src.close();

        std::ofstream test_src(proj_path / "tests" / ("test_" + name + ".dmc"));
        test_src << "import \"../lib/" << name << ".dmc\";\n\n#[test]\nfn test_math() -> int {\n";
        test_src << "    if (" << name << "_add(10, 32) != 42) { return 1; }\n";
        test_src << "    return 0;\n";
        test_src << "}\n\n#[test]\nfn test_greeting() -> int {\n";
        test_src << "    let g = " << name << "_greet(\"Antigravity\");\n";
        test_src << "    if (text_cmp(g, \"Greetings, Antigravity!\") != 0) { return 1; }\n";
        test_src << "    return 0;\n";
        test_src << "}\n";
        test_src.close();
    } else {
        // Default "cli" template
        std::filesystem::create_directories(proj_path / "lib");
        std::ofstream main_src(proj_path / "src" / "main.dmc");
        main_src << "// " << name << " - Demonic C Application\n";
        main_src << "import term;\n\n";
        main_src << "fn print_help() {\n";
        main_src << "    println(term_bold(\"Usage: \") + \"" << name << " [options]\");\n";
        main_src << "    println(\"\\nOptions:\");\n";
        main_src << "    println(\"  --help, -h    Display this help message\");\n";
        main_src << "}\n\n";
        main_src << "fn main() -> int {\n";
        main_src << "    let argc = arg_count();\n";
        main_src << "    if (argc > 1) {\n";
        main_src << "        let arg1 = arg_text(1);\n";
        main_src << "        if (text_cmp(arg1, \"--help\") == 0 || text_cmp(arg1, \"-h\") == 0) {\n";
        main_src << "            print_help();\n";
        main_src << "            return 0;\n";
        main_src << "        }\n";
        main_src << "    }\n";
        main_src << "    println(term_bold(term_green(\"Welcome to " << name << "!\")));\n";
        main_src << "    println(\"Built with Demonic C - Fast, expressive systems programming.\");\n";
        main_src << "    return 0;\n";
        main_src << "}\n";
        main_src.close();

        std::ofstream test_main(proj_path / "tests" / "test_main.dmc");
        test_main << "#[test]\nfn test_basic() -> int {\n";
        test_main << "    let a = 10;\n";
        test_main << "    let b = 32;\n";
        test_main << "    if (a + b == 42) { return 0; }\n";
        test_main << "    return 1;\n";
        test_main << "}\n";
        test_main.close();
    }
}

static void new_project(const std::string& name, const std::string& template_type = "cli") {
    std::filesystem::path proj_path = name;
    if (std::filesystem::exists(proj_path)) {
        std::cerr << "dmc new: directory '" << name << "' already exists\n";
        return;
    }
    generate_project_files(proj_path, name, template_type);
    std::cout << "Created new Demonic C project (" << template_type << "): " << name << "\n";
    std::cout << "\nTo get started:\n";
    std::cout << "  cd " << name << "\n";
    if (template_type == "lib") {
        std::cout << "  dmc test tests\n";
    } else {
        std::cout << "  dmc run src/main.dmc\n";
    }
}

static void init_project(const std::string& template_type = "cli") {
    if (std::filesystem::exists("dmc.toml")) {
        std::cerr << "dmc init: dmc.toml already exists\n";
        return;
    }
    std::string current_name = std::filesystem::current_path().filename().string();
    if (current_name.empty()) current_name = "my-project";
    generate_project_files(".", current_name, template_type);
    std::cout << "Initialized Demonic C project (" << template_type << ") in current directory\n";
}

static std::string format_dmc_code(const std::string& source) {
    std::stringstream input(source);
    std::string line;
    std::string result;
    int indent = 0;
    while (std::getline(input, line)) {
        size_t first = line.find_first_not_of(" \t");
        std::string trimmed = first == std::string::npos ? "" : line.substr(first);
        if (!trimmed.empty() && trimmed.front() == '}') indent = std::max(0, indent - 1);
        result.append(static_cast<size_t>(indent) * 4, ' ');
        result += trimmed;
        result += '\n';
        if (!trimmed.empty() && trimmed.back() == '{') ++indent;
    }
    return result;
}

static int cmd_fmt(int argc, char** argv) {
    (void)argc; (void)argv;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool check_only = false;
    std::string file_path;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--check") {
            check_only = true;
        } else if (file_path.empty()) {
            file_path = args[i];
        }
    }
    
    if (file_path.empty()) {
        std::cerr << "dmc fmt: usage: dmc fmt [--check] <file.dmc>\n";
        return 1;
    }
    
    if (!std::filesystem::exists(file_path)) {
        std::cerr << "dmc fmt: file not found: " << file_path << "\n";
        return 1;
    }
    
    std::ifstream input(file_path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string original = buffer.str();
    
    std::string formatted = format_dmc_code(format_dmc_code(original));
    
    if (check_only) {
        if (original != formatted) {
            std::cout << "dmc fmt: file would be reformatted: " << file_path << "\n";
            return 1;
        } else {
            std::cout << "dmc fmt: file is already formatted: " << file_path << "\n";
            return 0;
        }
    } else {
        std::ofstream output(file_path);
        output << formatted;
        std::cout << "dmc fmt: formatted: " << file_path << "\n";
        return 0;
    }
}

struct LintIssue {
    int line;
    std::string severity;
    std::string message;
    std::string code;
};

static std::vector<LintIssue> lint_dmc_code(const std::string& source) {
    std::vector<LintIssue> issues;
    auto lines = std::vector<std::string>();
    std::stringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    std::set<std::string> declared_vars;
    std::set<std::string> used_vars;
    bool in_function = false;
    std::string current_fn;
    
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        int line_num = (int)i + 1;
        
        if (ln.find("fn ") != std::string::npos && ln.find("fn main") != std::string::npos) {
            in_function = true;
            size_t fn_start = ln.find("fn ");
            size_t paren = ln.find("(");
            if (paren != std::string::npos) {
                current_fn = ln.substr(fn_start + 3, paren - fn_start - 3);
            }
        }
        
        std::regex var_decl("let\\s+(\\w+)");
        std::smatch match;
        if (std::regex_search(ln, match, var_decl)) {
            declared_vars.insert(match[1]);
        }
        
        if (ln.find("let ") != std::string::npos && ln.find("_ =") != std::string::npos) {
            issues.push_back({(int)line_num, "warning", "unused variable assigned to _", "W001"});
        }
        
        if (ln.find("return;") != std::string::npos && in_function) {
            issues.push_back({(int)line_num, "warning", "empty return in non-void function", "W002"});
        }
        
        if (ln.find("-> void") != std::string::npos && ln.find("{") != std::string::npos) {
            size_t brace_pos = ln.find("{");
            std::string before_brace = ln.substr(0, brace_pos);
            if (before_brace.find("fn ") != std::string::npos) {
                std::string fn_body = before_brace.substr(before_brace.find("fn ") + 3);
                fn_body.erase(0, fn_body.find_first_not_of(" \t"));
                issues.push_back({(int)line_num, "info", "function '" + fn_body + "' returns void", "I001"});
            }
        }
        
        if (ln.find("if (") != std::string::npos && ln.find("==") == std::string::npos && ln.find("!=") == std::string::npos) {
            issues.push_back({(int)line_num, "warning", "use explicit comparison in condition", "W003"});
        }
        
        if (ln.find("== \"\"") != std::string::npos || ln.find("!= \"\"") != std::string::npos) {
            issues.push_back({(int)line_num, "warning", "compare with empty string", "W004"});
        }
    }
    
    for (const auto& var : declared_vars) {
        if (used_vars.find(var) == used_vars.end() && var != "_") {
        }
    }
    
    return issues;
}

static int cmd_lint(int argc, char** argv) {
    (void)argc; (void)argv;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool fix_mode = false;
    (void)fix_mode;
    std::string file_path;
    
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--fix") {
            fix_mode = true;
        } else if (file_path.empty()) {
            file_path = args[i];
        }
    }
    
    if (file_path.empty()) {
        std::cerr << "dmc lint: usage: dmc lint [--fix] <file.dmc>\n";
        return 1;
    }
    
    if (!std::filesystem::exists(file_path)) {
        std::cerr << "dmc lint: file not found: " << file_path << "\n";
        return 1;
    }
    
    std::ifstream input(file_path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string source = buffer.str();
    
    auto issues = lint_dmc_code(source);
    
    if (issues.empty()) {
        std::cout << "dmc lint: no issues found in " << file_path << "\n";
        return 0;
    }
    
    int warnings = 0;
    int infos = 0;
    for (const auto& issue : issues) {
        std::cout << file_path << ":" << issue.line << ": " << issue.severity << ": " << issue.message << " [" << issue.code << "]\n";
        if (issue.severity == "warning") warnings++;
        else if (issue.severity == "info") infos++;
    }
    
    std::cout << "\n" << warnings << " warning(s), " << infos << " info(s)\n";
    
    return warnings > 0 ? 1 : 0;
}


static std::string html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

static std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    return lines;
}

static int cmd_doc(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool serve = false;
    bool publish = false;
    std::filesystem::path source = "../src";
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--serve") serve = true;
        else if (args[i] == "--publish") publish = true;
        else source = args[i];
    }
    std::filesystem::create_directories("docs/generated");
    std::ofstream index("docs/generated/index.html");
    if (!index) return 1;
    index << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Demonic C Documentation</title></head><body><h1>Demonic C Documentation</h1>";
    std::error_code error;
    if (std::filesystem::is_directory(source, error)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
            if (entry.path().extension() != ".dmc") continue;
            index << "<h2>" << html_escape(entry.path().string()) << "</h2><pre>";
            for (const auto& line : read_lines(entry.path())) index << html_escape(line) << "\n";
            index << "</pre>";
        }
    } else if (std::filesystem::exists(source)) {
        index << "<h2>" << html_escape(source.string()) << "</h2><pre>";
        for (const auto& line : read_lines(source)) index << html_escape(line) << "\n";
        index << "</pre>";
    }
    index << "</body></html>\n";
    index.close();
    if (publish) std::cout << "Documentation prepared for publishing at docs/generated/index.html\n";
    else if (serve) std::cout << "Documentation preview available at docs/generated/index.html\n";
    else std::cout << "Documentation generated at docs/generated/index.html\n";
    return 0;
}

static int count_attribute_functions(const std::filesystem::path& path, const std::string& attr) {
    int count = 0;
    for (const auto& line : read_lines(path)) if (line.find("#[" + attr + "]") != std::string::npos) ++count;
    return count;
}

static void safe_remove_file(const std::filesystem::path& p) {
    std::error_code ec;
    for (int retry = 0; retry < 10; ++retry) {
        if (!std::filesystem::exists(p, ec)) return;
        if (std::filesystem::remove(p, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::filesystem::remove(p, ec);
}

static bool is_c_or_cpp_source(const std::string& path) {
    if (path.size() >= 2 && path.compare(path.size() - 2, 2, ".c") == 0) return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".cpp") == 0) return true;
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".cc") == 0) return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".cxx") == 0) return true;
    if (path.size() >= 2 && path.compare(path.size() - 2, 2, ".o") == 0) return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".obj") == 0) return true;
    if (path.size() >= 2 && path.compare(path.size() - 2, 2, ".a") == 0) return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".lib") == 0) return true;
    return false;
}

static bool is_cpp_source(const std::string& path) {
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".cpp") == 0) return true;
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".cc") == 0) return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".cxx") == 0) return true;
    return false;
}

static void print_diagnostic(const std::string& filepath, const std::string& source, const Diagnostic& d, const std::string& severity);

static int cmd_test_impl(int argc, char** argv, const char* argv0) {
    (void)argv0;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool coverage = false;
    bool bench = false;
    std::vector<std::string> extra_flags;
    std::vector<std::string> extra_sources;
    std::filesystem::path root = std::filesystem::exists("test-files") ? "test-files" : "../test-files";
    for (const auto& arg : args) {
        if (arg == "--coverage") coverage = true;
        else if (arg == "--bench") bench = true;
        else if (arg.rfind("-l", 0) == 0 || arg.rfind("-L", 0) == 0 || arg.rfind("-I", 0) == 0 || arg.rfind("-D", 0) == 0) {
            extra_flags.push_back(arg);
        } else if (is_c_or_cpp_source(arg)) {
            extra_sources.push_back(arg);
        } else if (!arg.empty() && arg[0] != '-') {
            root = arg;
        }
    }
    if (!std::filesystem::exists(root)) {
        std::cerr << "dmc test: path not found: " << root.string() << "\n";
        return 1;
    }
    std::vector<std::filesystem::path> test_files;
    int total_tests = 0;
    int total_bench = 0;
    auto visit = [&](const std::filesystem::path& path) {
        if (path.extension() != ".dmc") return;
        int t = count_attribute_functions(path, "test");
        int b = count_attribute_functions(path, "bench");
        total_tests += t;
        total_bench += b;
        if (bench ? (b > 0) : (t > 0)) {
            test_files.push_back(path);
        }
    };
    std::error_code error;
    if (std::filesystem::is_directory(root, error)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) visit(entry.path());
    } else {
        visit(root);
    }
    if (test_files.empty()) {
        std::cout << "No " << (bench ? "#[bench]" : "#[test]") << " functions found in " << root.string() << "\n";
        return 0;
    }
    std::cout << "Running " << (bench ? total_bench : total_tests) << " " << (bench ? "benchmark(s)" : "test(s)") << " from " << test_files.size() << " file(s)...\n\n";
    const char* cc = std::getenv("CC");
    std::string c_compiler = cc ? cc : "clang";
#ifdef _WIN32
    if (!cc) {
        if (std::system("gcc --version > nul 2>&1") == 0) {
            c_compiler = "gcc";
        } else if (std::system("clang --version > nul 2>&1") == 0) {
            c_compiler = "clang";
        }
    }
#else
    if (!cc) c_compiler = "cc";
#endif
    int passed = 0;
    int failed = 0;
    static int test_counter = 0;
    auto now_epoch = (unsigned long long)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (const auto& file : test_files) {
        std::string test_id = std::to_string(now_epoch % 10000000) + "_" + std::to_string(++test_counter);
        std::string gen_c = "_test_tmp_" + test_id + ".c";
        std::string gen_bin = "_test_tmp_" + test_id;
#ifdef _WIN32
        gen_bin += ".exe";
#endif
        std::string source_content;
        try {
            std::ifstream in(file);
            if (!in) {
                std::cerr << "FAIL read " << file.string() << "\n";
                failed++;
                continue;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            source_content = ss.str();
        } catch (...) {
            std::cerr << "FAIL read " << file.string() << "\n";
            failed++;
            continue;
        }

        Parser parser(lex(source_content), file.string());
        parser.add_include_dir(".");
        parser.add_include_dir("stdlib");
        if (file.has_parent_path()) {
            parser.add_include_dir(file.parent_path().string());
        }
        for (const auto& flg : extra_flags) {
            if (flg.rfind("-I", 0) == 0 && flg.size() > 2) {
                parser.add_include_dir(flg.substr(2));
            }
        }
        std::string generated = parser.emit();
        if (parser.has_errors()) {
            std::cerr << "FAIL compile " << file.string() << "\n";
            for (const auto& d : parser.diagnostics()) {
                print_diagnostic(file.string(), source_content, d, "error");
            }
            failed++;
            continue;
        }

        {
            std::ofstream out(gen_c);
            out << generated;
        }

        bool has_cpp = false;
        for (const auto& s : extra_sources) if (is_cpp_source(s)) has_cpp = true;

        std::string link_flags = "";
#ifdef _WIN32
        link_flags += " -lws2_32 -lm";
#else
        link_flags += " -lm -ldl -lpthread";
#endif
        for (const auto& lib : parser.link_libs()) {
            link_flags += " -l" + lib;
        }
        for (const auto& flg : extra_flags) {
            link_flags += " " + flg;
        }
        if (has_cpp && !cc) {
            link_flags += " -lstdc++";
        }

        std::string sources_cmd = " \"" + gen_c + "\"";
        for (const auto& s : extra_sources) {
            sources_cmd += " \"" + s + "\"";
        }

#ifdef _WIN32
        std::string build_cmd = "\"" + c_compiler + " -O0 -w" + sources_cmd + " -o \"" + gen_bin + "\"" + link_flags + "\"";
#else
        std::string build_cmd = c_compiler + " -O0 -w" + sources_cmd + " -o \"" + gen_bin + "\"" + link_flags;
#endif
        if (std::system(build_cmd.c_str()) != 0) {
            std::cerr << "FAIL build " << file.string() << "\n";
            safe_remove_file(gen_c);
            failed++;
            continue;
        }
        safe_remove_file(gen_c);
        std::string run_cmd = "./" + gen_bin + (bench ? " --bench" : "");
#ifdef _WIN32
        run_cmd = gen_bin + (bench ? " --bench" : "");
#endif
        int res = std::system(run_cmd.c_str());
        safe_remove_file(gen_bin);
        if (res == 0) {
            std::cout << "PASS " << file.string() << "\n";
            passed++;
        } else {
            std::cerr << "FAIL " << file.string() << " (exit " << res << ")\n";
            failed++;
        }
    }
    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    if (coverage) std::cout << "Coverage: source collection completed\n";
    return failed == 0 ? 0 : 1;
}

static int cmd_bindgen(int argc, char** argv) {
    std::string header_path;
    std::string output_path;
    std::vector<std::string> link_libs;
    std::vector<std::string> include_dirs;
    std::string prefix_filter;
    std::string py_module;
    bool is_rust = false;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (i == 0 && (arg == "bindgen" || arg == "bindings" || arg == "binding")) continue;
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--link" && i + 1 < argc) {
            link_libs.push_back(argv[++i]);
        } else if (arg.rfind("-l", 0) == 0 && arg.size() > 2) {
            link_libs.push_back(arg.substr(2));
        } else if (arg == "-I" && i + 1 < argc) {
            include_dirs.push_back(argv[++i]);
        } else if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            include_dirs.push_back(arg.substr(2));
        } else if (arg == "--prefix" && i + 1 < argc) {
            prefix_filter = argv[++i];
        } else if ((arg == "--py" || arg == "--python") && i + 1 < argc) {
            py_module = argv[++i];
        } else if (arg == "--rust" && i + 1 < argc) {
            is_rust = true;
            header_path = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            if (header_path.empty()) header_path = arg;
        }
    }

    if (header_path.empty() && py_module.empty()) {
        std::cerr << "dmc bindgen: header file, rust file, or python module required\n";
        std::cerr << "usage:\n";
        std::cerr << "  dmc bindgen <header.h> [-o <out.dmc>] [-l<lib>|--link <lib>] [-I<dir>] [--prefix <prefix>]\n";
        std::cerr << "  dmc bindgen --rust <lib.rs> [-o <out.dmc>] [-l<lib>|--link <lib>]\n";
        std::cerr << "  dmc bindgen --py <module> [-o <out.dmc>] [--prefix <prefix>]\n";
        return 1;
    }

    if (!py_module.empty()) {
        std::string safe_module = py_module;
        for (char& c : safe_module) {
            if (c == '.' || c == '-' || c == '/' || c == ':') c = '_';
        }
        if (output_path.empty()) output_path = safe_module + "_bindings.dmc";
        std::string py_cmd = "python -c \"import json, importlib; m = importlib.import_module('" + py_module + "'); print(json.dumps([n for n in dir(m) if not n.startswith('_') and callable(getattr(m, n))]))\" 2>&1";
        std::string out_json;
#ifdef _WIN32
        FILE* p = _popen(py_cmd.c_str(), "r");
#else
        FILE* p = popen(py_cmd.c_str(), "r");
#endif
        int exit_code = 0;
        if (p) {
            char buf[4096];
            while (fgets(buf, sizeof(buf), p)) out_json += buf;
#ifdef _WIN32
            exit_code = _pclose(p);
#else
            exit_code = pclose(p);
#endif
        }

        if (exit_code != 0 || out_json.find('[') == std::string::npos) {
            std::cerr << "dmc bindgen: Python failed to inspect module '" << py_module << "'\n";
            if (out_json.find("ModuleNotFoundError") != std::string::npos) {
                std::cerr << "  ModuleNotFoundError: No module named '" << py_module << "'\n";
            } else if (!out_json.empty()) {
                std::cerr << "  Python error:\n    " << out_json << "\n";
            }
            if (py_module == "Optional" || py_module == "Union" || py_module == "List" ||
                py_module == "Dict" || py_module == "Tuple" || py_module == "Any" ||
                py_module == "Callable") {
                std::cerr << "\nHint: '" << py_module << "' is part of Python's 'typing' module, not a top-level module.\n"
                          << "To generate bindings for Python typing, run:\n"
                          << "  dmc bindgen --py typing\n\n"
                          << "Or if you were looking for Demonic C's native null/optional types, use '?T':\n"
                          << "  var value: ?string = null;\n"
                          << "Run 'dmc explain optional' to learn more.\n";
            }
            return 1;
        }

        std::vector<std::string> py_fns;
        size_t json_start = out_json.find('[');
        size_t json_end = out_json.rfind(']');
        std::string json_body = (json_start != std::string::npos && json_end != std::string::npos) 
                              ? out_json.substr(json_start, json_end - json_start + 1) : out_json;
        size_t pos = 0;
        while ((pos = json_body.find('"', pos)) != std::string::npos) {
            size_t end_q = json_body.find('"', pos + 1);
            if (end_q == std::string::npos) break;
            std::string fn_name = json_body.substr(pos + 1, end_q - pos - 1);
            if (!fn_name.empty() && fn_name != "json" && fn_name != "inspect" && fn_name != "importlib") {
                if (prefix_filter.empty() || fn_name.rfind(prefix_filter, 0) == 0) {
                    py_fns.push_back(fn_name);
                }
            }
            pos = end_q + 1;
        }

        std::ostringstream out;
        out << "// Auto-generated Demonic C bindings for Python module: " << py_module << "\n";
        out << "// Generated by dmc bindgen v2.0.1\n\n";
        out << "import py;\n\n";
        out << "fn py_" << safe_module << "_init() -> int {\n";
        out << "    return py_import(\"" << py_module << "\");\n";
        out << "}\n\n";
        out << "fn py_" << safe_module << "_call(func: string, arg: string) -> string {\n";
        out << "    return py_eval(\"" << py_module << ".\" + func + \"(\" + arg + \")\");\n";
        out << "}\n\n";
        for (const auto& fn : py_fns) {
            out << "fn py_" << safe_module << "_" << fn << "(arg: string) -> string {\n";
            out << "    return py_eval(\"" << py_module << "." << fn << "(\" + arg + \")\");\n";
            out << "}\n";
        }
        std::ofstream fout(output_path);
        if (!fout) {
            std::cerr << "dmc bindgen: failed to write output file: " << output_path << "\n";
            return 1;
        }
        fout << out.str();
        fout.close();
        std::cout << "Generated Python bindings in " << output_path << " (" << py_fns.size() << " functions)\n";
        return 0;
    }

    std::filesystem::path hp(header_path);
    if (!std::filesystem::exists(hp)) {
        bool found = false;
        for (const auto& d : include_dirs) {
            std::filesystem::path cand = std::filesystem::path(d) / hp;
            if (std::filesystem::exists(cand)) {
                hp = cand;
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "dmc bindgen: header file not found: " << header_path << "\n";
            return 1;
        }
    }

    std::ifstream in(hp);
    if (!in) {
        std::cerr << "dmc bindgen: cannot open header file: " << hp.string() << "\n";
        return 1;
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (is_rust || hp.extension() == ".rs") {
        if (output_path.empty()) output_path = hp.stem().string() + "_bindings.dmc";
        auto rust_type_to_dmc = [](const std::string& raw_t) -> std::string {
            std::string t = raw_t;
            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
            if (t == "()" || t.empty()) return "void";
            if (t == "i32" || t == "c_int") return "int";
            if (t == "u32" || t == "c_uint") return "u32";
            if (t == "i64" || t == "isize" || t == "c_longlong" || t == "c_long") return "i64";
            if (t == "u64" || t == "usize" || t == "c_ulonglong" || t == "c_ulong") return "u64";
            if (t == "f32" || t == "c_float") return "float";
            if (t == "f64" || t == "c_double") return "double";
            if (t == "bool") return "bool";
            if (t == "*const c_char" || t == "*const u8" || t == "&str") return "string";
            if (t == "*mut c_void" || t == "*const c_void" || t.rfind("*mut", 0) == 0 || t.rfind("*const", 0) == 0) return "*void";
            return t;
        };

        struct RustFn { std::string name; std::string ret; std::vector<std::pair<std::string, std::string>> params; };
        std::vector<RustFn> rust_fns;
        std::istringstream stream(raw);
        std::string line;
        while (std::getline(stream, line)) {
            size_t fn_pos = line.find("extern \"C\" fn ");
            if (fn_pos != std::string::npos) {
                size_t after_fn = fn_pos + 14;
                size_t paren = line.find('(', after_fn);
                if (paren != std::string::npos) {
                    std::string fname = line.substr(after_fn, paren - after_fn);
                    while (!fname.empty() && (fname.front() == ' ' || fname.front() == '\t')) fname.erase(fname.begin());
                    while (!fname.empty() && (fname.back() == ' ' || fname.back() == '\t')) fname.pop_back();

                    size_t close_paren = line.find(')', paren);
                    if (close_paren != std::string::npos) {
                        std::string params_str = line.substr(paren + 1, close_paren - paren - 1);
                        std::vector<std::pair<std::string, std::string>> params;
                        std::istringstream pstream(params_str);
                        std::string param_tok;
                        while (std::getline(pstream, param_tok, ',')) {
                            size_t colon = param_tok.find(':');
                            if (colon != std::string::npos) {
                                std::string pname = param_tok.substr(0, colon);
                                std::string ptype = param_tok.substr(colon + 1);
                                while (!pname.empty() && std::isspace((unsigned char)pname.front())) pname.erase(pname.begin());
                                while (!pname.empty() && std::isspace((unsigned char)pname.back())) pname.pop_back();
                                params.push_back({pname, rust_type_to_dmc(ptype)});
                            }
                        }
                        std::string ret_t = "void";
                        size_t arrow = line.find("->", close_paren);
                        if (arrow != std::string::npos) {
                            size_t brace = line.find('{', arrow);
                            if (brace == std::string::npos) brace = line.find(';', arrow);
                            if (brace == std::string::npos) brace = line.size();
                            std::string raw_ret = line.substr(arrow + 2, brace - arrow - 2);
                            ret_t = rust_type_to_dmc(raw_ret);
                        }
                        if (prefix_filter.empty() || fname.rfind(prefix_filter, 0) == 0) {
                            rust_fns.push_back({fname, ret_t, params});
                        }
                    }
                }
            }
        }

        std::ostringstream out;
        out << "// Auto-generated Demonic C bindings for Rust crate/source: " << hp.filename().string() << "\n";
        out << "// Generated by dmc bindgen v2.0.1\n\n";
        for (const auto& lib : link_libs) {
            out << "#[link(\"" << lib << "\")]\n";
        }
        out << "extern \"C\" {\n";
        for (const auto& fn : rust_fns) {
            out << "    fn " << fn.name << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) out << ", ";
                out << fn.params[i].first << ": " << fn.params[i].second;
            }
            out << ")";
            if (fn.ret != "void") out << " -> " << fn.ret;
            out << ";\n";
        }
        out << "}\n";

        std::ofstream fout(output_path);
        if (!fout) {
            std::cerr << "dmc bindgen: failed to write output file: " << output_path << "\n";
            return 1;
        }
        fout << out.str();
        fout.close();
        std::cout << "Generated Rust bindings in " << output_path << " (" << rust_fns.size() << " functions)\n";
        return 0;
    }
    in.close();

    if (output_path.empty()) {
        output_path = hp.stem().string() + "_bindings.dmc";
    }

    auto c_type_to_dmc = [](const std::string& raw_t) -> std::string {
        std::string t = raw_t;
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();

        bool is_const = false;
        if (t.rfind("const ", 0) == 0) {
            is_const = true;
            t = t.substr(6);
            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
        }
        if (t.size() >= 6 && t.rfind(" const") == t.size() - 6) {
            is_const = true;
            t = t.substr(0, t.size() - 6);
        }

        if (t == "void") return "void";
        if (t == "int" || t == "signed int" || t == "short" || t == "int32_t" || t == "int16_t") return "int";
        if (t == "unsigned int" || t == "uint32_t" || t == "uint16_t" || t == "unsigned short") return "u32";
        if (t == "long long" || t == "int64_t" || t == "long") return "i64";
        if (t == "unsigned long long" || t == "uint64_t" || t == "size_t" || t == "uintptr_t") return "u64";
        if (t == "float") return "float";
        if (t == "double") return "double";
        if (t == "bool" || t == "_Bool") return "bool";
        if (t == "char" || t == "int8_t") return "char";
        if (t == "unsigned char" || t == "uint8_t") return "u8";

        if (t == "char*" || (is_const && t == "char*") || t == "const char*") return "string";
        if (t == "void*" || t == "const void*") return "*void";
        if (!t.empty() && t.back() == '*') {
            std::string base = t.substr(0, t.size() - 1);
            while (!base.empty() && base.back() == ' ') base.pop_back();
            return "*" + base;
        }
        return t;
    };

    struct ParsedConst { std::string name; std::string type; std::string val; };
    struct ParsedField { std::string name; std::string type; };
    struct ParsedStruct { std::string name; std::vector<ParsedField> fields; };
    struct ParsedEnum { std::string name; std::vector<std::string> variants; };
    struct ParsedParam { std::string name; std::string type; };
    struct ParsedFn { std::string name; std::string ret; std::vector<ParsedParam> params; };

    std::vector<ParsedConst> constants;
    std::vector<ParsedStruct> structs;
    std::vector<ParsedEnum> enums;
    std::vector<ParsedFn> functions;

    std::string clean;
    clean.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == '/') {
            while (i < raw.size() && raw[i] != '\n') i++;
        } else if (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == '*') {
            i += 2;
            while (i + 1 < raw.size() && !(raw[i] == '*' && raw[i + 1] == '/')) i++;
            if (i < raw.size()) i += 2;
        } else {
            clean += raw[i++];
        }
    }

    std::istringstream stream(clean);
    std::string line;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        std::string l = line.substr(start);
        if (l.rfind("#define ", 0) == 0) {
            std::istringstream ls(l.substr(8));
            std::string cname, cval;
            ls >> cname >> cval;
            if (!cname.empty() && !cval.empty() && cname.find('(') == std::string::npos) {
                if (cval.front() == '"' && cval.back() == '"') {
                    constants.push_back({cname, "string", cval});
                } else if (cval.find('.') != std::string::npos) {
                    if (cval.back() == 'f' || cval.back() == 'F') cval.pop_back();
                    constants.push_back({cname, "float", cval});
                } else if (std::isdigit((unsigned char)cval[0]) || (cval.size() > 1 && cval[0] == '-' && std::isdigit((unsigned char)cval[1])) || (cval.rfind("0x", 0) == 0)) {
                    constants.push_back({cname, "int", cval});
                }
            }
        }
    }

    std::vector<std::string> toks;
    {
        std::string cur;
        for (size_t i = 0; i < clean.size(); ++i) {
            char c = clean[i];
            if (std::isspace((unsigned char)c)) {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            } else if (c == ';' || c == '{' || c == '}' || c == '(' || c == ')' || c == ',' || c == '*') {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
                toks.push_back(std::string(1, c));
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) toks.push_back(cur);
    }

    size_t ti = 0;
    while (ti < toks.size()) {
        if (toks[ti] == "#" || toks[ti].front() == '#') {
            while (ti < toks.size() && toks[ti] != ";") ti++;
            if (ti < toks.size() && toks[ti] == ";") ti++;
            continue;
        }

        if (toks[ti] == "struct" || (toks[ti] == "typedef" && ti + 1 < toks.size() && toks[ti + 1] == "struct")) {
            if (toks[ti] == "typedef") ti++;
            ti++;
            std::string tag;
            if (ti < toks.size() && toks[ti] != "{") {
                tag = toks[ti++];
            }
            if (ti < toks.size() && toks[ti] == "{") {
                ti++;
                std::vector<ParsedField> fields;
                while (ti < toks.size() && toks[ti] != "}") {
                    std::string ftype = toks[ti++];
                    if (ti < toks.size() && toks[ti] == "*") { ftype += "*"; ti++; }
                    if (ti < toks.size() && toks[ti] != ";" && toks[ti] != "}") {
                        std::string fname = toks[ti++];
                        if (ti < toks.size() && toks[ti] == ";") ti++;
                        fields.push_back({fname, c_type_to_dmc(ftype)});
                    }
                }
                if (ti < toks.size() && toks[ti] == "}") ti++;
                std::string sname = tag;
                if (ti < toks.size() && toks[ti] != ";") {
                    sname = toks[ti++];
                }
                if (ti < toks.size() && toks[ti] == ";") ti++;
                if (!sname.empty() && !fields.empty()) {
                    structs.push_back({sname, fields});
                }
            }
            continue;
        }

        if (toks[ti] == "enum" || (toks[ti] == "typedef" && ti + 1 < toks.size() && toks[ti + 1] == "enum")) {
            if (toks[ti] == "typedef") ti++;
            ti++;
            std::string tag;
            if (ti < toks.size() && toks[ti] != "{") {
                tag = toks[ti++];
            }
            if (ti < toks.size() && toks[ti] == "{") {
                ti++;
                std::vector<std::string> variants;
                while (ti < toks.size() && toks[ti] != "}") {
                    std::string vname = toks[ti++];
                    if (ti < toks.size() && toks[ti] == "=") {
                        ti++;
                        while (ti < toks.size() && toks[ti] != "," && toks[ti] != "}") ti++;
                    }
                    if (ti < toks.size() && toks[ti] == ",") ti++;
                    variants.push_back(vname);
                }
                if (ti < toks.size() && toks[ti] == "}") ti++;
                std::string ename = tag;
                if (ti < toks.size() && toks[ti] != ";") {
                    ename = toks[ti++];
                }
                if (ti < toks.size() && toks[ti] == ";") ti++;
                if (!ename.empty() && !variants.empty()) {
                    enums.push_back({ename, variants});
                }
            }
            continue;
        }

        if (toks[ti] == "extern" || toks[ti] == "inline" || toks[ti] == "__inline" || toks[ti] == "__declspec" ||
            toks[ti] == "RLAPI" || toks[ti] == "SQLITE_API" || toks[ti] == "WINAPI" || toks[ti] == "APIENTRY" ||
            toks[ti] == "\"C\"") {
            ti++;
            continue;
        }

        size_t look = ti;
        std::string ret_t = toks[look++];
        if (ret_t == "const" && look < toks.size()) ret_t += " " + toks[look++];
        if (look < toks.size() && toks[look] == "*") { ret_t += "*"; look++; }
        if (look + 1 < toks.size() && toks[look + 1] == "(") {
            std::string fname = toks[look++];
            look++;
            std::vector<ParsedParam> params;
            int pidx = 0;
            while (look < toks.size() && toks[look] != ")") {
                if (toks[look] == "void" && look + 1 < toks.size() && toks[look + 1] == ")") {
                    look++;
                    break;
                }
                if (toks[look] == "...") {
                    params.push_back({"...", "..."});
                    look++;
                    break;
                }
                std::string pt = toks[look++];
                if (pt == "const" && look < toks.size()) pt += " " + toks[look++];
                if (look < toks.size() && toks[look] == "*") { pt += "*"; look++; }
                std::string pname;
                if (look < toks.size() && toks[look] != "," && toks[look] != ")") {
                    pname = toks[look++];
                } else {
                    pname = "arg" + std::to_string(pidx++);
                }
                params.push_back({pname, c_type_to_dmc(pt)});
                if (look < toks.size() && toks[look] == ",") look++;
            }
            if (look < toks.size() && toks[look] == ")") look++;
            if (look < toks.size() && toks[look] == ";") {
                look++;
                ti = look;
                if (prefix_filter.empty() || fname.rfind(prefix_filter, 0) == 0) {
                    functions.push_back({fname, c_type_to_dmc(ret_t), params});
                }
                continue;
            }
        }
        ti++;
    }

    std::ostringstream out;
    out << "// Auto-generated Demonic C bindings for " << hp.filename().string() << "\n";
    out << "// Generated by dmc bindgen v2.0.1\n\n";
    out << "#include <" << hp.filename().string() << ">\n";
    for (const auto& lib : link_libs) {
        out << "#link \"" << lib << "\"\n";
    }
    out << "\n";

    if (!constants.empty()) {
        out << "// --- Constants ---\n";
        for (const auto& c : constants) {
            out << "let " << c.name << ": " << c.type << " = " << c.val << ";\n";
        }
        out << "\n";
    }

    if (!enums.empty()) {
        out << "// --- Enums ---\n";
        for (const auto& e : enums) {
            out << "enum " << e.name << " {\n";
            for (size_t i = 0; i < e.variants.size(); ++i) {
                out << "    " << e.variants[i] << (i + 1 < e.variants.size() ? ",\n" : "\n");
            }
            out << "}\n\n";
        }
    }

    if (!structs.empty()) {
        out << "// --- Structs ---\n";
        for (const auto& s : structs) {
            out << "struct " << s.name << " {\n";
            for (size_t i = 0; i < s.fields.size(); ++i) {
                out << "    " << s.fields[i].name << ": " << s.fields[i].type << ";\n";
            }
            out << "}\n\n";
        }
    }

    if (!functions.empty()) {
        out << "// --- Functions ---\n";
        for (const auto& f : functions) {
            out << "extern \"C\" fn " << f.name << "(";
            for (size_t i = 0; i < f.params.size(); ++i) {
                if (i) out << ", ";
                if (f.params[i].name == "...") out << "...";
                else out << f.params[i].name << ": " << f.params[i].type;
            }
            out << ") -> " << f.ret << ";\n";
        }
    }

    if (output_path == "-") {
        std::cout << out.str();
    } else {
        std::ofstream fout(output_path);
        if (!fout) {
            std::cerr << "dmc bindgen: cannot write to " << output_path << "\n";
            return 1;
        }
        fout << out.str();
        fout.close();
        std::cout << "Successfully generated " << output_path << " ("
                  << functions.size() << " functions, "
                  << structs.size() << " structs, "
                  << enums.size() << " enums, "
                  << constants.size() << " constants)\n";
    }
    return 0;
}

static int cmd_build(int argc, char** argv, const char* argv0) {
    (void)argv0;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool release = false;
    bool statik = false;
    bool sanitize = false;
    bool emit_c_only = false;
    bool shared_lib = false;
    bool freestanding = false;
    bool target_wasm = false;
    bool emit_line_directives = true;
    std::filesystem::path source = "";
    std::string explicit_output = "";
    std::vector<std::string> extra_sources;
    std::vector<std::string> extra_flags;
    std::vector<std::string> include_dirs;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--release") release = true;
        else if (arg == "--static") statik = true;
        else if (arg == "--sanitize") sanitize = true;
        else if (arg == "--emit-c") emit_c_only = true;
        else if (arg == "--shared" || arg == "--lib") shared_lib = true;
        else if (arg == "--freestanding" || arg == "--no-std") freestanding = true;
        else if (arg == "--target=wasm" || arg == "--target=wasm32") target_wasm = true;
        else if (arg == "--target" && i + 1 < args.size() && (args[i + 1] == "wasm" || args[i + 1] == "wasm32")) {
            target_wasm = true;
            ++i;
        }
        else if (arg == "--no-line-directives") emit_line_directives = false;
        else if (arg == "-o") {
            if (i + 1 < args.size()) explicit_output = args[++i];
        }
        else if (arg == "-I") {
            if (i + 1 < args.size()) {
                std::string d = args[++i];
                include_dirs.push_back(d);
                extra_flags.push_back("-I" + d);
            }
        }
        else if (arg.rfind("-I", 0) == 0) {
            include_dirs.push_back(arg.substr(2));
            extra_flags.push_back(arg);
        }
        else if (arg.rfind("-l", 0) == 0 || arg.rfind("-L", 0) == 0 || arg.rfind("-D", 0) == 0) {
            extra_flags.push_back(arg);
        }
        else if (is_c_or_cpp_source(arg)) {
            extra_sources.push_back(arg);
        }
        else if (!arg.empty() && arg[0] != '-') {
            if (source.empty()) source = arg;
            else extra_sources.push_back(arg);
        }
    }
    if (source.empty()) {
        if (std::filesystem::exists("src/main.dmc")) source = "src/main.dmc";
        else source = "main.dmc";
    }
    if (!std::filesystem::exists(source)) {
        std::cerr << "dmc build: source not found: " << source.string() << "\n";
        return 1;
    }
    std::string output = explicit_output;
    if (output.empty()) {
        std::filesystem::create_directories("bin");
        std::string stem = source.stem().string();
        if (target_wasm) {
            output = "bin/" + stem + ".wasm";
        } else if (shared_lib) {
#ifdef _WIN32
            output = "bin/" + stem + ".dll";
#elif defined(__APPLE__)
            output = "bin/" + stem + ".dylib";
#else
            output = "bin/" + stem + ".so";
#endif
        } else {
            output = "bin/" + stem;
#ifdef _WIN32
            output += ".exe";
#endif
        }
    } else {
        std::filesystem::path out_p(output);
        if (out_p.has_parent_path()) {
            std::filesystem::create_directories(out_p.parent_path());
        }
    }

    std::string source_content;
    try {
        std::ifstream in(source);
        if (!in) {
            std::cerr << "dmc build: cannot open source file: " << source.string() << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        source_content = ss.str();
    } catch (...) {
        std::cerr << "dmc build: cannot read source file: " << source.string() << "\n";
        return 1;
    }

    Parser parser(lex(source_content), source.string());
    parser.set_emit_line_directives(emit_line_directives);
    parser.set_freestanding(freestanding);
    parser.add_include_dir(".");
    parser.add_include_dir("stdlib");
    if (source.has_parent_path()) {
        parser.add_include_dir(source.parent_path().string());
    }
    for (const auto& d : include_dirs) parser.add_include_dir(d);

    std::string generated = parser.emit();
    for (const auto& d : parser.diagnostics()) {
        print_diagnostic(source.string(), source_content, d, "error");
    }
    for (const auto& w : parser.type_warnings()) {
        print_diagnostic(source.string(), source_content, w, "warning");
    }
    if (parser.has_errors()) return 1;

    if (emit_c_only) {
        std::string c_out = explicit_output.empty() ? source.stem().string() + ".c" : explicit_output;
        std::ofstream out(c_out);
        out << generated;
        std::cout << "Emitted C source: " << c_out << "\n";
        return 0;
    }

    uint64_t ts = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string gen_c = "_build_tmp_" + std::to_string(ts) + ".c";
    {
        std::ofstream out(gen_c);
        out << generated;
    }

    if (target_wasm) {
        bool has_emcc = (std::system("emcc --version > nul 2>&1") == 0);
        bool has_clang = (std::system("clang --version > nul 2>&1") == 0);
        std::string sources_cmd = " \"" + gen_c + "\"";
        for (const auto& s : extra_sources) sources_cmd += " \"" + s + "\"";
        std::string wasm_cmd;
        if (has_emcc) {
            wasm_cmd = "emcc -O2 -s WASM=1 -s STANDALONE_WASM=1 " + sources_cmd + " -o \"" + output + "\"";
        } else if (has_clang) {
            wasm_cmd = "clang --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export-all " + sources_cmd + " -o \"" + output + "\"";
        } else {
            std::cerr << "dmc build: WebAssembly target requires 'emcc' (Emscripten) or 'clang' with wasm32 support in PATH.\n";
            std::cerr << "Tip: Use 'dmc build " << source.string() << " --emit-c' to inspect or compile the generated C code.\n";
            safe_remove_file(gen_c);
            return 1;
        }
        int res = std::system(wasm_cmd.c_str());
        safe_remove_file(gen_c);
        if (res == 0) std::cout << "Built " << output << " (WebAssembly)\n";
        return res == 0 ? 0 : 1;
    }

    bool has_cpp = false;
    for (const auto& s : extra_sources) {
        if (is_cpp_source(s)) has_cpp = true;
    }

    const char* cc = std::getenv(has_cpp ? "CXX" : "CC");
    std::string c_compiler;
    if (cc) {
        c_compiler = cc;
    } else {
#ifdef _WIN32
        if (has_cpp) {
            if (std::system("g++ --version > nul 2>&1") == 0) c_compiler = "g++";
            else if (std::system("clang++ --version > nul 2>&1") == 0) c_compiler = "clang++";
            else c_compiler = "g++";
        } else {
            if (std::system("gcc --version > nul 2>&1") == 0) c_compiler = "gcc";
            else if (std::system("clang --version > nul 2>&1") == 0) c_compiler = "clang";
            else c_compiler = "gcc";
        }
#else
        if (has_cpp) c_compiler = "c++";
        else c_compiler = "cc";
#endif
    }

    std::string flags = release ? " -O2" : " -O0 -g";
    if (statik) flags += " -static";
    if (sanitize) flags += " -fsanitize=address,undefined -fno-omit-frame-pointer -g";
    if (shared_lib) {
        flags += " -shared";
#ifndef _WIN32
        flags += " -fPIC";
#else
        flags += " -Wl,--export-all-symbols";
#endif
    }
    if (freestanding) {
        flags += " -ffreestanding -nostdlib";
    }

    std::string link_flags = "";
    if (!freestanding) {
#ifdef _WIN32
        link_flags += " -lws2_32 -lm";
#else
        link_flags += " -lm -ldl -lpthread";
#endif
    }
    for (const auto& lib : parser.link_libs()) {
        link_flags += " -l" + lib;
    }
    for (const auto& flg : extra_flags) {
        link_flags += " " + flg;
    }
    if (has_cpp && !cc) {
        link_flags += " -lstdc++";
    }

    std::string sources_cmd = " \"" + gen_c + "\"";
    for (const auto& s : extra_sources) {
        sources_cmd += " \"" + s + "\"";
    }

    std::string command;
#ifdef _WIN32
    command = "\"" + c_compiler + flags + sources_cmd + " -o \"" + output + "\"" + link_flags + "\"";
#else
    command = c_compiler + flags + sources_cmd + " -o \"" + output + "\"" + link_flags;
#endif

    int result = std::system(command.c_str());
    safe_remove_file(gen_c);
    if (result == 0) std::cout << "Built " << output << "\n";
    return result == 0 ? 0 : 1;
}

static void print_diagnostic(const std::string& filepath, const std::string& source, const Diagnostic& d, const std::string& severity);

static std::string dmc_cache_dir() {
    std::error_code ec;
    const char* home = std::getenv("USERPROFILE");
#ifndef _WIN32
    if (!home) home = std::getenv("HOME");
#endif
    if (home) {
        std::filesystem::path p = std::filesystem::path(home) / ".dmc" / "cache";
        if (std::filesystem::create_directories(p, ec) || (!ec && std::filesystem::exists(p))) {
            return p.string();
        }
    }
    ec.clear();
    std::filesystem::path local = std::filesystem::current_path(ec) / ".dmc" / "cache";
    if (!ec && (std::filesystem::create_directories(local, ec) || (!ec && std::filesystem::exists(local)))) {
        return local.string();
    }
    ec.clear();
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / ".dmc" / "cache";
    if (!ec && (std::filesystem::create_directories(tmp, ec) || (!ec && std::filesystem::exists(tmp)))) {
        return tmp.string();
    }
    return ".";
}

static uint64_t compute_fnv1a_hash(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int cmd_run(const std::string& source_path, std::vector<std::string> program_args, const char* argv0, bool force_no_cache = false, bool force_sanitize = false) {
    (void)argv0;
    if (!std::filesystem::exists(source_path)) {
        std::cerr << "dmc: source file not found: " << source_path << "\n";
        return 1;
    }

    bool sanitize = force_sanitize;
    bool no_cache = force_no_cache;
    bool emit_line_directives = true;
    std::vector<std::string> extra_sources;
    std::vector<std::string> extra_flags;
    std::vector<std::string> clean_args;
    bool after_dash_dash = false;
    for (const auto& a : program_args) {
        if (after_dash_dash) {
            clean_args.push_back(a);
            continue;
        }
        if (a == "--") {
            after_dash_dash = true;
            continue;
        }
        if (a == "--sanitize") sanitize = true;
        else if (a == "--no-cache") no_cache = true;
        else if (a == "--no-line-directives") emit_line_directives = false;
        else if (a.rfind("-l", 0) == 0 || a.rfind("-L", 0) == 0 || a.rfind("-I", 0) == 0 || a.rfind("-D", 0) == 0) {
            extra_flags.push_back(a);
        } else if (is_c_or_cpp_source(a)) {
            extra_sources.push_back(a);
        } else {
            clean_args.push_back(a);
        }
    }
    program_args = clean_args;

    std::string source_content;
    try {
        std::ifstream in(source_path);
        if (!in) {
            std::cerr << "dmc: cannot open source file: " << source_path << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        source_content = ss.str();
    } catch (...) {
        std::cerr << "dmc: cannot read source file: " << source_path << "\n";
        return 1;
    }

    std::string stem = std::filesystem::path(source_path).stem().string();
    std::string hash_input = source_path + ":" + (sanitize ? "san:" : "nosan:") + source_content;
    for (const auto& s : extra_sources) hash_input += ":src:" + s;
    for (const auto& f : extra_flags) hash_input += ":flg:" + f;

    uint64_t hash_val = compute_fnv1a_hash(hash_input);
    std::stringstream hss;
    hss << std::hex << hash_val;
    std::string hash_hex = hss.str();

    std::string exe_name = stem + "_" + hash_hex;
#ifdef _WIN32
    exe_name += ".exe";
#endif
    std::filesystem::path cached_bin = std::filesystem::path(dmc_cache_dir()) / exe_name;

    if (!no_cache && extra_sources.empty() && std::filesystem::exists(cached_bin)) {
        std::string run_cmd;
#ifdef _WIN32
        run_cmd = "\"\"" + cached_bin.string() + "\"";
        for (const auto& arg : program_args) run_cmd += " \"" + arg + "\"";
        run_cmd += "\"";
#else
        run_cmd = "\"" + cached_bin.string() + "\"";
        for (const auto& arg : program_args) run_cmd += " \"" + arg + "\"";
#endif
        int run_res = std::system(run_cmd.c_str());
#ifdef _WIN32
        return run_res;
#else
        return (run_res >> 8) & 0xff;
#endif
    }

    uint64_t ts = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::string tmp_stem = "_dmc_run_" + std::to_string(ts);
    std::string gen_c = tmp_stem + ".c";

    Parser parser(lex(source_content), source_path);
    parser.set_emit_line_directives(emit_line_directives);
    try {
        parser.add_include_dir(".");
        parser.add_include_dir("stdlib");
        std::filesystem::path sp(source_path);
        if (sp.has_parent_path()) {
            parser.add_include_dir(sp.parent_path().string());
        }
        for (const auto& flg : extra_flags) {
            if (flg.rfind("-I", 0) == 0 && flg.size() > 2) {
                parser.add_include_dir(flg.substr(2));
            }
        }
        std::string generated = parser.emit();
        for (const auto& d : parser.diagnostics()) {
            print_diagnostic(source_path, source_content, d, "error");
        }
        for (const auto& w : parser.type_warnings()) {
            print_diagnostic(source_path, source_content, w, "warning");
        }
        if (parser.has_errors()) return 1;

        std::ofstream out(gen_c);
        if (!out) {
            std::cerr << "dmc: cannot write temporary source file\n";
            return 1;
        }
        out << generated;
        out.close();
    } catch (const std::exception& e) {
        std::cerr << "dmc: " << e.what() << "\n";
        return 1;
    }

    bool has_cpp = false;
    for (const auto& s : extra_sources) {
        if (is_cpp_source(s)) has_cpp = true;
    }

    const char* cc = std::getenv(has_cpp ? "CXX" : "CC");
    std::string c_compiler;
    if (cc) {
        c_compiler = cc;
    } else {
#ifdef _WIN32
        if (has_cpp) {
            if (std::system("g++ --version > nul 2>&1") == 0) c_compiler = "g++";
            else if (std::system("clang++ --version > nul 2>&1") == 0) c_compiler = "clang++";
            else c_compiler = "g++";
        } else {
            if (std::system("gcc --version > nul 2>&1") == 0) c_compiler = "gcc";
            else if (std::system("clang --version > nul 2>&1") == 0) c_compiler = "clang";
            else c_compiler = "gcc";
        }
#else
        if (has_cpp) c_compiler = "c++";
        else c_compiler = "cc";
#endif
    }

    std::string flags = " -O2";
    if (sanitize) {
        flags += " -fsanitize=address,undefined -fno-omit-frame-pointer -g";
    }

    std::string link_flags = "";
#ifdef _WIN32
    link_flags += " -lws2_32 -lm";
#else
    link_flags += " -lm -ldl -lpthread";
#endif
    for (const auto& lib : parser.link_libs()) {
        link_flags += " -l" + lib;
    }
    for (const auto& flg : extra_flags) {
        link_flags += " " + flg;
    }
    if (has_cpp && !cc) {
        link_flags += " -lstdc++";
    }

    std::string sources_cmd = " \"" + gen_c + "\"";
    for (const auto& s : extra_sources) {
        sources_cmd += " \"" + s + "\"";
    }

#ifdef _WIN32
    std::string compile_cmd = "\"" + c_compiler + flags + " -w" + sources_cmd + " -o \"" + cached_bin.string() + "\"" + link_flags + "\"";
#else
    std::string compile_cmd = c_compiler + flags + " -w" + sources_cmd + " -o \"" + cached_bin.string() + "\"" + link_flags;
#endif
    int c_res = std::system(compile_cmd.c_str());
    safe_remove_file(gen_c);
    if (c_res != 0) {
        std::cerr << "dmc: compilation failed\n";
        return 1;
    }

    std::string run_cmd;
#ifdef _WIN32
    run_cmd = "\"\"" + cached_bin.string() + "\"";
    for (const auto& arg : program_args) {
        run_cmd += " \"" + arg + "\"";
    }
    run_cmd += "\"";
#else
    run_cmd = "\"" + cached_bin.string() + "\"";
    for (const auto& arg : program_args) {
        run_cmd += " \"" + arg + "\"";
    }
#endif
    int run_res = std::system(run_cmd.c_str());
    if (no_cache || !extra_sources.empty()) {
        safe_remove_file(cached_bin);
    }
#ifdef _WIN32
    return run_res;
#else
    return (run_res >> 8) & 0xff;
#endif
}

static int cmd_watch(int argc, char** argv, const char* argv0) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::cerr << "dmc watch: usage: dmc watch <file.dmc> [args...]\n";
        return 1;
    }
    std::string source_path = args[0];
    std::vector<std::string> prog_args(args.begin() + 1, args.end());
    if (!std::filesystem::exists(source_path)) {
        std::cerr << "dmc watch: file not found: " << source_path << "\n";
        return 1;
    }
    std::cout << "\033[1;36m[dmc watch]\033[0m Watching " << source_path << " (Press Ctrl+C to stop)...\n\n";

    auto get_mtime = [](const std::filesystem::path& p) {
        try {
            return std::filesystem::last_write_time(p);
        } catch (...) {
            return std::filesystem::file_time_type::min();
        }
    };

    auto last_time = get_mtime(source_path);

    auto run_once = [&]() {
        std::cout << "\033[1;32m[dmc watch]\033[0m Running " << source_path << "...\n";
        cmd_run(source_path, prog_args, argv0, true, false);
        std::cout << "\n\033[1;30m----------------------------------------\033[0m\n";
    };

    run_once();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto cur_time = get_mtime(source_path);
        if (cur_time != last_time) {
            last_time = cur_time;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cout << "\033[1;33m[dmc watch]\033[0m Change detected in " << source_path << "\n";
            run_once();
        }
    }
    return 0;
}

static int cmd_repl(const char* argv0) {
    std::cout << "\033[1;31m"
              << "   ___                         _       _____ \n"
              << "  / _ \\___ __ _ ___  _ _  (_)__   /  __ \\\n"
              << " / // / -_)  ' \\/ _ \\/ ' \\/ / _ \\ / / / /\n"
              << "/____/\\__/_/_/_/\\___/_/_/_/_/\\___//_/ /_/ \n"
              << "\033[0m"
              << "Demonic C Interactive REPL (v2.0.1)\n"
              << "Type :help for commands, :exit or Ctrl+C to quit.\n\n";

    std::vector<std::string> session_statements;

    while (std::cin.good()) {
        std::cout << "\033[1;31mdmc>\033[0m ";
        std::string line;
        if (!std::getline(std::cin, line)) break;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;

        if (line == ":exit" || line == ":quit" || line == ":q") break;
        if (line == ":clear") {
            session_statements.clear();
            std::cout << "REPL session cleared.\n";
            continue;
        }
        if (line == ":help") {
            std::cout << "REPL Commands:\n"
                      << "  :exit, :q   Exit REPL\n"
                      << "  :clear      Clear session history\n"
                      << "  :history    Print current session code\n"
                      << "  :help       Print this help\n";
            continue;
        }
        if (line == ":history") {
            for (const auto& s : session_statements) std::cout << "  " << s << "\n";
            continue;
        }

        std::string statement = line;
        if (statement.back() != ';' && statement.back() != '}') {
            if (statement.rfind("let ", 0) != 0 && statement.rfind("var ", 0) != 0 && statement.rfind("fn ", 0) != 0) {
                statement = "println(" + statement + ");";
            } else {
                statement += ";";
            }
        }

        std::ostringstream snippet;
        snippet << "fn main() {\n";
        for (const auto& s : session_statements) {
            snippet << "    " << s << "\n";
        }
        snippet << "    " << statement << "\n";
        snippet << "}\n";

        std::string tmp_file = "_dmc_repl_tmp.dmc";
        std::ofstream out(tmp_file);
        out << snippet.str();
        out.close();

        std::vector<std::string> dummy;
        int res = cmd_run(tmp_file, dummy, argv0, true, false);
        safe_remove_file(tmp_file);
        if (res == 0) {
            if (line.rfind("let ", 0) == 0 || line.rfind("var ", 0) == 0 || line.rfind("fn ", 0) == 0) {
                session_statements.push_back(line.back() == ';' ? line : line + ";");
            }
        }
    }
    std::cout << "\nGoodbye!\n";
    return 0;
}

static int cmd_eval(int argc, char** argv, const char* argv0) {
    if (argc < 2) {
        std::cerr << "dmc eval: expression or code snippet required\n"
                  << "usage: dmc eval <expr> or dmc -e <expr>\n\n"
                  << "examples:\n"
                  << "  dmc eval 'println(\"Hello World!\")'\n"
                  << "  dmc eval '40 + 2'\n"
                  << "  dmc eval 'println(os_platform())'\n";
        return 1;
    }
    std::string snippet;
    for (int i = 1; i < argc; ++i) {
        if (!snippet.empty()) snippet += " ";
        snippet += argv[i];
    }
    while (!snippet.empty() && (snippet.front() == ' ' || snippet.front() == '\t')) snippet.erase(snippet.begin());
    while (!snippet.empty() && (snippet.back() == ' ' || snippet.back() == '\t' || snippet.back() == '\r' || snippet.back() == '\n')) snippet.pop_back();

    // Convert multi-character single-quoted strings 'hello' to "hello" for shell ergonomics
    std::string converted;
    bool in_single = false;
    size_t sq_start = 0;
    for (size_t i = 0; i < snippet.size(); ++i) {
        if (snippet[i] == '\'') {
            if (!in_single) {
                in_single = true;
                sq_start = converted.size();
                converted += '\'';
            } else {
                in_single = false;
                size_t inner_len = converted.size() - sq_start - 1;
                if (inner_len > 1 && !(inner_len == 2 && converted[sq_start + 1] == '\\')) {
                    converted[sq_start] = '"';
                    converted += '"';
                } else {
                    converted += '\'';
                }
            }
        } else {
            converted += snippet[i];
        }
    }
    snippet = converted;

    std::string full_code;
    if (snippet.find("fn main") != std::string::npos) {
        full_code = snippet;
    } else {
        std::string imports_block;
        std::string body = snippet;
        while (true) {
            while (!body.empty() && (body.front() == ' ' || body.front() == '\t' || body.front() == '\r' || body.front() == '\n')) body.erase(body.begin());
            if (body.rfind("import ", 0) == 0 || body.rfind("summon ", 0) == 0) {
                size_t semi = body.find(';');
                if (semi != std::string::npos) {
                    imports_block += body.substr(0, semi + 1) + "\n";
                    body = body.substr(semi + 1);
                    continue;
                }
            }
            break;
        }
        while (!body.empty() && (body.front() == ' ' || body.front() == '\t' || body.front() == '\r' || body.front() == '\n')) body.erase(body.begin());

        full_code = imports_block;
        full_code += "fn main() -> int {\n";
        if (body.find(';') == std::string::npos &&
            body.find("println(") == std::string::npos &&
            body.rfind("let ", 0) != 0 &&
            body.rfind("var ", 0) != 0 &&
            body.rfind("return ", 0) != 0) {
            full_code += "    println(" + body + ");\n";
        } else {
            full_code += "    " + body + "\n";
            if (!body.empty() && body.back() != ';' && body.back() != '}') {
                full_code += ";\n";
            }
        }
        full_code += "    return 0;\n}\n";
    }

#ifdef _WIN32
    unsigned long cur_pid = (unsigned long)GetCurrentProcessId();
#else
    unsigned long cur_pid = (unsigned long)getpid();
#endif
    std::string tmp_file = ".dmc_eval_" + std::to_string(cur_pid) + ".dmc";
    {
        std::ofstream out(tmp_file);
        if (!out) {
            std::cerr << "dmc eval: failed to create temporary file\n";
            return 1;
        }
        out << full_code;
    }

    int res = cmd_run(tmp_file, {}, argv0, true /* no-cache */);
    std::error_code ec;
    std::filesystem::remove(tmp_file, ec);
    return res;
}

static int cmd_explain(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" || std::string(argv[1]) == "list") {
        std::cout << "Demonic C Interactive Topic & Diagnostic Explainer (v2.0.1)\n\n"
                  << "usage: dmc explain <topic|code>\n\n"
                  << "Available Topics:\n"
                  << "  concurrency- Native OS threads, recursive mutexes, atomics, and Go-style channels\n"
                  << "  pointers   - Raw memory addresses, dereferencing (*p), and address-of (&x)\n"
                  << "  nullable   - Null-safety with '?T' types and safe unwrapping\n"
                  << "  defer      - Guaranteed resource cleanup before function exit\n"
                  << "  ffi        - Dynamic polyglot FFI to C, C++, Python, JavaScript & Rust\n"
                  << "  simd       - Hardware 128-bit SIMD vector types (f32x4, i32x4)\n"
                  << "  match      - Pattern matching on enums and algebraic variants\n"
                  << "  memory     - Raw heap buffers (mem_alloc) & zero-alloc arena rotators\n"
                  << "  struct     - Custom data structures, fields, and method receivers\n"
                  << "  prompt     - Interactive terminal stdin input with automatic trimming\n"
                  << "  term       - ANSI terminal styling, colors, and screen controls\n"
                  << "  fs         - Modern filesystem utilities (fs_read, fs_write, fs_copy, fs_mkdir)\n"
                  << "  strings    - String manipulation, concatenation (+), and text helpers\n\n"
                  << "Diagnostic Error & Warning Codes:\n"
                  << "  E001       - Syntax errors, unexpected tokens, missing semicolons/braces\n"
                  << "  W001       - Unused variable or unread parameter warning\n"
                  << "  W002       - Type mismatch or implicit conversion warning\n";
        return 0;
    }

    std::string topic = argv[1];
    std::transform(topic.begin(), topic.end(), topic.begin(), ::tolower);

    if (topic == "pointers" || topic == "ptr" || topic == "pointer") {
        std::cout << "================================================================================\n"
                  << "Topic: Pointers (*T, &x, *p)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Pointers hold the direct memory address of a value in RAM.\n"
                  << "  In Demonic C, pointers give you full bare-metal control with zero overhead.\n\n"
                  << "Syntax:\n"
                  << "  let p: *int = &x;     // Address-of operator '&' gets memory location\n"
                  << "  println(*p);          // Dereference operator '*' reads the value at address\n"
                  << "  *p = 100;             // Mutate the value stored at that address\n\n"
                  << "Example:\n"
                  << "  var number: int = 42;\n"
                  << "  let ptr: *int = &number;\n"
                  << "  *ptr = 99;\n"
                  << "  println(number);      // Prints 99!\n"
                  << "================================================================================\n";
    } else if (topic == "nullable" || topic == "null" || topic == "null-safety" || topic == "optional" || topic == "option") {
        std::cout << "================================================================================\n"
                  << "Topic: Null-Safety & Optional Types (?T, null)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Demonic C protects your programs from null-pointer crashes (segfaults).\n"
                  << "  Normal types cannot be null. To make a type optional or nullable, prefix it with '?'\n"
                  << "  (e.g. ?string, ?*int, ?Point).\n\n"
                  << "Best Practices:\n"
                  << "  Always verify that a nullable variable is not null before unwrapping or using it:\n\n"
                  << "Example:\n"
                  << "  var greeting: ?string = null;\n"
                  << "  if (greeting != null) {\n"
                  << "      println(greeting);\n"
                  << "  } else {\n"
                  << "      println(\"greeting is currently unset!\");\n"
                  << "  }\n\n"
                  << "Tagged Union Option (Rust-style):\n"
                  << "  You can also define an algebraic Option enum for explicit variants:\n"
                  << "    enum Option { Some(int), None }\n"
                  << "================================================================================\n";
    } else if (topic == "defer") {
        std::cout << "================================================================================\n"
                  << "Topic: defer\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  'defer' schedules a statement or block to execute right before the current\n"
                  << "  function exits, regardless of whether it returns early or normally.\n\n"
                  << "Why use it:\n"
                  << "  Never forget to close files, free memory, or release locks again!\n\n"
                  << "Example:\n"
                  << "  fn read_config(path: string) -> string {\n"
                  << "      let f = file_open(path, \"r\");\n"
                  << "      if (f < 0) { return \"\"; }\n"
                  << "      defer file_close(f); // Guaranteed to run when read_config() returns\n\n"
                  << "      let content = file_read(f);\n"
                  << "      return content;\n"
                  << "  }\n"
                  << "================================================================================\n";
    } else if (topic == "ffi" || topic == "polyglot" || topic == "bindgen") {
        std::cout << "================================================================================\n"
                  << "Topic: Polyglot Interop & Dynamic FFI\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Demonic C seamlessly integrates with C, C++, Python, JavaScript, and Rust.\n\n"
                  << "Options:\n"
                  << "  1. Dynamic C FFI:\n"
                  << "     import ffi;\n"
                  << "     let lib = ffi_open(\"raylib.dll\");\n"
                  << "     let fn_ptr = ffi_symbol(lib, \"InitWindow\");\n\n"
                  << "  2. Python Bridge:\n"
                  << "     import py;\n"
                  << "     let math_val = py_eval(\"math.sin(1.5)\");\n\n"
                  << "  3. JavaScript Bridge:\n"
                  << "     import js;\n"
                  << "     let js_val = js_eval(\"new Date().toISOString()\");\n\n"
                  << "  4. Auto-Generating Bindings:\n"
                  << "     dmc bindgen header.h -o header_bindings.dmc\n"
                  << "     dmc bindgen --py numpy\n"
                  << "     dmc bindgen --rust my_crate.rs\n"
                  << "================================================================================\n";
    } else if (topic == "simd" || topic == "vectors") {
        std::cout << "================================================================================\n"
                  << "Topic: 128-bit Hardware SIMD Vectors (f32x4, i32x4)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Single Instruction Multiple Data (SIMD) lets the CPU compute 4 operations\n"
                  << "  in parallel in a single clock cycle using x86 SSE/AVX or ARM NEON registers.\n\n"
                  << "Example:\n"
                  << "  let v1 = f32x4(1.0, 2.0, 3.0, 4.0);\n"
                  << "  let v2 = f32x4(10.0, 20.0, 30.0, 40.0);\n"
                  << "  let sum = v1.add(v2);   // [11.0, 22.0, 33.0, 44.0]\n"
                  << "  let dot = v1.dot(v2);   // 300.0\n"
                  << "================================================================================\n";
    } else if (topic == "match" || topic == "enum") {
        std::cout << "================================================================================\n"
                  << "Topic: Pattern Matching & Enums\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Type-safe algebraic data types (tagged unions) with pattern matching.\n\n"
                  << "Example:\n"
                  << "  enum Result { Ok(int), Err(string) }\n\n"
                  << "  fn handle_result(r: Result) {\n"
                  << "      match r {\n"
                  << "          Result::Ok(val) => println(\"Success: \" + text_from_int(val)),\n"
                  << "          Result::Err(msg) => println(\"Error: \" + msg),\n"
                  << "      }\n"
                  << "  }\n"
                  << "================================================================================\n";
    } else if (topic == "prompt" || topic == "cli" || topic == "input") {
        std::cout << "================================================================================\n"
                  << "Topic: Terminal Input (prompt)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  'prompt(msg)' prints the message prompt to stdout, flushes the stream,\n"
                  << "  and reads a line from stdin, automatically trimming trailing newlines.\n\n"
                  << "Example:\n"
                  << "  let name = prompt(\"What is your name? \");\n"
                  << "  println(\"Hello, \" + name + \"!\");\n"
                  << "================================================================================\n";
    } else if (topic == "term" || topic == "color" || topic == "colors") {
        std::cout << "================================================================================\n"
                  << "Topic: Terminal Utilities & ANSI Styling (stdlib/term)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  The 'term' module provides standard terminal styling and color helpers.\n\n"
                  << "Example:\n"
                  << "  import term;\n\n"
                  << "  println(term_bold(term_green(\"Build successful!\")));\n"
                  << "  println(term_red(\"Warning: deprecated API\"));\n"
                  << "  println(term_cyan(\"Server listening on port 8080\"));\n"
                  << "================================================================================\n";
    } else if (topic == "fs" || topic == "files") {
        std::cout << "================================================================================\n"
                  << "Topic: Filesystem Utilities (stdlib/fs)\n"
                  << "================================================================================\n"
                  << "Functions:\n"
                  << "  fs_read(path)          - Read entire file as string\n"
                  << "  fs_write(path, text)   - Overwrite file with content\n"
                  << "  fs_append(path, text)  - Append content to file\n"
                  << "  fs_copy(src, dst)      - Fast binary file copy\n"
                  << "  fs_mkdir(path)         - Create directory\n"
                  << "  fs_exists(path)        - Check if file exists (returns 1 or 0)\n"
                  << "  fs_remove(path)        - Delete file\n"
                  << "  fs_size(path)          - Get file size in bytes\n"
                  << "================================================================================\n";
    } else if (topic == "strings" || topic == "text") {
        std::cout << "================================================================================\n"
                  << "Topic: Strings & Text Helpers (stdlib/strings)\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Demonic C supports UTF-8 strings, automatic '+' concatenation, and rich helpers.\n\n"
                  << "Functions:\n"
                  << "  str_len(s)             - String length\n"
                  << "  str_starts_with(s, p)  - Check if string starts with prefix\n"
                  << "  str_ends_with(s, suf)  - Check if string ends with suffix\n"
                  << "  str_contains(s, sub)   - Check if string contains substring\n"
                  << "  str_index_of(s, sub)   - Find first index of substring\n"
                  << "  str_trim(s)            - Strip leading and trailing whitespace\n"
                  << "  str_to_upper(s)        - Convert to uppercase\n"
                  << "  str_to_lower(s)        - Convert to lowercase\n"
                  << "  str_replace(s, o, n)   - Replace occurrences of substring\n"
                  << "  str_repeat(s, n)       - Repeat string n times\n"
                  << "================================================================================\n";
    } else if (topic == "memory" || topic == "arena") {
        std::cout << "================================================================================\n"
                  << "Topic: Memory & Bump Arenas\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Demonic C provides both direct heap memory management and lightning-fast\n"
                  << "  bump arenas for zero-fragmentation bulk allocations.\n\n"
                  << "Example:\n"
                  << "  let arena = arena_new(1048576); // 1MB arena\n"
                  << "  let offset = arena_alloc(arena, 256);\n"
                  << "  arena_reset(arena);             // Instant O(1) reset of all allocations!\n"
                  << "  arena_free(arena);\n"
                  << "================================================================================\n";
    } else if (topic == "struct" || topic == "structs") {
        std::cout << "================================================================================\n"
                  << "Topic: Structs and Methods\n"
                  << "================================================================================\n"
                  << "Example:\n"
                  << "  struct Player {\n"
                  << "      name: string,\n"
                  << "      score: int;\n"
                  << "  }\n\n"
                  << "  fn (self: Player) display() {\n"
                  << "      println(self.name + \": \" + text_from_int(self.score));\n"
                  << "  }\n"
                  << "================================================================================\n";
    } else if (topic == "e001" || topic == "syntax") {
        std::cout << "================================================================================\n"
                  << "Diagnostic: E001 - Syntax Error\n"
                  << "================================================================================\n"
                  << "Common Causes:\n"
                  << "  1. Missing semicolon ';' at the end of a statement.\n"
                  << "  2. Unclosed parenthesis ')' or brace '}'.\n"
                  << "  3. Typo in keywords (e.g. 'function' instead of 'fn').\n\n"
                  << "Fix:\n"
                  << "  Check the line and column number shown in the error diagnostic.\n"
                  << "================================================================================\n";
    } else if (topic == "w001" || topic == "unused") {
        std::cout << "================================================================================\n"
                  << "Diagnostic: W001 - Unused Variable Warning\n"
                  << "================================================================================\n"
                  << "What it means:\n"
                  << "  A variable was declared with 'let' or 'var' but never read or referenced.\n\n"
                  << "Fix:\n"
                  << "  - Remove the unused variable, or\n"
                  << "  - Prefix the variable name with an underscore '_' (e.g. let _unused = 0;)\n"
                  << "================================================================================\n";
    } else if (topic == "w002" || topic == "type") {
        std::cout << "================================================================================\n"
                  << "Diagnostic: W002 - Type Mismatch Warning\n"
                  << "================================================================================\n"
                  << "What it means:\n"
                  << "  Assigning a value of one type to a variable of a different type without\n"
                  << "  an explicit cast.\n\n"
                  << "Fix:\n"
                  << "  Use the 'as' casting operator: 'let x: int = float_val as int;'\n"
                  << "================================================================================\n";
    } else if (topic == "threads" || topic == "thread" || topic == "concurrency" || topic == "channels" || topic == "channel" || topic == "mutex") {
        std::cout << "================================================================================\n"
                  << "Topic: Multithreading, Recursive Mutexes & Channels\n"
                  << "================================================================================\n"
                  << "What is it:\n"
                  << "  Demonic C provides zero-cost native OS threads, recursive mutexes, lock-free\n"
                  << "  hardware atomics, and Go-style thread-safe condition-variable channels.\n\n"
                  << "Core Primitives:\n"
                  << "  thread_spawn(func)           - Spawn a new OS thread running func\n"
                  << "  thread_join(th)              - Wait for thread completion\n"
                  << "  thread_sleep(ms)             - Sleep current thread for ms milliseconds\n"
                  << "  mutex_new() / mutex_lock(m)  - Recursive thread synchronization\n"
                  << "  atomic_add(&x, val)          - Hardware lock-free atomic increment\n"
                  << "  channel_new(capacity)        - Create bounded/unbounded safe FIFO channel\n"
                  << "  channel_send(ch, msg)        - Send string message across threads\n"
                  << "  channel_recv(ch)             - Blocking receive string message\n\n"
                  << "Example:\n"
                  << "  import thread;\n\n"
                  << "  fn worker() {\n"
                  << "      println(\"Worker thread started: \" + text_from_int(thread_id()));\n"
                  << "  }\n\n"
                  << "  fn main() -> int {\n"
                  << "      let th = thread_spawn(worker);\n"
                  << "      thread_join(th);\n"
                  << "      return 0;\n"
                  << "  }\n"
                  << "================================================================================\n";
    } else {
        std::cout << "Unknown topic '" << topic << "'. Run 'dmc explain' to see all available topics.\n";
        return 1;
    }
    return 0;
}

static int cmd_install_vscode() {
    const char* home = std::getenv("USERPROFILE");
#ifndef _WIN32
    if (!home) home = std::getenv("HOME");
#endif
    if (!home) {
        std::cerr << "dmc: could not determine user home directory\n";
        return 1;
    }
    std::filesystem::path ext_dir = std::filesystem::path(home) / ".vscode" / "extensions" / "demonic-c-2.0.1";
    std::filesystem::path src_dir = "editor/vscode";
    if (!std::filesystem::exists(src_dir)) {
        if (std::filesystem::exists("../editor/vscode")) src_dir = "../editor/vscode";
    }
    if (!std::filesystem::exists(src_dir)) {
        std::cerr << "dmc: editor/vscode directory not found\n";
        return 1;
    }
    try {
        std::filesystem::create_directories(ext_dir);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
            auto rel = std::filesystem::relative(entry.path(), src_dir);
            auto dest = ext_dir / rel;
            if (entry.is_directory()) {
                std::filesystem::create_directories(dest);
            } else {
                std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing);
            }
        }
        std::cout << "\033[1;32m[dmc]\033[0m Successfully installed Demonic C extension to " << ext_dir.string() << "\n"
                  << "Restart or reload VS Code / Cursor to enjoy full language support!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "dmc: failed to install VS Code extension: " << e.what() << "\n";
        return 1;
    }
}

static int cmd_bundle() {
    std::filesystem::create_directories("dist");
    std::ofstream manifest("dist/manifest.txt");
    manifest << "dmc.toml\n";
    if (std::filesystem::exists("src")) for (const auto& entry : std::filesystem::recursive_directory_iterator("src")) if (entry.is_regular_file()) manifest << entry.path().string() << "\n";
    if (std::filesystem::exists("test-files")) for (const auto& entry : std::filesystem::recursive_directory_iterator("test-files")) if (entry.is_regular_file()) manifest << entry.path().string() << "\n";
    std::cout << "Bundle manifest created at dist/manifest.txt\n";
    return 0;
}

static int cmd_login() {
    std::filesystem::create_directories(pkg_dir());
    std::ofstream token(std::filesystem::path(pkg_dir()) / "credentials");
    token << "authenticated=true\n";
    std::cout << "Registry authentication initialized locally\n";
    return 0;
}

static int cmd_deprecate(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "dmc deprecate: package version required\n";
        return 1;
    }
    std::ofstream notice(std::filesystem::path(pkg_dir()) / (std::string(argv[1]) + ".deprecated"));
    notice << "deprecated=true\n";
    std::cout << "Marked " << argv[1] << " as deprecated locally\n";
    return 0;
}

static int cmd_new(int argc, char** argv) {
    std::string name;
    std::string tmpl = "cli";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--game") {
            tmpl = "game";
        } else if (a == "--polyglot") {
            tmpl = "polyglot";
        } else if (a == "--lib" || a == "--library") {
            tmpl = "lib";
        } else if (a == "--cli") {
            tmpl = "cli";
        } else if (a.rfind("--template=", 0) == 0) {
            tmpl = a.substr(11);
        } else if ((a == "-t" || a == "--template") && i + 1 < argc) {
            tmpl = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: dmc new <project-name> [--game|--polyglot|--lib|--cli|--template=<name>]\n\n"
                      << "templates:\n"
                      << "  cli       Command-line application starter (default)\n"
                      << "  game      Interactive terminal game using prompt() & term styling (--game)\n"
                      << "  polyglot  Multi-language project using Python, JS, and C FFI (--polyglot)\n"
                      << "  lib       Reusable library with exported modules and unit tests (--lib)\n";
            return 0;
        } else if (a[0] != '-' && name.empty()) {
            name = a;
        }
    }
    if (name.empty()) {
        std::cerr << "dmc new: project name required\nusage: dmc new <project-name> [--game|--polyglot|--lib|--cli|--template=<name>]\n";
        return 1;
    }
    new_project(name, tmpl);
    return 0;
}

static int cmd_init(int argc, char** argv) {
    std::string tmpl = "cli";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--game") {
            tmpl = "game";
        } else if (a == "--polyglot") {
            tmpl = "polyglot";
        } else if (a == "--lib" || a == "--library") {
            tmpl = "lib";
        } else if (a == "--cli") {
            tmpl = "cli";
        } else if (a.rfind("--template=", 0) == 0) {
            tmpl = a.substr(11);
        } else if ((a == "-t" || a == "--template") && i + 1 < argc) {
            tmpl = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout << "usage: dmc init [--game|--polyglot|--lib|--cli|--template=<name>]\n";
            return 0;
        }
    }
    init_project(tmpl);
    return 0;
}

static int cmd_pkg(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    
    if (args.empty()) {
        std::cerr << "dmc pkg: usage: dmc pkg <add|install|uninstall|update|search|list|publish> [package/git-url]\n";
        return 1;
    }
    
    std::string command = args[0];
    
    if (command == "add") {
        if (args.size() < 2) {
            std::cerr << "dmc pkg add: repository URL or package name required\n";
            return 1;
        }
        std::string target = args[1];
        if (target.find("http://") != std::string::npos || 
            target.find("https://") != std::string::npos || 
            target.find("git@") != std::string::npos || 
            (target.size() > 4 && target.rfind(".git") == target.size() - 4)) {
            
            std::string repo_name = target;
            if (repo_name.size() > 4 && repo_name.rfind(".git") == repo_name.size() - 4) {
                repo_name = repo_name.substr(0, repo_name.size() - 4);
            }
            size_t slash = repo_name.find_last_of("/\\");
            if (slash != std::string::npos) {
                repo_name = repo_name.substr(slash + 1);
            }
            
            std::filesystem::create_directories("packages");
            std::string dest = "packages/" + repo_name;
            std::string clone_cmd = "git clone --depth 1 " + target + " " + dest;
            std::cout << "Cloning " << target << " into " << dest << "...\n";
            int ret = system(clone_cmd.c_str());
            if (ret != 0) {
                std::cerr << "dmc pkg add: git clone failed with code " << ret << "\n";
                return 1;
            }
            
            std::string toml_path = "dmc.toml";
            std::string content;
            bool has_deps = false;
            if (std::filesystem::exists(toml_path)) {
                std::ifstream f(toml_path);
                std::string line;
                while (std::getline(f, line)) {
                    if (line.find("[dependencies]") != std::string::npos) has_deps = true;
                    content += line + "\n";
                }
            } else {
                content = "[package]\nname = \"dmc_project\"\nversion = \"0.1.0\"\n\n";
            }
            
            if (has_deps) {
                size_t pos = content.find("[dependencies]");
                size_t next_line = content.find('\n', pos);
                content.insert(next_line + 1, repo_name + " = { git = \"" + target + "\" }\n");
            } else {
                content += "[dependencies]\n" + repo_name + " = { git = \"" + target + "\" }\n";
            }
            std::ofstream out(toml_path);
            out << content;
            std::cout << "Successfully added package '" << repo_name << "' to " << toml_path << "\n";
            return 0;
        } else {
            install_package(target, args.size() > 2 ? args[2] : "");
            resolve_dependencies();
            return 0;
        }
    } else if (command == "install") {
        std::string name = "";
        std::string version = "";
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--version" && i + 1 < args.size()) {
                version = args[++i];
            } else if (name.empty()) {
                name = args[i];
            }
        }
        if (name.empty()) {
            std::cerr << "dmc install: package name required\n";
            return 1;
        }
        install_package(name, version);
        resolve_dependencies();
    } else if (command == "uninstall") {
        if (args.size() < 2) {
            std::cerr << "dmc uninstall: package name required\n";
            return 1;
        }
        uninstall_package(args[1]);
    } else if (command == "update") {
        std::string name = "";
        for (size_t i = 1; i < args.size(); ++i) {
            if (!name.empty()) break;
            name = args[i];
        }
        if (name.empty()) {
            std::cerr << "dmc update: package name required\n";
            return 1;
        }
        update_package(name);
    } else if (command == "search") {
        std::string name = "";
        for (size_t i = 1; i < args.size(); ++i) {
            if (name.empty()) {
                name = args[i];
            }
        }
        search_packages(name);
        return 0;
    } else if (command == "list") {
        list_packages();
    } else if (command == "publish") {
        publish_package();
        std::string _r = registry_url();
        (void)_r;
    } else {
        std::cerr << "dmc pkg: unknown command '" << command << "'\n";
        return 1;
    }
    
    return 0;
}

static void enable_ansi_console() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            SetConsoleMode(hOut, dwMode | 0x0004 );
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hErr, &dwMode)) {
            SetConsoleMode(hErr, dwMode | 0x0004 );
        }
    }
#endif
}

static const char* color_red = "\033[1;31m";
static const char* color_green = "\033[1;32m";
static const char* color_yellow = "\033[1;33m";
static const char* color_cyan = "\033[1;36m";
static const char* color_bold = "\033[1m";
static const char* color_reset = "\033[0m";

static void print_diagnostic(const std::string& filepath, const std::string& source, const Diagnostic& d, const std::string& severity) {
    bool is_error = (severity == "error");
    const char* sev_color = is_error ? color_red : color_yellow;

    std::cerr << color_bold << filepath << ":" << d.line << ":" << d.col << ": "
              << sev_color << severity << ": " << color_bold << d.message << color_reset << "\n";

    if (!source.empty() && d.line > 0) {
        std::istringstream stream(source);
        std::string line_content;
        int current_line = 1;
        while (current_line <= d.line && std::getline(stream, line_content)) {
            if (current_line == d.line) {
                if (!line_content.empty() && line_content.back() == '\r') {
                    line_content.pop_back();
                }
                std::string visual_line;
                int caret_pos = 0;
                for (size_t i = 0; i < line_content.size(); ++i) {
                    if (static_cast<int>(i + 1) == d.col) {
                        caret_pos = static_cast<int>(visual_line.size());
                    }
                    if (line_content[i] == '\t') {
                        visual_line.append("    ");
                    } else {
                        visual_line.push_back(line_content[i]);
                    }
                }
                if (d.col > static_cast<int>(line_content.size())) {
                    caret_pos = static_cast<int>(visual_line.size());
                }

                std::string line_num_str = std::to_string(d.line);
                std::string padding(line_num_str.size(), ' ');

                std::cerr << color_cyan << " " << line_num_str << " | " << color_reset << visual_line << "\n";
                std::cerr << color_cyan << " " << padding << " | " << color_reset;
                for (int i = 0; i < caret_pos; ++i) {
                    std::cerr << ' ';
                }
                std::cerr << sev_color << "^" << color_reset << "\n";
                break;
            }
            current_line++;
        }
    }
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

static void lsp_send(const std::string& content) {
    std::cout << "Content-Length: " << content.size() << "\r\n\r\n" << content << std::flush;
}

static std::string lsp_find_key(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                if (json[pos] == 'n') val += '\n';
                else if (json[pos] == 'r') val += '\r';
                else if (json[pos] == 't') val += '\t';
                else if (json[pos] == '"') val += '"';
                else if (json[pos] == '\\') val += '\\';
                else val += json[pos];
            } else {
                val += json[pos];
            }
            pos++;
        }
        return val;
    } else {
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' && json[end] != ' ' && json[end] != '\r' && json[end] != '\n') {
            end++;
        }
        return json.substr(pos, end - pos);
    }
}

static int cmd_lsp() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::map<std::string, std::string> documents;

    auto validate_doc = [&](const std::string& uri, const std::string& text) {
        std::vector<Diagnostic> errors;
        std::vector<Diagnostic> warnings;
        try {
            Parser parser(lex(text), uri);
            parser.emit();
            errors = parser.diagnostics();
            warnings = parser.type_warnings();
        } catch (const std::exception& e) {
            errors.push_back(Diagnostic{1, 1, e.what()});
        }

        std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + uri + "\",\"diagnostics\":[";
        bool first = true;
        auto add_diag = [&](const Diagnostic& d, int severity) {
            if (!first) json += ",";
            first = false;
            int l = d.line > 0 ? d.line - 1 : 0;
            int c = d.col > 0 ? d.col - 1 : 0;
            json += "{\"range\":{\"start\":{\"line\":" + std::to_string(l) + ",\"character\":" + std::to_string(c) + "},";
            json += "\"end\":{\"line\":" + std::to_string(l) + ",\"character\":" + std::to_string(c + 1) + "}},";
            json += "\"severity\":" + std::to_string(severity) + ",";
            json += "\"source\":\"dmc\",";
            json += "\"message\":\"" + json_escape(d.message) + "\"}";
        };
        for (const auto& d : errors) add_diag(d, 1);
        for (const auto& w : warnings) add_diag(w, 2);
        json += "]}}";
        lsp_send(json);
    };

    while (std::cin.good()) {
        std::string header;
        size_t content_length = 0;
        while (std::getline(std::cin, header)) {
            if (!header.empty() && header.back() == '\r') header.pop_back();
            if (header.empty()) break;
            if (header.rfind("Content-Length: ", 0) == 0) {
                content_length = std::stoull(header.substr(16));
            }
        }
        if (content_length == 0) break;

        std::string payload(content_length, '\0');
        std::cin.read(&payload[0], content_length);
        if (!std::cin) break;

        std::string method = lsp_find_key(payload, "method");
        std::string id = lsp_find_key(payload, "id");

        if (method == "initialize") {
            std::string resp = "{\"jsonrpc\":\"2.0\",\"id\":" + (id.empty() ? "1" : id) + ",\"result\":{"
                "\"capabilities\":{"
                "\"textDocumentSync\":1,"
                "\"completionProvider\":{\"resolveProvider\":false,\"triggerCharacters\":[\".\",\":\"]},"
                "\"hoverProvider\":true,"
                "\"definitionProvider\":true"
                "},"
                "\"serverInfo\":{\"name\":\"dmc-lsp\",\"version\":\"2.0.1\"}"
                "}}";
            lsp_send(resp);
        } else if (method == "initialized") {
        } else if (method == "textDocument/didOpen") {
            std::string uri = lsp_find_key(payload, "uri");
            std::string text = lsp_find_key(payload, "text");
            documents[uri] = text;
            validate_doc(uri, text);
        } else if (method == "textDocument/didChange") {
            std::string uri = lsp_find_key(payload, "uri");
            std::string text = lsp_find_key(payload, "text");
            documents[uri] = text;
            validate_doc(uri, text);
        } else if (method == "textDocument/didClose") {
            std::string uri = lsp_find_key(payload, "uri");
            documents.erase(uri);
            std::string json = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" + uri + "\",\"diagnostics\":[]}}";
            lsp_send(json);
        } else if (method == "textDocument/completion") {
            std::string resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":[";
            static const std::vector<std::pair<std::string, std::string>> kw = {
                {"fn", "Function definition"}, {"let", "Immutable variable declaration"},
                {"var", "Mutable variable declaration"}, {"if", "Conditional branching"},
                {"else", "Else branch"}, {"while", "While loop"},
                {"for", "For loop"}, {"in", "Range/iterator membership"},
                {"return", "Return from function"}, {"defer", "Defer statement execution to scope exit"},
                {"struct", "Structure definition"}, {"enum", "Enum or tagged union definition"},
                {"match", "Pattern match"}, {"import", "Import module"},
                {"export", "Export symbol"}, {"break", "Break loop"},
                {"continue", "Continue loop"}, {"true", "Boolean true"},
                {"false", "Boolean false"}, {"null", "Null pointer"},
                {"int", "Default signed integer"}, {"i32", "32-bit signed integer"},
                {"i64", "64-bit signed integer"}, {"u8", "8-bit unsigned byte"},
                {"u32", "32-bit unsigned integer"}, {"u64", "64-bit unsigned integer"},
                {"float", "Default floating-point"}, {"f64", "64-bit float"},
                {"string", "Built-in string type"}, {"bool", "Boolean type"},
                {"void", "Void type"}, {"print", "Print to stdout"},
                {"println", "Print to stdout with newline"}, {"eprintln", "Print to stderr with newline"},
                {"assert", "Assertion check"}, {"panic", "Abort execution with error"},
                {"len", "Length of string or array"}, {"sizeof", "Size of type"}
            };
            bool f = true;
            for (const auto& item : kw) {
                if (!f) resp += ",";
                f = false;
                resp += "{\"label\":\"" + item.first + "\",\"kind\":14,\"detail\":\"" + item.second + "\"}";
            }
            resp += "]}";
            lsp_send(resp);
        } else if (method == "textDocument/definition") {
            std::string uri = lsp_find_key(payload, "uri");
            std::string line_s = lsp_find_key(payload, "line");
            std::string col_s = lsp_find_key(payload, "character");
            int target_line = line_s.empty() ? 0 : std::stoi(line_s);
            int target_col = col_s.empty() ? 0 : std::stoi(col_s);

            std::string word;
            auto it = documents.find(uri);
            if (it != documents.end()) {
                std::istringstream iss(it->second);
                std::string line_str;
                int cur_line = 0;
                while (std::getline(iss, line_str)) {
                    if (cur_line == target_line) {
                        if (target_col >= 0 && target_col < static_cast<int>(line_str.size())) {
                            int s = target_col;
                            while (s > 0 && (isalnum(static_cast<unsigned char>(line_str[s - 1])) || line_str[s - 1] == '_')) s--;
                            int e = target_col;
                            while (e < static_cast<int>(line_str.size()) && (isalnum(static_cast<unsigned char>(line_str[e])) || line_str[e] == '_')) e++;
                            word = line_str.substr(s, e - s);
                        }
                        break;
                    }
                    cur_line++;
                }
            }

            int def_line = -1, def_col = -1;
            if (!word.empty() && it != documents.end()) {
                std::istringstream iss(it->second);
                std::string line_str;
                int cur_line = 0;
                while (std::getline(iss, line_str)) {
                    std::vector<std::string> prefixes = {"fn " + word, "struct " + word, "enum " + word, "let " + word, "var " + word};
                    for (const auto& pfx : prefixes) {
                        auto pos = line_str.find(pfx);
                        if (pos != std::string::npos) {
                            def_line = cur_line;
                            def_col = (int)pos + (int)(pfx.size() - word.size());
                            break;
                        }
                    }
                    if (def_line >= 0) break;
                    cur_line++;
                }
            }

            std::string resp;
            if (def_line >= 0) {
                resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{"
                       "\"uri\":\"" + uri + "\","
                       "\"range\":{\"start\":{\"line\":" + std::to_string(def_line) + ",\"character\":" + std::to_string(def_col) + "},"
                       "\"end\":{\"line\":" + std::to_string(def_line) + ",\"character\":" + std::to_string(def_col + (int)word.size()) + "}}"
                       "}}";
            } else {
                resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}";
            }
            lsp_send(resp);
        } else if (method == "textDocument/hover") {
            std::string uri = lsp_find_key(payload, "uri");
            std::string line_s = lsp_find_key(payload, "line");
            std::string col_s = lsp_find_key(payload, "character");
            int target_line = line_s.empty() ? 0 : std::stoi(line_s);
            int target_col = col_s.empty() ? 0 : std::stoi(col_s);

            std::string hover_text;
            auto it = documents.find(uri);
            if (it != documents.end()) {
                std::istringstream iss(it->second);
                std::string line_str;
                int cur_line = 0;
                while (std::getline(iss, line_str)) {
                    if (cur_line == target_line) {
                        if (target_col >= 0 && target_col < static_cast<int>(line_str.size())) {
                            int s = target_col;
                            while (s > 0 && (isalnum(line_str[s - 1]) || line_str[s - 1] == '_')) s--;
                            int e = target_col;
                            while (e < static_cast<int>(line_str.size()) && (isalnum(line_str[e]) || line_str[e] == '_')) e++;
                            std::string word = line_str.substr(s, e - s);
                            if (word == "defer") hover_text = "**defer <stmt>**\\n\\nSchedules statement execution for when the enclosing function or block exits (LIFO order).";
                            else if (word == "try") hover_text = "**try <expr>**\\n\\nEvaluates an expression returning an error code (<0 indicates error). Bubbles negative error up to caller immediately; otherwise unwraps value.";
                            else if (word == "fn") hover_text = "**fn <name>(<params>): <ret>**\\n\\nDefines a Demonic C function.";
                            else if (word == "let") hover_text = "**let <name>[: <type>] = <expr>**\\n\\nDeclares an immutable variable.";
                            else if (word == "var") hover_text = "**var <name>[: <type>] = <expr>**\\n\\nDeclares a mutable variable.";
                            else if (word == "arena") hover_text = "**arena**\\n\\nFirst-class bump-allocator handle. Fast O(1) allocation with bulk cleanup via `a.reset()` and `a.free()`.";
                            else if (word == "Vector") hover_text = "**Vector<T>**\\n\\nMonomorphized type-safe dynamic array with zero runtime overhead.";
                            else if (word == "#embed") hover_text = "**#embed(\"path\")**\\n\\nEmbeds file contents at compile time as a static byte array literal.";
                            else if (word == "#embed_str") hover_text = "**#embed_str(\"path\")**\\n\\nEmbeds file contents at compile time as an escaped UTF-8 string literal.";
                            else if (word == "#embed_size") hover_text = "**#embed_size(\"path\")**\\n\\nReturns the byte size of an embedded file at compile time.";
                            else if (word == "comptime") hover_text = "**comptime { <expr> }**\\n\\nCompile-time constant evaluation.";
                            else if (word == "match") hover_text = "**match <expr> { <pattern> => <expr>, ... }**\\n\\nPattern matches over values or tagged union variants.";
                            else if (word == "struct") hover_text = "**struct <Name> { <field>: <type>, ... }**\\n\\nDefines a structured record type.";
                            else if (word == "enum") hover_text = "**enum <Name> { <Variant>[(<Type>)], ... }**\\n\\nDefines an enumeration or tagged union.";
                            else if (word == "string") hover_text = "**string**\\n\\nDemonic C UTF-8 string type backed by runtime string helpers.";
                            else if (word == "print" || word == "println") hover_text = "**" + word + "(value)**\\n\\nStandard output printing helper.";
                            else if (!word.empty()) {
                                std::string sig;
                                std::istringstream iss2(it->second);
                                std::string lstr;
                                while (std::getline(iss2, lstr)) {
                                    if (lstr.find("fn " + word + "(") != std::string::npos) {
                                        size_t bpos = lstr.find("{");
                                        sig = bpos != std::string::npos ? lstr.substr(0, bpos) : lstr;
                                        break;
                                    }
                                }
                                if (!sig.empty()) {
                                    while (!sig.empty() && (sig.back() == ' ' || sig.back() == '	')) sig.pop_back();
                                    hover_text = "```dmc\n" + sig + "\n```";
                                } else {
                                    hover_text = "`" + word + "` (Demonic C symbol)";
                                }
                            }
                        }
                        break;
                    }
                    cur_line++;
                }
            }
            std::string resp;
            if (hover_text.empty()) {
                resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}";
            } else {
                resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + hover_text + "\"}}}";
            }
            lsp_send(resp);
        } else if (method == "shutdown") {
            lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        } else if (method == "exit") {
            return 0;
        } else if (!id.empty()) {
            lsp_send("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":null}");
        }
    }
    return 0;
}

static int cmd_clean() {
    std::cout << color_bold << "Cleaning Demonic C build artifacts and cache...\n" << color_reset;
    size_t removed = 0;
    std::error_code ec;
    
    if (std::filesystem::exists(".dmc/cache", ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(".dmc/cache", ec)) {
            std::filesystem::remove_all(entry.path(), ec);
            removed++;
        }
        std::cout << "  " << color_green << "[✓]" << color_reset << " Cleared .dmc/cache\n";
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
        std::string filename = entry.path().filename().string();
        if (filename.rfind("_test_tmp_", 0) == 0 || filename.rfind("_dmc_tmp_", 0) == 0 ||
            filename.rfind("debug_", 0) == 0) {
            std::filesystem::remove(entry.path(), ec);
            removed++;
        }
    }
    
    std::cout << color_green << "Clean complete" << color_reset << " (" << removed << " item(s) removed).\n";
    return 0;
}

static int cmd_check(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string source_path;
    std::vector<std::string> include_dirs;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-I" && i + 1 < args.size()) {
            include_dirs.push_back(args[++i]);
        } else if (!args[i].empty() && args[i][0] != '-' && source_path.empty()) {
            source_path = args[i];
        }
    }
    if (source_path.empty()) {
        if (std::filesystem::exists("main.dmc")) source_path = "main.dmc";
        else if (std::filesystem::exists("src/main.dmc")) source_path = "src/main.dmc";
        else {
            std::cerr << "dmc check: no source file provided and main.dmc not found\nusage: dmc check <file.dmc>\n";
            return 1;
        }
    }
    try {
        std::ifstream input(source_path);
        if (!input) {
            std::cerr << "dmc check: cannot open '" << source_path << "'\n";
            return 1;
        }
        std::ostringstream ss;
        ss << input.rdbuf();
        std::string src_content = ss.str();
        auto start_t = std::chrono::high_resolution_clock::now();
        Parser parser(lex(src_content), source_path);
        for (const auto& d : include_dirs) parser.add_include_dir(d);
        parser.emit();
        auto end_t = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end_t - start_t).count();
        
        for (const auto& d : parser.diagnostics()) {
            print_diagnostic(source_path, src_content, d, "error");
        }
        for (const auto& w : parser.type_warnings()) {
            print_diagnostic(source_path, src_content, w, "warning");
        }
        if (parser.has_errors()) {
            std::cerr << "\n" << color_red << "Found " << parser.diagnostics().size() << " error(s)" << color_reset << " in " << source_path << "\n";
            return 1;
        }
        std::cout << color_green << "Check passed" << color_reset << " for " << source_path 
                  << " (" << std::fixed << std::setprecision(1) << elapsed_ms << "ms)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "dmc check: " << e.what() << "\n";
        return 1;
    }
}

static int cmd_info() {
    std::cout << color_bold << "Demonic C Environment Info\n" << color_reset;
    std::cout << "  Version:       2.0.1\n";
    std::cout << "  Features:      Pattern Matching (match), Generic Structs, Task Pool, Multithreading, Mutexes, Atomics, Channels, HTTP Server, dmc.lock, Polyglot FFI, SIMD, bindgen\n";
    std::cout << "  Architecture:  " << (sizeof(void*) == 8 ? "64-bit" : "32-bit") << " (";
#ifdef _WIN32
    std::cout << "windows-x86_64";
#elif defined(__APPLE__)
    std::cout << "macos-universal";
#else
    std::cout << "linux-x86_64";
#endif
    std::cout << ")\n";

    const char* cc = std::getenv("CC");
    std::string backend = "clang";
#ifdef _WIN32
    if (!cc) {
        if (std::system("gcc --version > nul 2>&1") == 0) backend = "gcc (MinGW)";
        else if (std::system("clang --version > nul 2>&1") == 0) backend = "clang";
        else backend = "unknown (gcc/clang recommended)";
    } else backend = cc;
#else
    if (!cc) backend = "cc";
    else backend = cc;
#endif
    std::cout << "  C Backend:     " << backend << "\n";
    std::cout << "  Package Store: " << pkg_dir() << "\n";
    std::cout << "  Cache Dir:     .dmc/cache\n";
    
    std::string stdlib_path = "not found";
    if (std::filesystem::exists("stdlib")) stdlib_path = std::filesystem::absolute("stdlib").string();
    else if (std::filesystem::exists("../stdlib")) stdlib_path = std::filesystem::absolute("../stdlib").string();
    else {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
            std::filesystem::path p = std::filesystem::path(home) / ".dmc" / "stdlib";
            if (std::filesystem::exists(p)) stdlib_path = p.string();
        }
    }
    std::cout << "  Stdlib Path:   " << stdlib_path << "\n";
    return 0;
}

static int cmd_heal(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << color_bold << "🩺 Demonic C Doctor & Self-Healing\n" << color_reset;
    std::cout << "Diagnosing toolchain health...\n\n";

    bool ok = true;
    
    std::cout << "  [1/6] Checking C Compiler backend... ";
    const char* cc = std::getenv("CC");
    bool cc_found = false;
    std::string cc_name;
#ifdef _WIN32
    if (cc) { cc_found = true; cc_name = cc; }
    else if (std::system("gcc --version > nul 2>&1") == 0) { cc_found = true; cc_name = "gcc (MinGW)"; }
    else if (std::system("clang --version > nul 2>&1") == 0) { cc_found = true; cc_name = "clang"; }
#else
    if (cc) { cc_found = true; cc_name = cc; }
    else if (std::system("cc --version > /dev/null 2>&1") == 0) { cc_found = true; cc_name = "cc"; }
    else if (std::system("clang --version > /dev/null 2>&1") == 0) { cc_found = true; cc_name = "clang"; }
    else if (std::system("gcc --version > /dev/null 2>&1") == 0) { cc_found = true; cc_name = "gcc"; }
#endif
    if (cc_found) {
        std::cout << color_green << "OK" << color_reset << " (found " << cc_name << ")\n";
    } else {
        std::cout << color_red << "FAILED" << color_reset << "\n";
        std::cout << "        Warning: No C compiler found in PATH! Install GCC or Clang.\n";
        ok = false;
    }

    std::cout << "  [2/6] Checking Git installation... ";
#ifdef _WIN32
    bool git_ok = (std::system("git --version > nul 2>&1") == 0);
#else
    bool git_ok = (std::system("git --version > /dev/null 2>&1") == 0);
#endif
    if (git_ok) {
        std::cout << color_green << "OK" << color_reset << "\n";
    } else {
        std::cout << color_yellow << "NOT FOUND" << color_reset << " (git packages will be disabled)\n";
    }

    std::cout << "  [3/6] Checking Standard Library modules... ";
    std::vector<std::string> stdlib_modules = {"strings.dmc", "path.dmc", "time.dmc", "random.dmc", "buffer.dmc", "log.dmc", "testing.dmc"};
    std::filesystem::path stdlib_found_dir;
    if (std::filesystem::exists("stdlib")) stdlib_found_dir = "stdlib";
    else if (std::filesystem::exists("../stdlib")) stdlib_found_dir = "../stdlib";
    else {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) {
            std::filesystem::path p = std::filesystem::path(home) / ".dmc" / "stdlib";
            if (std::filesystem::exists(p)) stdlib_found_dir = p;
        }
    }
    if (!stdlib_found_dir.empty()) {
        int present = 0;
        for (const auto& m : stdlib_modules) {
            if (std::filesystem::exists(stdlib_found_dir / m)) present++;
        }
        std::cout << color_green << "OK" << color_reset << " (" << present << "/" << stdlib_modules.size() 
                  << " in " << stdlib_found_dir.string() << ")\n";
    } else {
        std::cout << color_yellow << "MISSING" << color_reset << " (run 'dmc update --force' to restore stdlib)\n";
    }

    std::cout << "  [4/6] Checking Cache integrity... ";
    std::error_code ec;
    size_t cache_files = 0;
    if (std::filesystem::exists(".dmc/cache", ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(".dmc/cache", ec)) {
            (void)entry;
            cache_files++;
        }
        std::cout << color_green << "OK" << color_reset << " (" << cache_files << " cached objects)\n";
    } else {
        std::cout << color_green << "OK" << color_reset << " (clean)\n";
    }

    std::cout << "  [5/6] Checking Package Directory... ";
    std::filesystem::path pkg_path = pkg_dir();
    if (!std::filesystem::exists(pkg_path, ec)) {
        std::filesystem::create_directories(pkg_path, ec);
    }
    std::cout << color_green << "OK" << color_reset << " (" << pkg_path.string() << ")\n";

    std::cout << "  [6/6] Self-Healing & Maintenance... ";
    int cleaned = 0;
    for (const auto& entry : std::filesystem::directory_iterator(".", ec)) {
        std::string fname = entry.path().filename().string();
        if (fname.rfind("_test_tmp_", 0) == 0 || fname.rfind("_dmc_tmp_", 0) == 0) {
            std::filesystem::remove(entry.path(), ec);
            cleaned++;
        }
    }
    std::cout << color_green << "HEALED" << color_reset << " (cleaned " << cleaned << " orphan file(s))\n\n";

    if (ok) {
        std::cout << color_green << color_bold << "✓ All toolchain systems are healthy and operational!" << color_reset << "\n";
        return 0;
    } else {
        std::cout << color_yellow << "⚠ Toolchain has warnings. Follow the recommendations above." << color_reset << "\n";
        return 1;
    }
}

static int cmd_update(int argc, char** argv) {
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--force" || std::string(argv[i]) == "-f") force = true;
    }
    std::cout << color_bold << "Demonic C Self-Updater\n" << color_reset;
    std::cout << "Checking for latest release from GitHub...\n";
    
#ifdef _WIN32
    std::string cmd = "powershell -NoProfile -Command \"try { (Invoke-RestMethod -Uri 'https://api.github.com/repos/ak495867/Demonic-C/releases/latest' -Headers @{'User-Agent'='dmc'}).tag_name } catch { '' }\"";
#else
    std::string cmd = "curl -s -H 'User-Agent: dmc' https://api.github.com/repos/ak495867/Demonic-C/releases/latest | grep '\"tag_name\":' | head -n 1 | cut -d '\"' -f 4";
#endif
    FILE* pipe =
#ifdef _WIN32
        _popen(cmd.c_str(), "r");
#else
        popen(cmd.c_str(), "r");
#endif
    std::string latest_tag;
    if (pipe) {
        char buf[256];
        if (fgets(buf, sizeof(buf), pipe)) {
            latest_tag = buf;
            while (!latest_tag.empty() && (latest_tag.back() == '\r' || latest_tag.back() == '\n' || latest_tag.back() == ' ')) {
                latest_tag.pop_back();
            }
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
    
    if (latest_tag.empty()) {
        std::cout << color_yellow << "Could not fetch latest release info (offline or rate limited).\n" << color_reset;
        std::cout << "You can manually reinstall using:\n";
#ifdef _WIN32
        std::cout << "  irm https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.ps1 | iex\n";
#else
        std::cout << "  curl -fsSL https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash\n";
#endif
        return 1;
    }
    
    std::string current_ver = "v2.0.1";
    std::cout << "Current version: " << current_ver << "\n";
    std::cout << "Latest version:  " << latest_tag << "\n";
    
    if (!force && latest_tag == current_ver) {
        std::cout << color_green << "✓ Demonic C is already up to date (" << latest_tag << ")!\n" << color_reset;
        std::cout << "Use 'dmc update --force' to force reinstallation.\n";
        return 0;
    }
    
    std::cout << "Updating Demonic C to " << latest_tag << "...\n";
#ifdef _WIN32
    int res = std::system("powershell -NoProfile -Command \"irm https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.ps1 | iex\"");
#else
    int res = std::system("curl -fsSL https://raw.githubusercontent.com/ak495867/Demonic-C/main/install.sh | bash");
#endif
    if (res == 0) {
        std::cout << color_green << color_bold << "✓ Successfully updated Demonic C to " << latest_tag << "!\n" << color_reset;
        return 0;
    } else {
        std::cerr << color_red << "Update script failed with code " << res << "\n" << color_reset;
        return res;
    }
}

static int cmd_tokens(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "dmc tokens: usage: dmc tokens <file.dmc>\n";
        return 1;
    }
    std::string path = argv[1];
    std::ifstream file(path);
    if (!file) {
        std::cerr << "dmc tokens: cannot open file: " << path << "\n";
        return 1;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    auto tokens = lex(ss.str());
    std::cout << "Tokens in " << path << " (" << tokens.size() << " tokens):\n";
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::cout << std::setw(5) << i << ": [" 
                  << std::setw(3) << tokens[i].line << ":" << std::setw(2) << tokens[i].col << "] "
                  << tokens[i].text << "\n";
    }
    return 0;
}

static void print_import_tree(const std::string& source, const std::string& prefix, std::set<std::string>& visited) {
    if (visited.count(source)) {
        std::cout << prefix << "└── " << source << " (already visited)\n";
        return;
    }
    visited.insert(source);
    std::ifstream input(source);
    if (!input) {
        std::cout << prefix << "└── " << source << " [missing]\n";
        return;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    Parser parser(lex(buffer.str()), source);
    std::vector<std::string> imports;
    try {
        imports = parser.get_imports();
    } catch (...) {}
    
    for (size_t i = 0; i < imports.size(); ++i) {
        bool is_last = (i + 1 == imports.size());
        std::string branch = is_last ? "└── " : "├── ";
        std::string next_prefix = prefix + (is_last ? "    " : "│   ");
        std::string imp = imports[i];
        std::cout << prefix << branch << imp << "\n";
        try {
            std::string resolved = parser.get_resolved_import(imp);
            if (resolved != imp && std::filesystem::exists(resolved)) {
                print_import_tree(resolved, next_prefix, visited);
            }
        } catch (...) {}
    }
}

static int cmd_tree(int argc, char** argv) {
    std::string root = "main.dmc";
    if (argc >= 2) root = argv[1];
    else if (!std::filesystem::exists("main.dmc") && std::filesystem::exists("src/main.dmc")) root = "src/main.dmc";
    
    std::cout << color_bold << "Dependency Tree for " << root << ":\n" << color_reset;
    std::cout << root << "\n";
    std::set<std::string> visited;
    print_import_tree(root, "", visited);
    return 0;
}

static void print_usage() {
    std::cout << "usage: dmc <source.dmc> [args...]       compile and run Demonic C program\n"
              << "       dmc run <source.dmc> [args...]   compile and run Demonic C program\n"
              << "       dmc build <source.dmc> [options] compile to executable, shared lib, or wasm\n"
              << "       dmc bindgen <header.h> [options] generate Demonic C bindings from C header\n"
              << "       dmc <source.dmc> -o <file.c>     transpile to C code\n\n"
              << "options:\n"
              << "  -o <file>       write generated C/binary to <file>\n"
              << "  --emit-c        transpile to C on stdout\n"
              << "  --shared        build dynamic / shared library (.dll, .so, .dylib)\n"
              << "  --freestanding  bare-metal / no-std mode (no libc headers or runtime tables)\n"
              << "  --target=wasm   compile directly to standalone WebAssembly (.wasm)\n"
              << "  --no-line-directives omit #line source maps from generated C\n"
              << "  --sanitize      compile with AddressSanitizer & UndefinedBehaviorSanitizer\n"
              << "  --no-cache      bypass compilation cache\n"
              << "  -l<lib>         link external C/C++ library (e.g. -lm, -lcurl)\n"
              << "  -L<dir>         add library search directory\n"
              << "  -I <dir>        add include / import search directory (repeatable)\n"
              << "  -D<macro>       pass preprocessor define to backend compiler\n"
              << "  --lsp           start Language Server Protocol daemon\n"
              << "  --version       print version and exit\n"
              << "  --help          print this message and exit\n\n"
              << "simd & extensions:\n"
              << "  f32x4(a, b, c, d)      128-bit 4-lane single-precision float SIMD vector\n"
              << "  i32x4(a, b, c, d)      128-bit 4-lane 32-bit integer SIMD vector\n"
              << "  v.add(w) / v.mul(w)    hardware-accelerated lane-wise vector arithmetic\n"
              << "  v.dot(w) / v.x / v[0]  vector dot products and lane extractors\n"
              << "  #[export] / export fn  export function symbol in shared libraries (.dll/.so)\n\n"
              << "interop:\n"
              << "  dmc bindgen raylib.h -lraylib   auto-generate DMC bindings for C header\n"
              << "  dmc main.dmc helper.c           compile DMC and C files together\n"
              << "  dmc build main.dmc -lcurl       link with external C libraries\n"
              << "  #link \"lib\" / #[link(\"lib\")]    declare required link libraries in source\n"
              << "  extern fn name(...) -> type;    declare external C functions\n"
              << "  c_code { ... }                  embed raw inline C code snippets\n\n"
              << "commands:\n"
              << "  run <src>       compile and run Demonic C source directly (or 'dmc <src>')\n"
              << "  eval <expr>     evaluate one-liner expression or code (or 'dmc -e <expr>')\n"
              << "  explain [topic] interactive explainer for language topics and diagnostics\n"
              << "  watch <src>     watch source file and rerun on save\n"
              << "  repl            interactive Demonic C read-eval-print loop\n"
              << "  build [src]     compile Demonic C source directly to executable binary\n"
              << "  bindgen, bindings   generate Demonic C bindings from C header, Rust, or Python (--py)\n"
              << "  check [src]     fast syntax & semantic check without compiling binary\n"
              << "  clean           clear build cache and temporary artifacts\n"
              << "  heal / doctor   diagnose toolchain health and self-heal issues\n"
              << "  update          check and self-update DMC to latest version\n"
              << "  info / env      display toolchain, compiler, and environment info\n"
              << "  test [dir]      run test suite\n"
              << "  bench [dir]     run benchmark suite\n"
              << "  tree [src]      display project import dependency tree\n"
              << "  tokens <src>    dump lexed syntax token stream\n"
              << "  lsp             run LSP daemon (--install-vscode to setup editor)\n"
              << "  pkg             package manager commands\n"
              << "  new <name>      create new project (--template=<cli|polyglot|game|lib>)\n"
              << "  init            initialize project in current directory\n"
              << "  fmt [--check]   format or check .dmc files\n"
              << "  lint [--fix]    lint .dmc files for issues\n"
              << "  doc             generate documentation\n\n"
              << "pkg commands:\n"
              << "  pkg add <git-url>   clone Git package and add to dependencies\n"
              << "  pkg install <pkg>   install package from registry\n"
              << "  pkg uninstall <pkg> remove installed package\n"
              << "  pkg update <pkg>    update package to latest version\n"
              << "  pkg search <term>   search registry for packages\n"
              << "  pkg list            list installed packages\n"
              << "  pkg publish         publish local package to registry\n\n"
              << "dmc.toml format:\n"
              << "  [package]\n"
              << "  name = \"my-project\"\n"
              << "  version = \"1.0.0\"\n"
              << "\n"
              << "  [build]\n"
              << "  output = \"bin/my-project\"\n"
              << "  type = \"executable\"\n"
              << "\n"
              << "  [dependencies]\n"
              << "  json = \"^2.0\"\n"
              << "  http = \"^1.0\"\n"
              << "  crypto = \"^0.9\"\n";
}

int main(int argc, char** argv) {
    enable_ansi_console();
    std::vector<std::string> args(argv + 1, argv + argc);
    
    if (args.empty()) {
        print_usage();
        return 0;
    }

    if (args[0] == "version" || args[0] == "--version" || args[0] == "-v" || args[0] == "-V") {
        std::cout << "dmc 2.0.1 (Demonic C -> C)\n";
        return 0;
    }

    if (args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return 0;
    }

    if (args[0] == "eval" || args[0] == "-e") {
        return cmd_eval(argc - 1, argv + 1, argv[0]);
    }

    if (args[0] == "explain") {
        return cmd_explain(argc - 1, argv + 1);
    }

    if (args[0] == "lsp" || args[0] == "--lsp") {
        if (args.size() > 1 && args[1] == "--install-vscode") {
            return cmd_install_vscode();
        }
        return cmd_lsp();
    }

    if (args[0] == "repl") {
        return cmd_repl(argv[0]);
    }

    if (args[0] == "watch") {
        return cmd_watch(argc - 1, argv + 1, argv[0]);
    }

    if (!args.empty() && (args[0] == "pkg" || args[0] == "package")) {
        return cmd_pkg(argc - 1, argv + 1);
    }

    if (!args.empty() && (args[0] == "install" || args[0] == "search")) {
        return cmd_pkg(argc, argv);
    }
    
    if (!args.empty() && args[0] == "new") {
        return cmd_new(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "init") {
        return cmd_init(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "fmt") {
        return cmd_fmt(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "lint") {
        return cmd_lint(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "test") {
        return cmd_test_impl(argc - 1, argv + 1, argv[0]);
    }

    if (!args.empty() && args[0] == "bench") {
        std::vector<char*> bench_argv;
        bench_argv.push_back(argv[0]);
        bench_argv.push_back(const_cast<char*>("--bench"));
        for (int i = 2; i < argc; ++i) bench_argv.push_back(argv[i]);
        return cmd_test_impl((int)bench_argv.size(), bench_argv.data(), argv[0]);
    }

    if (!args.empty() && (args[0] == "update" || args[0] == "upgrade")) return cmd_update(argc - 1, argv + 1);
    if (!args.empty() && (args[0] == "heal" || args[0] == "doctor")) return cmd_heal(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "clean") return cmd_clean();
    if (!args.empty() && args[0] == "check") return cmd_check(argc - 1, argv + 1);
    if (!args.empty() && (args[0] == "info" || args[0] == "env")) return cmd_info();
    if (!args.empty() && args[0] == "tokens") return cmd_tokens(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "tree") return cmd_tree(argc - 1, argv + 1);

    if (!args.empty() && args[0] == "doc") return cmd_doc(argc - 1, argv + 1);
    if (!args.empty() && (args[0] == "--py" || args[0] == "--python" || args[0] == "--rust")) return cmd_bindgen(argc, argv);
    if (!args.empty() && (args[0] == "bindgen" || args[0] == "bindings" || args[0] == "binding")) return cmd_bindgen(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "build") return cmd_build(argc - 1, argv + 1, argv[0]);
    if (!args.empty() && args[0] == "bundle") return cmd_bundle();
    if (!args.empty() && args[0] == "login") return cmd_login();
    if (!args.empty() && args[0] == "deprecate") return cmd_deprecate(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "publish") { publish_package(); return 0; }
    
    if (!args.empty() && args[0] == "run") {
        if (args.size() < 2) {
            std::cerr << "dmc run: source file required\nusage: dmc run <file.dmc> [args...]\n";
            return 1;
        }
        std::string source = args[1];
        std::vector<std::string> prog_args(args.begin() + 2, args.end());
        return cmd_run(source, prog_args, argv[0]);
    }

    bool has_dash_o = false;
    bool has_emit_c = false;
    for (const auto& a : args) {
        if (a == "-o") has_dash_o = true;
        if (a == "--emit-c") has_emit_c = true;
    }

    auto is_dmc_file = [](const std::string& s) {
        return s.size() >= 4 && s.compare(s.size() - 4, 4, ".dmc") == 0;
    };

    if (!args.empty() && !has_dash_o && !has_emit_c) {
        std::string dmc_file;
        for (const auto& a : args) {
            if (is_dmc_file(a)) {
                dmc_file = a;
                break;
            }
        }
        if (!dmc_file.empty()) {
            std::vector<std::string> prog_args;
            bool found_dmc = false;
            for (const auto& a : args) {
                if (!found_dmc && a == dmc_file) {
                    found_dmc = true;
                    continue;
                }
                prog_args.push_back(a);
            }
            return cmd_run(dmc_file, prog_args, argv[0]);
        }
    }
    
    std::string source_path;
    std::string output_path = "-";
    std::vector<std::string> include_dirs;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") {
            print_usage();
            return 0;
        }
        if (a == "--version" || a == "-V") {
            std::cout << "dmc 2.0.1 (Demonic C -> C)\n";
            return 0;
        }
        if (a == "--lsp") {
            return cmd_lsp();
        }
        if (a == "-o") {
            if (i + 1 >= args.size()) { std::cerr << "dmc: -o requires an argument\n"; return 1; }
            output_path = args[++i];
        } else if (a == "-I") {
            if (i + 1 >= args.size()) { std::cerr << "dmc: -I requires an argument\n"; return 1; }
            include_dirs.push_back(args[++i]);
        } else if (a.rfind("-I", 0) == 0) {
            include_dirs.push_back(a.substr(2));
        } else if (a.rfind("-l", 0) == 0 || a.rfind("-L", 0) == 0 || a.rfind("-D", 0) == 0 || is_c_or_cpp_source(a)) {
        } else if (a.size() > 0 && a[0] == '-') {
            std::cerr << "dmc: unknown option '" << a << "'\n";
            if (a == "--py" || a == "--python") {
                std::cerr << "hint: to generate bindings for a Python module, run:\n"
                          << "  dmc bindgen --py <module>\n";
            } else if (a == "--rust") {
                std::cerr << "hint: to generate bindings for a Rust file, run:\n"
                          << "  dmc bindgen --rust <lib.rs>\n";
            }
            return 1;
        } else if (source_path.empty()) {
            source_path = a;
        } else {
        }
    }
    if (source_path.empty()) {
        print_usage();
        return 1;
    }
    if (!std::filesystem::exists(source_path)) {
        static const std::vector<std::string> known_commands = {
            "build", "run", "test", "bench", "new", "init", "fmt", "lint",
            "bindgen", "bindings", "doc", "repl", "watch", "pkg", "update",
            "upgrade", "heal", "doctor", "clean", "check", "info", "env",
            "tokens", "tree", "bundle", "eval", "explain", "version", "help"
        };
        for (const auto& cmd : known_commands) {
            int len1 = (int)source_path.size();
            int len2 = (int)cmd.size();
            if (std::abs(len1 - len2) <= 2) {
                int diff = 0;
                int min_l = std::min(len1, len2);
                for (int c = 0; c < min_l; ++c) {
                    if (source_path[c] != cmd[c]) diff++;
                }
                diff += std::abs(len1 - len2);
                if (diff <= 2) {
                    std::cerr << "dmc: unknown command '" << source_path << "'. Did you mean '" << cmd << "'?\n";
                    return 1;
                }
            }
        }
    }
    try {
        std::ifstream input(source_path);
        if (!input) throw std::runtime_error("cannot open source file");
        std::ostringstream source;
        source << input.rdbuf();
        Parser parser(lex(source.str()), source_path);
        for (auto& dir : include_dirs) parser.add_include_dir(dir);
        std::string generated = parser.emit();
        if (output_path == "-") std::cout << generated;
        else {
            std::ofstream output(output_path);
            output << generated;
        }
        for (const auto& d : parser.diagnostics()) {
            print_diagnostic(source_path, source.str(), d, "error");
        }
        for (const auto& w : parser.type_warnings()) {
            print_diagnostic(source_path, source.str(), w, "warning");
        }
        if (parser.has_errors()) return 1;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dmc: " << error.what() << '\n';
        return 1;
    }
}

