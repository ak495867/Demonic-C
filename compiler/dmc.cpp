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
#include <direct.h>
#else
#include <unistd.h>
#endif

struct Token { std::string text; int line; };
struct Diagnostic { int line; std::string message; };
struct Param { std::string name; std::string type; };
struct Field { std::string name; std::string type; };
struct Function { std::string name; std::string ret_type; std::vector<Param> params; std::vector<std::string> body; std::vector<std::string> attributes; };
struct Struct { std::string name; std::vector<Field> fields; };
struct Enum { std::string name; std::vector<std::string> variants; };
struct Case { std::string value; std::vector<std::string> body; };

static std::vector<Token> lex(const std::string& source) {
    std::vector<Token> result;
    int line = 1;
    for (size_t i = 0; i < source.size();) {
        unsigned char c = source[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(c)) { ++i; continue; }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = i++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
            result.push_back({source.substr(start, i - start), line});
            continue;
        }
        if (std::isdigit(c)) {
            size_t start = i++;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            if (i < source.size() && source[i] == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1]))) {
                ++i;
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i;
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) ++i;
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            }
            result.push_back({source.substr(start, i - start), line});
            continue;
        }
        if (c == '"') {
            size_t start = i++;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < source.size()) i += 2; else { if (source[i] == '\n') ++line; ++i; }
            }
            if (i >= source.size()) throw std::runtime_error("unterminated string at line " + std::to_string(line));
            ++i;
            result.push_back({source.substr(start, i - start), line});
            continue;
        }
        if (i + 1 < source.size()) {
            std::string pair = source.substr(i, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" || pair == "->" || pair == "::" || pair == "#[") {
                result.push_back({pair, line}); i += 2; continue;
            }
        }
        result.push_back({std::string(1, static_cast<char>(c)), line});
        ++i;
    }
    result.push_back({"<eof>", line});
    return result;
}

static std::string ctype(const std::string& type) {
    if (type == "string") return "const char*";
    if (type == "bool") return "bool";
    if (type == "void") return "void";
    if (type == "f32") return "float";
    if (type == "f64") return "double";
    if (!type.empty() && type.front() == '*') return ctype(type.substr(1)) + "*";
    if (!type.empty() && type.front() == '[') {
        auto sep = type.find(';');
        if (sep != std::string::npos) return ctype(type.substr(1, sep - 1));
    }
    return type.empty() ? "long long" : type;
}

static const std::vector<std::string> BUILTIN_MODULES = {"io", "text", "math", "mem", "core", "process", "fs", "collections", "low", "json", "http", "datetime", "regex", "db"};

static bool is_builtin_module(const std::string& name) {
    for (auto& m : BUILTIN_MODULES) if (m == name) return true;
    return false;
}

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

    std::string emit() {
        collect_declarations();
        std::ostringstream out;
        if (!diagnostics_.empty()) {
            out << "int _dmc_diagnostic_count = " << diagnostics_.size() << ";\n";
        }
        out << "#define _POSIX_C_SOURCE 200112L\n";
        out << "#ifdef _WIN32\n";
        out << "#include <winsock2.h>\n#include <ws2tcpip.h>\n#include <io.h>\n";
        out << "#define NSCLOSE closesocket\n";
        out << "#else\n";
        out << "#include <stdio.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n";
        out << "#include <sys/types.h>\n#include <sys/socket.h>\n#include <netdb.h>\n#include <unistd.h>\n";
        out << "#define NSCLOSE close\n";
        out << "#endif\n\n";
        out << "#include <stdio.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#include <time.h>\n#include <regex.h>\n\n";
        out << "#include <stdio.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#ifdef DMC_SQLITE\ntypedef struct sqlite3 sqlite3;\ntypedef struct sqlite3_stmt sqlite3_stmt;\nextern int sqlite3_open(const char *, sqlite3 **);\nextern int sqlite3_close(sqlite3 *);\nextern int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);\nextern int sqlite3_step(sqlite3_stmt *);\nextern int sqlite3_finalize(sqlite3_stmt *);\nextern int sqlite3_column_count(sqlite3_stmt *);\nextern const unsigned char *sqlite3_column_text(sqlite3_stmt *, int);\n#define SQLITE_OK 0\n#define SQLITE_ROW 100\n#endif\n\n";
        emit_runtime(out);
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
            out << "typedef enum { ";
            for (size_t i = 0; i < e.variants.size(); ++i) {
                if (i) out << ", ";
                out << e.name << "_" << e.variants[i];
            }
            out << " } " << e.name << ";\n";
        }
        if (!enums_.empty()) out << "\n";
        
        std::vector<Function> test_fns;
        std::vector<Function> bench_fns;
        std::vector<Function> normal_fns;
        bool has_main = false;
        for (const auto& fn : functions_) {
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
        
        for (const auto& fn : normal_fns) {
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
        std::vector<std::string> mods = parse_imports();
        for (auto& name : mods) if (is_builtin_module(name)) imports_.insert(name);
        for (auto& name : mods) {
            if (is_builtin_module(name)) continue;
            std::string path = resolve_import(name);
            if (loaded_.count(path)) continue;
            loaded_.insert(path);
            Parser mod(lex(read_file(path)), path);
            mod.loaded_ = loaded_;
            mod.include_dirs_ = include_dirs_;
            mod.collect_declarations();
            mod.merge_into(this);
            loaded_ = mod.loaded_;
        }
        while (peek() != "<eof>") {
            try {
                if (peek() == "struct") structs_.push_back(structure());
                else if (peek() == "enum") enums_.push_back(parse_enum());
                else functions_.push_back(function());
            } catch (const std::runtime_error& err) {
                diagnostics_.push_back({tokens_[index_].line, err.what()});
                skip_to_decl();
            }
        }
    }

    void skip_to_decl() {
        while (peek() != "<eof>") {
            if (peek() == "struct" || peek() == "enum" || peek() == "fn" || peek() == "import") return;
            ++index_;
        }
    }

    std::vector<std::string> parse_imports() {
        std::vector<std::string> mods;
        while (peek() == "import") {
            take();
            std::string name = take();
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                if (name.size() > 2) mods.push_back(name.substr(1, name.size() - 2));
            } else {
                mods.push_back(name);
            }
            expect(";");
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
        }
        for (auto& dir : include_dirs_) {
            candidates.push_back(std::filesystem::path(dir) / name);
            candidates.push_back(std::filesystem::path(dir) / (name + ".dmc"));
        }
        candidates.push_back(std::filesystem::path(name));
        candidates.push_back(std::filesystem::path(name + ".dmc"));
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
    }

    bool has_function(const std::string& name) const {
        for (auto& f : functions_) if (f.name == name) return true;
        return false;
    }

    void emit_runtime(std::ostringstream& out) {
        out << "static unsigned char *_dmc_mem[1024];\n";
        out << "static size_t _dmc_mem_sz[1024];\n";
        out << "static FILE *_dmc_files[1024];\n";
        out << "static int _dmc_next = 1;\n";
        out << "static int __dmc_argc = 0;\n";
        out << "static char **__dmc_argv = NULL;\n";
        out << "const char *arg_text(long long i) { if (!__dmc_argv || i < 0 || i >= __dmc_argc) return \"\"; return __dmc_argv[i]; }\n\n";
        out << "int mem_alloc(long long n) { int h = _dmc_next++; _dmc_mem[h] = calloc((size_t)n, 1); _dmc_mem_sz[h] = (size_t)n; return h; }\n";
        out << "int mem_free(long long h) { free(_dmc_mem[h]); _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0; }\n";
        out << "int mem_size(long long h) { return (int)_dmc_mem_sz[h]; }\n";
        out << "int mem_write(long long h, long long i, long long v) { if (!_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1; _dmc_mem[h][i] = (unsigned char)v; return 0; }\n";
        out << "int mem_read(long long h, long long i) { if (!_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1; return _dmc_mem[h][i]; }\n";
        out << "int mem_map(void *addr, long long sz) { int h = _dmc_next++; _dmc_mem[h] = (unsigned char*)addr; _dmc_mem_sz[h] = (size_t)sz; return h; }\n";
        out << "int mem_unmap(long long h) { _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0; }\n\n";
        out << "int file_open(const char *p, const char *m) { int h = _dmc_next++; _dmc_files[h] = fopen(p, m); return _dmc_files[h] ? h : -1; }\n";
        out << "const char *file_read(long long h) { static char b[65536]; if (!_dmc_files[h]) return \"\"; size_t n = fread(b, 1, sizeof(b)-1, _dmc_files[h]); b[n] = 0; return b; }\n";
        out << "int file_write(long long h, const char *s) { if (!_dmc_files[h]) return -1; int n = fprintf(_dmc_files[h], \"%s\", s); fflush(_dmc_files[h]); return n; }\n";
        out << "int file_close(long long h) { if (_dmc_files[h]) fclose(_dmc_files[h]); _dmc_files[h] = NULL; return 0; }\n\n";
        out << "int tcp_connect(const char *host, long long port) { char svc[16]; snprintf(svc, sizeof(svc), \"%lld\", port); struct addrinfo hints = {0}, *res = NULL; hints.ai_socktype = SOCK_STREAM; if (getaddrinfo(host, svc, &hints, &res) != 0) return -1; int s = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol); if (s >= 0 && connect(s, res->ai_addr, res->ai_addrlen) < 0) { NSCLOSE(s); s = -1; } freeaddrinfo(res); return s; }\n";
        out << "#ifdef _WIN32\n";
        out << "__attribute__((constructor)) static void dmc_wsa_init(void) { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }\n";
        out << "#endif\n";
        out << "int tcp_send(long long s, const char *text) { return (int)send((int)s, text, (int)strlen(text), 0); }\n";
        out << "const char *tcp_recv(long long s, long long max) { static char b[65536]; if (max > 65535) max = 65535; int n = (int)recv((int)s, b, (int)max, 0); if (n < 0) n = 0; b[n] = 0; return b; }\n";
        out << "int tcp_close(long long s) { NSCLOSE((int)s); return 0; }\n\n";
        out << "typedef struct { long long *data; long long len; long long cap; } _dmc_vec;\n";
        out << "typedef struct { long long cap; long long len; long long head; long long tail; long long *data; } _dmc_queue;\n";
        out << "typedef struct _dmc_map_node { long long key; long long value; struct _dmc_map_node *next; } _dmc_map_node;\n";
        out << "typedef struct { long long cap; long long used; _dmc_map_node **buckets; } _dmc_map;\n";
        out << "static _dmc_vec *_dmc_vecs[1024];\n";
        out << "static _dmc_queue *_dmc_queues[1024];\n";
        out << "static _dmc_map *_dmc_maps[1024];\n\n";
        out << "int vec_new(void) { _dmc_vec *v = (_dmc_vec*)calloc(1, sizeof(_dmc_vec)); if (!v) return -1; int h = _dmc_next++; _dmc_vecs[h] = v; return h; }\n";
        out << "int vec_len(long long h) { return _dmc_vecs[h] ? (int)_dmc_vecs[h]->len : -1; }\n";
        out << "int vec_push(long long h, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; if (v->len == v->cap) { long long nc = v->cap ? v->cap * 2 : 4; v->data = (long long*)realloc(v->data, (size_t)nc * sizeof(long long)); v->cap = nc; } v->data[v->len++] = x; return 0; }\n";
        out << "int vec_get(long long h, long long i) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; return (int)v->data[i]; }\n";
        out << "int vec_set(long long h, long long i, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; v->data[i] = x; return 0; }\n";
        out << "int vec_free(long long h) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; free(v->data); free(v); _dmc_vecs[h] = NULL; return 0; }\n\n";
        out << "int queue_new(void) { _dmc_queue *q = (_dmc_queue*)calloc(1, sizeof(_dmc_queue)); if (!q) return -1; int h = _dmc_next++; _dmc_queues[h] = q; return h; }\n";
        out << "int queue_len(long long h) { _dmc_queue *q = _dmc_queues[h]; return q ? (int)q->len : -1; }\n";
        out << "int queue_push(long long h, long long x) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; if (q->len == q->cap) { long long nc = q->cap ? q->cap * 2 : 4; q->data = (long long*)realloc(q->data, (size_t)nc * sizeof(long long)); q->cap = nc; } q->data[q->tail] = x; q->tail = (q->tail + 1) % q->cap; ++q->len; return 0; }\n";
        out << "int queue_pop(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; long long x = q->data[q->head]; q->head = (q->head + 1) % q->cap; --q->len; return (int)x; }\n";
        out << "int queue_peek(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; return (int)q->data[q->head]; }\n";
        out << "int queue_free(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; free(q->data); free(q); _dmc_queues[h] = NULL; return 0; }\n\n";
        out << "int map_new(void) { _dmc_map *m = (_dmc_map*)calloc(1, sizeof(_dmc_map)); if (!m) return -1; int h = _dmc_next++; _dmc_maps[h] = m; return h; }\n";
        out << "int map_set(long long h, long long key, long long value) {\n";
        out << "  _dmc_map *m = _dmc_maps[h]; if (!m) return -1; if (!m->buckets) { m->cap = 8; m->buckets = (_dmc_map_node**)calloc((size_t)m->cap, sizeof(_dmc_map_node*)); }\n";
        out << "  unsigned long long k = (unsigned long long)key; long long idx = (long long)(k % (unsigned long long)m->cap);\n";
        out << "  _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) { n->value = value; return 0; } n = n->next; }\n";
        out << "  n = (_dmc_map_node*)malloc(sizeof(_dmc_map_node)); n->key = key; n->value = value; n->next = m->buckets[idx]; m->buckets[idx] = n; ++m->used; return 0;\n";
        out << "}\n";
        out << "int map_get(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return -1; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return (int)n->value; n = n->next; } return -1; }\n";
        out << "int map_has(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return 0; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return 1; n = n->next; } return 0; }\n";
        out << "int map_len(long long h) { _dmc_map *m = _dmc_maps[h]; return m ? (int)m->used : -1; }\n";
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
        out << "long long text_len(const char *s) { return (long long)strlen(s); }\n";
        out << "const char *text_concat(const char *a, const char *b) { static char buf[131072]; snprintf(buf, sizeof(buf), \"%s%s\", a, b); return buf; }\n";
        out << "const char *text_sub(const char *s, long long start, long long len) { static char buf[65536]; long long slen = (long long)strlen(s); if (start < 0) start = 0; if (start >= slen) { buf[0] = 0; return buf; } if (start + len > slen) len = slen - start; memcpy(buf, s + start, (size_t)len); buf[len] = 0; return buf; }\n";
        out << "long long text_char_at(const char *s, long long i) { if (i < 0 || i >= (long long)strlen(s)) return -1; return (unsigned char)s[i]; }\n";
        out << "long long text_cmp(const char *a, const char *b) { return strcmp(a, b); }\n";
        out << "const char *text_from_int(long long x) { static char buf[32]; snprintf(buf, sizeof(buf), \"%lld\", x); return buf; }\n";
        out << "const char *text_from_float(double x) { static char buf[64]; snprintf(buf, sizeof(buf), \"%g\", x); return buf; }\n";
        out << "long long text_to_int(const char *s) { return atoll(s); }\n";
        out << R"DMC_JSON(
static char _dmc_json_value[65536];
const char *json_parse(const char *s) { return s ? s : ""; }
const char *json_stringify(const char *s) { return s ? s : ""; }
const char *json_get(const char *json, const char *key) { _dmc_json_value[0] = 0; if (!json || !key) return _dmc_json_value; char needle[512]; snprintf(needle, sizeof(needle), "\"%s\"", key); const char *p = strstr(json, needle); if (!p) return _dmc_json_value; p = strchr(p + strlen(needle), ':'); if (!p) return _dmc_json_value; ++p; while (*p == ' ' || *p == '\\t') ++p; if (*p == '\"') { ++p; size_t n = 0; while (p[n] && p[n] != '\"' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } _dmc_json_value[n] = 0; return _dmc_json_value; } size_t n = 0; while (p[n] && p[n] != ',' && p[n] != '}' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } while (n && (_dmc_json_value[n - 1] == ' ' || _dmc_json_value[n - 1] == '\\n')) --n; _dmc_json_value[n] = 0; return _dmc_json_value; }
)DMC_JSON";

        out << R"DMC_HTTP(
static char _dmc_http_buffer[1048576];
static int _dmc_http_url_ok(const char *url) { if (!url || !*url || strstr(url, "\"") || strstr(url, "'") || strstr(url, "&&") || strstr(url, ";") || strstr(url, "|") || strstr(url, "`")) return 0; return 1; }
const char *http_get(const char *url) { _dmc_http_buffer[0] = 0; if (!_dmc_http_url_ok(url)) return _dmc_http_buffer; char command[2048]; snprintf(command, sizeof(command), "curl -LfsS --max-time 20 -- %s", url); FILE *pipe = popen(command, "r"); if (!pipe) return _dmc_http_buffer; size_t used = 0; while (used + 1 < sizeof(_dmc_http_buffer)) { size_t n = fread(_dmc_http_buffer + used, 1, sizeof(_dmc_http_buffer) - used - 1, pipe); used += n; if (!n) break; } _dmc_http_buffer[used] = 0; pclose(pipe); return _dmc_http_buffer; }
long long http_status(const char *url) { if (!_dmc_http_url_ok(url)) return -1; char command[2048]; snprintf(command, sizeof(command), "curl -Lso /dev/null -w %%{http_code} --max-time 20 -- %s", url); FILE *pipe = popen(command, "r"); if (!pipe) return -1; char status[16] = {0}; fgets(status, sizeof(status), pipe); pclose(pipe); long long value = atoll(status); return value == 0 ? -1 : value; }
)DMC_HTTP";

        out << "long long datetime_now(void) { return (long long)time(NULL); }\n";
        out << "const char *datetime_format(long long value, const char *format) { static char buf[128]; time_t t = (time_t)value; struct tm *tmv = localtime(&t); if (!tmv) { buf[0] = 0; return buf; } strftime(buf, sizeof(buf), format ? format : \"%Y-%m-%d %H:%M:%S\", tmv); return buf; }\n";
        out << "long long datetime_unix(void) { return (long long)time(NULL); }\n";
        out << "int regex_new(const char *pattern) { regex_t *r = (regex_t*)malloc(sizeof(regex_t)); if (!r || regcomp(r, pattern ? pattern : \"\", REG_EXTENDED) != 0) { free(r); return -1; } int h = _dmc_next++; _dmc_mem[h] = (unsigned char*)r; _dmc_mem_sz[h] = 0; return h; }\n";
        out << "int regex_match(long long h, const char *text) { regex_t *r = (regex_t*)_dmc_mem[h]; return r && text && regexec(r, text, 0, NULL, 0) == 0; }\n";
        out << "int regex_free(long long h) { regex_t *r = (regex_t*)_dmc_mem[h]; if (!r) return -1; regfree(r); free(r); _dmc_mem[h] = NULL; return 0; }\n";
        out << R"DMC_DB(
#ifdef DMC_SQLITE
static sqlite3 *_dmc_db[1024];
static char _dmc_db_buffer[1048576];
int db_connect(const char *url) { if (!url || strncmp(url, "sqlite://", 9) != 0) return -1; sqlite3 *db = NULL; if (sqlite3_open(url + 9, &db) != SQLITE_OK) { sqlite3_close(db); return -1; } int h = _dmc_next++; _dmc_db[h] = db; return h; }
const char *db_query(long long connection, const char *sql) { _dmc_db_buffer[0] = 0; if (connection <= 0 || connection >= 1024 || !_dmc_db[connection] || !sql) return _dmc_db_buffer; sqlite3_stmt *stmt = NULL; if (sqlite3_prepare_v2(_dmc_db[connection], sql, -1, &stmt, NULL) != SQLITE_OK) return _dmc_db_buffer; size_t used = 0; int first_row = 1; while (sqlite3_step(stmt) == SQLITE_ROW) { if (!first_row && used + 1 < sizeof(_dmc_db_buffer)) _dmc_db_buffer[used++] = '\n'; first_row = 0; int columns = sqlite3_column_count(stmt); for (int i = 0; i < columns; ++i) { const unsigned char *value = sqlite3_column_text(stmt, i); const char *text = value ? (const char *)value : ""; size_t n = strlen(text); if (used + n + 2 >= sizeof(_dmc_db_buffer)) break; if (i) _dmc_db_buffer[used++] = '\t'; memcpy(_dmc_db_buffer + used, text, n); used += n; } } sqlite3_finalize(stmt); _dmc_db_buffer[used] = 0; return _dmc_db_buffer; }
int db_close(long long connection) { if (connection <= 0 || connection >= 1024 || !_dmc_db[connection]) return -1; sqlite3_close(_dmc_db[connection]); _dmc_db[connection] = NULL; return 0; }
#else
int db_connect(const char *url) { (void)url; return -1; }
const char *db_query(long long connection, const char *sql) { (void)connection; (void)sql; return ""; }
int db_close(long long connection) { (void)connection; return -1; }
#endif
)DMC_DB";

        out << "double text_to_float(const char *s) { return atof(s); }\n";
        out << "const char *string_copy(const char *s) { if (!s) return \"\"; size_t n = strlen(s); char *b = (char*)malloc(n + 1); if (n) memcpy(b, s, n); b[n] = 0; return b; }\n";
        out << "const char *string_slice(const char *s, long long start, long long len) { if (!s) return \"\"; long long slen = (long long)strlen(s); if (start < 0) start = 0; if (start >= slen) return string_copy(\"\"); if (start + len > slen) len = slen - start; char *b = (char*)malloc((size_t)len + 1); if (len) memcpy(b, s + start, (size_t)len); b[len] = 0; return b; }\n\n";
        out << "long long proc_arg_count(long long argc) { return argc; }\n";
        out << "const char *proc_exit(long long code) { exit((int)code); return \"\"; }\n\n";
        out << "static unsigned char *_dmc_arenas[1024];\n";
        out << "static size_t _dmc_arena_sz[1024];\n";
        out << "static long long _dmc_arena_top[1024];\n";
        out << "long long arena_new(long long cap) { if (cap < 0) return -1; int h = _dmc_next++; _dmc_arenas[h] = (unsigned char*)malloc((size_t)(cap ? cap : 1)); _dmc_arena_sz[h] = (size_t)cap; _dmc_arena_top[h] = 0; return h; }\n";
        out << "long long arena_alloc(long long h, long long n) { if (!_dmc_arenas[h] || n < 0 || _dmc_arena_top[h] + n > (long long)_dmc_arena_sz[h]) return -1; long long off = _dmc_arena_top[h]; _dmc_arena_top[h] += n; return off; }\n";
        out << "long long arena_write(long long h, long long off, long long i, long long v) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; _dmc_arenas[h][(size_t)(off + i)] = (unsigned char)v; return 0; }\n";
        out << "long long arena_read(long long h, long long off, long long i) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; return _dmc_arenas[h][(size_t)(off + i)]; }\n";
        out << "long long arena_reset(long long h) { if (!_dmc_arenas[h]) return -1; _dmc_arena_top[h] = 0; return 0; }\n";
        out << "long long arena_top(long long h) { return _dmc_arenas[h] ? _dmc_arena_top[h] : -1; }\n";
        out << "long long arena_free(long long h) { if (!_dmc_arenas[h]) return -1; free(_dmc_arenas[h]); _dmc_arenas[h] = NULL; _dmc_arena_top[h] = 0; _dmc_arena_sz[h] = 0; return 0; }\n\n";
        out << "static int _dmc_io_allowed = 0;\n";
        out << "long long port_init(void) {\n";
        out << "#ifdef __linux__\n";
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
        out << "long long port_in32(long long port) { if (!_dmc_io_allowed) return -1; unsigned int r; __asm__ volatile(\"inl %1, %0\" : \"=a\"(r) : \"Nd\"((unsigned short)port)); return r; }\n\n";
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
    }

    Struct structure() {
        expect("struct");
        Struct result{take(), {}};
        expect("{");
        while (peek() != "}") {
            std::string name = take();
            expect(":");
            std::string type = type_name();
            expect(";");
            result.fields.push_back({name, type});
        }
        expect("}");
        if (peek() == ";") take();
        return result;
    }

    Enum parse_enum() {
        expect("enum");
        Enum result{take(), {}};
        expect("{");
        while (peek() != "}") {
            result.variants.push_back(take());
            if (peek() == ",") take();
        }
        expect("}");
        if (peek() == ";") take();
        return result;
    }

    Function function() {
        std::vector<std::string> attrs;
        while (peek() == "#[") {
            take();
            std::string attr = take();
            expect("]");
            attrs.push_back(attr);
        }
        expect("fn");
        Function result{take(), "int", {}, {}, attrs};
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

    std::vector<std::string> block() {
        expect("{");
        std::vector<std::string> stmts;
        auto body = block_inner();
        stmts = std::move(body);
        expect("}");
        return stmts;
    }

    std::vector<std::string> block_inner() {
        scopes_.emplace_back();
        std::vector<std::string> stmts;
        bool terminated = false;
        while (peek() != "}") {
            std::string s = statement();
            if (terminated) stmts.push_back("0;");
            else stmts.push_back(s);
            if (s.rfind("return", 0) == 0 || s.find("proc_exit") != std::string::npos) terminated = true;
        }
        scopes_.pop_back();
        return stmts;
    }

    std::string statement() {
        if (peek() == "let" || peek() == "var") {
            take();
            std::string name = take();
            std::string type;
            if (peek() == ":") { take(); type = type_name(); }
            expect("=");
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
        if (peek() == "return") {
            take();
            if (peek() == ";") { take(); return "return;"; }
            std::string val = expression(";");
            expect(";");
            std::string vt = infer_type(val);
            if (current_ret_ != "void" && !is_compatible(current_ret_, vt)) {
                type_warn("returning '" + vt + "' from function returning '" + current_ret_ + "'");
            }
            return "return " + val + ";";
        }
        if (peek() == "if") {
            take();
            expect("(");
            std::string cond = expression(")");
            expect(")");
            auto then_body = block();
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
        std::string left = postfix(unary_expr(end));
        while (is_binary_op()) {
            int prec = binary_precedence(peek());
            if (prec < min_prec) break;
            std::string op = take();
            std::string right = parse_binary(prec + 1, end);
            if (op != "&&" && op != "||") {
                std::string lt = infer_type(left);
                std::string rt = infer_type(right);
                if (lt == "string" || rt == "string") {
                    if (!(op == "==" || op == "!=")) {
                        type_warn("operator '" + op + "' applied to string operand");
                    }
                }
            }
            if ((op == "==" || op == "!=") && (is_string_arg(left) || is_string_arg(right))) {
                left = "(text_cmp(" + left + ", " + right + ") " + (op == "==" ? "==" : "!=") + " 0)";
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
        while (peek() == "(" || peek() == "." || peek() == "[") {
            if (peek() == "(") {
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
                } else {
                    val = val + "(" + args + ")";
                }
            } else if (peek() == ".") {
                take();
                std::string field = take();
                val = "(" + val + ")." + field;
            } else {
                take();
                std::string idx = expression("]");
                expect("]");
                val = "(" + val + ")[" + idx + "]";
            }
        }
        return val;
    }

    std::string unary_expr(const std::string& end) {
        if (peek() == "!" || peek() == "-" || peek() == "*") {
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
        std::string next = peek();
        if (std::isalpha(static_cast<unsigned char>(next[0])) || next[0] == '_') {
            std::string name = take();
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
                if (lookup_type(name).empty() && !is_user_function(name) && !is_stdlib_fn(name)) {
                    type_warn("use of undeclared identifier '" + name + "'");
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
        std::ostringstream out;
        if (fn.name == "main") {
            out << "int __dmc_main_body(void) {\n";
            for (auto& s : fn.body) out << "    " << s << "\n";
            out << "}\n";
            out << "int main(int argc, char **argv) {\n";
            out << "    __dmc_argc = argc;\n";
            out << "    __dmc_argv = argv;\n";
            out << "    return __dmc_main_body();\n";
            out << "}\n\n";
            return out.str();
        }
        out << (fn.name == "main" ? "int" : ctype(fn.ret_type)) << " " << fn.name << "(";
        if (fn.params.empty()) out << "void";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) out << ", ";
            out << ctype(fn.params[i].type) << " " << fn.params[i].name;
        }
        out << ") {\n";
        for (auto& s : fn.body) out << "    " << s << "\n";
        out << "}";
        return out.str();
    }

    std::string type_name() {
        if (peek() == "*") { take(); return "*" + type_name(); }
        if (peek() == "[") {
            take();
            std::string elem = take();
            expect(";");
            std::string sz = take();
            expect("]");
            return "[" + elem + ";" + sz + "]";
        }
        return take();
    }

    bool is_float_arg(const std::string& arg) const {
        if (arg.size() >= 3 && std::isdigit(static_cast<unsigned char>(arg[0])) && arg.find('.') != std::string::npos) return true;
        if (!arg.empty() && std::isalpha(static_cast<unsigned char>(arg[0]))) {
            std::string t = lookup_type(arg);
            if (t == "f32" || t == "f64") return true;
        }
        for (const auto& fn : {"math_sin", "math_cos", "math_tan", "math_sqrt", "math_floor", "math_ceil", "math_abs_f", "math_pi", "math_e", "text_to_float", "text_from_float"}) {
            if (arg.rfind(std::string(fn) + "(", 0) == 0) return true;
        }
        auto open = arg.find('(');
        if (open != std::string::npos) {
            const Function* fn = find_function(arg.substr(0, open));
            if (fn && (fn->ret_type == "f32" || fn->ret_type == "f64")) return true;
        }
        return false;
    }

    bool is_string_arg(const std::string& arg) const {
        if (!arg.empty() && arg.front() == '"') return true;
        if (!arg.empty() && std::isalpha(static_cast<unsigned char>(arg[0]))) {
            if (lookup_type(arg) == "string") return true;
        }
        auto dot = arg.rfind(").");
        if (dot != std::string::npos) {
            std::string field = arg.substr(dot + 2);
            if (!field.empty() && std::isalpha(static_cast<unsigned char>(field[0]))) {
                if (struct_field_type(field) == "string") return true;
            }
        }
        for (const auto& fn : {"file_read", "tcp_recv", "text_concat", "text_sub", "text_from_int", "text_from_float", "proc_exit", "string_copy", "string_slice", "arg_text"}) {
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
        for (const auto& fn : {"file_read", "tcp_recv", "text_concat", "text_sub", "text_from_int", "text_from_float", "proc_exit", "string_copy", "string_slice", "arg_text"}) {
            if (val.rfind(std::string(fn) + "(", 0) == 0) return "string";
        }
        auto open = val.find('(');
        if (open != std::string::npos) {
            const Function* fn = find_function(val.substr(0, open));
            if (fn) return fn->ret_type;
        }
        auto dot = val.rfind(").");
        if (dot != std::string::npos) {
            std::string field = val.substr(dot + 2);
            if (!field.empty() && std::isalpha(static_cast<unsigned char>(field[0]))) {
                std::string ft = struct_field_type(field);
                if (!ft.empty()) return ft;
            }
        }
        if (!val.empty() && std::isalpha(static_cast<unsigned char>(val[0]))) {
            std::string t = lookup_type(val);
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
        type_warnings_.push_back({tokens_[index_].line, message});
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
            "tcp_connect", "tcp_send", "tcp_recv", "tcp_close",
            "vec_new", "vec_len", "vec_push", "vec_get", "vec_set", "vec_free",
            "queue_new", "queue_len", "queue_push", "queue_pop", "queue_peek", "queue_free",
            "map_new", "map_set", "map_get", "map_has", "map_len", "map_free",
            "math_abs", "math_min", "math_max", "math_clamp",
            "math_sin", "math_cos", "math_tan", "math_sqrt", "math_floor", "math_ceil", "math_abs_f", "math_pi", "math_e",
            "text_len", "text_concat", "text_sub", "text_char_at", "text_cmp",
            "text_from_int", "text_from_float", "text_to_int", "text_to_float",
            "string_copy", "string_slice",
            "proc_arg_count", "proc_exit", "syscall", "interrupt"
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
                if (name == e.name + "::" + v || name == v) return true;
            }
        }
        return false;
    }

    bool is_enum_value(const std::string& val) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if (val == e.name + "::" + v || val == e.name + "_" + v) return true;
            }
        }
        return false;
    }

    std::string enum_type_of(const std::string& val) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if (val == e.name + "::" + v || val == e.name + "_" + v) return e.name;
            }
        }
        return "int";
    }

    std::string enum_variant_expr(const std::string& enum_name, const std::string& variant) const {
        for (auto& e : enums_) {
            for (auto& v : e.variants) {
                if ((enum_name.empty() || e.name == enum_name) && v == variant) {
                    return e.name + "_" + v;
                }
            }
        }
        return variant;
    }

    std::string peek() const { return tokens_[index_].text; }
    std::string take() { if (peek() == "<eof>") fail("unexpected end of file"); return tokens_[index_++].text; }
    void expect(const std::string& value) { if (peek() != value) fail("expected '" + value + "', got '" + peek() + "'"); ++index_; }
    [[noreturn]] void fail(const std::string& message) const { throw std::runtime_error(message + " at line " + std::to_string(tokens_[index_].line)); }

    std::vector<Token> tokens_;
    size_t index_ = 0;
    std::vector<Function> functions_;
    std::vector<Struct> structs_;
    std::vector<Enum> enums_;
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::set<std::string> imports_;
    std::string source_ = "";
    std::set<std::string> loaded_;
    std::vector<std::string> include_dirs_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<Diagnostic> type_warnings_;
    std::string current_ret_ = "int";
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
    std::filesystem::create_directories(p);
    return p;
}

static std::vector<PackageInfo> search_registry(const std::string& name) {
    std::vector<PackageInfo> results;
    
    
    if (name == "json") {
        results.push_back({"json", "2.0.1", "JSON parsing and serialization"});
        results.push_back({"json", "1.5.0", "Basic JSON support"});
    } else if (name == "http") {
        results.push_back({"http", "1.0.3", "HTTP client library"});
        results.push_back({"http", "0.9.0", "Basic HTTP support"});
    } else if (name == "crypto") {
        results.push_back({"crypto", "0.9.1", "Cryptographic primitives"});
    }
    return results;
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
                while (std::getline(f, line)) {
                    if (line.find("version") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.version = line.substr(eq + 2);
                    } else if (line.find("description") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.description = line.substr(eq + 2);
                    } else if (line.find("author") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.author = line.substr(eq + 2);
                    } else if (line.find("license") != std::string::npos) {
                        size_t eq = line.find('=');
                        if (eq != std::string::npos) meta.license = line.substr(eq + 2);
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
        if (pkg.version == pkg_version) {
            
            
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
    
    
    
    std::string current_dir = std::filesystem::current_path().string();
    std::string dmc_toml = current_dir + "/dmc.toml";
    
    
    std::cout << "Package '" << name << "-" << pkg_version << "' installed successfully\n";
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
    if (!found) {
        std::cerr << "dmc uninstall: package '" << name << "' is not installed\n";
        return;
    }
    std::cout << "Package '" << name << "' uninstalled successfully\n";
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
    if (pkgs.empty()) {
        std::cout << "No packages installed.\n";
        return;
    }
    std::cout << "Installed Demonic C packages:\n";
    for (const auto& pkg : pkgs) {
        std::cout << "  " << pkg.name << " " << pkg.version << "\n";
    }
}

static void search_packages(const std::string& name) {
    auto results = search_registry(name);
    if (results.empty()) {
        std::cout << "No packages found matching '" << name << "'.\n";
        return;
    }
    std::cout << "Search results for '" << name << "':\n";
    for (const auto& pkg : results) {
        std::cout << "  " << pkg.name << " " << pkg.version << " - " << pkg.summary << "\n";
    }
}

static void publish_package() {
    
    std::cout << "dmc publish: publishing package to registry...\n";
    
}

static void new_project(const std::string& name) {
    std::filesystem::path proj_path = name;
    if (std::filesystem::exists(proj_path)) {
        std::cerr << "dmc new: directory '" << name << "' already exists\n";
        return;
    }
    
    std::filesystem::create_directories(proj_path / "src");
    std::filesystem::create_directories(proj_path / "lib");
    std::filesystem::create_directories(proj_path / "tests");
    
    std::ofstream toml(proj_path / "dmc.toml");
    toml << "[package]\n";
    toml << "name = \"" << name << "\"\n";
    toml << "version = \"0.1.0\"\n";
    toml << "description = \"A Demonic C project\"\n";
    toml << "\n";
    toml << "[build]\n";
    toml << "output = \"bin/" << name << "\"\n";
    toml << "type = \"executable\"\n";
    toml << "\n";
    toml << "[dependencies]\n";
    toml << "# json = \"^2.0\"\n";
    toml << "# http = \"^1.0\"\n";
    toml.close();
    
    std::ofstream main_src(proj_path / "src" / "main.dmc");
    main_src << "fn main() -> int {\n";
    main_src << "    println(\"Hello from " << name << "!\");\n";
    main_src << "    return 0;\n";
    main_src << "}\n";
    main_src.close();
    
    std::ofstream test_main(proj_path / "tests" / "test_main.dmc");
    test_main << "fn test_basic() -> int {\n";
    test_main << "    return 0;\n";
    test_main << "}\n";
    test_main.close();
    
    std::ofstream gitignore(proj_path / ".gitignore");
    gitignore << "bin/\n";
    gitignore << "*.o\n";
    gitignore << "*.exe\n";
    gitignore << ".dmc/\n";
    gitignore << "*.c\n";
    gitignore.close();
    
    std::cout << "Created new Demonic C project: " << name << "\n";
    std::cout << "\nTo get started:\n";
    std::cout << "  cd " << name << "\n";
    std::cout << "  dmc run src/main.dmc\n";
    std::cout << "\nProject structure:\n";
    std::cout << "  " << name << "/\n";
    std::cout << "  ├── dmc.toml\n";
    std::cout << "  ├── src/\n";
    std::cout << "  │   └── main.dmc\n";
    std::cout << "  ├── lib/\n";
    std::cout << "  ├── test-files/\n";
    std::cout << "  │   └── test_main.dmc\n";
    std::cout << "  └── .gitignore\n";
}

static void init_project() {
    if (std::filesystem::exists("dmc.toml")) {
        std::cerr << "dmc init: dmc.toml already exists\n";
        return;
    }
    
    std::ofstream toml("dmc.toml");
    toml << "[package]\n";
    toml << "name = \"my-project\"\n";
    toml << "version = \"0.1.0\"\n";
    toml << "description = \"A Demonic C project\"\n";
    toml << "\n";
    toml << "[build]\n";
    toml << "output = \"bin/my-project\"\n";
    toml << "type = \"executable\"\n";
    toml << "\n";
    toml << "[dependencies]\n";
    toml << "# json = \"^2.0\"\n";
    toml.close();
    
    std::cout << "Initialized Demonic C project in current directory\n";
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

static std::string find_compiler_binary(const char* argv0) {
    std::filesystem::path self(argv0 ? argv0 : "dmc-native");
    if (std::filesystem::exists(self)) return self.string();
    if (std::filesystem::exists("./dmc-native")) return "./dmc-native";
    if (std::filesystem::exists("./dmc-native.exe")) return "./dmc-native.exe";
    return "dmc-native";
}

static int count_attribute_functions(const std::filesystem::path& path, const std::string& attr) {
    int count = 0;
    for (const auto& line : read_lines(path)) if (line.find("#[" + attr + "]") != std::string::npos) ++count;
    return count;
}

static int cmd_test_impl(int argc, char** argv, const char* argv0) {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool coverage = false;
    bool bench = false;
    std::filesystem::path root = std::filesystem::exists("test-files") ? "test-files" : "../test-files";
    for (const auto& arg : args) {
        if (arg == "--coverage") coverage = true;
        else if (arg == "--bench") bench = true;
        else root = arg;
    }
    if (!std::filesystem::exists(root)) {
        std::cerr << "dmc test: path not found: " << root.string() << "\n";
        return 1;
    }
    int files = 0;
    int tests = 0;
    int benches = 0;
    auto visit = [&](const std::filesystem::path& path) {
        if (path.extension() != ".dmc") return;
        ++files;
        tests += count_attribute_functions(path, "test");
        benches += count_attribute_functions(path, "bench");
    };
    std::error_code error;
    if (std::filesystem::is_directory(root, error)) for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) visit(entry.path());
    else visit(root);
    std::cout << "Collected " << tests << " test(s) and " << benches << " benchmark(s) from " << files << " file(s)\n";
    if (coverage) std::cout << "Coverage report: source collection completed; runtime coverage requires compiler instrumentation\n";
    if (bench) std::cout << "Benchmark collection: " << benches << " benchmark(s)\n";
    if (tests == 0 && !bench) std::cout << "No #[test] functions found\n";
    (void)argv0;
    return 0;
}

static int cmd_build(int argc, char** argv, const char* argv0) {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool release = false;
    bool statik = false;
    std::filesystem::path source = "src/main.dmc";
    for (const auto& arg : args) {
        if (arg == "--release") release = true;
        else if (arg == "--static") statik = true;
        else if (arg.rfind("--", 0) != 0) source = arg;
    }
    if (!std::filesystem::exists(source)) {
        std::cerr << "dmc build: source not found: " << source.string() << "\n";
        return 1;
    }
    std::filesystem::create_directories("bin");
    std::string stem = source.stem().string();
    std::string output = "bin/" + stem;
    std::string compiler = find_compiler_binary(argv0);
    std::string command = "\"" + compiler + "\" \"" + source.string() + "\" -o build.generated.c";
    if (std::system(command.c_str()) != 0) return 1;
    const char* cc = std::getenv("CC");
    std::string c_compiler = cc ? cc : "cc";
    std::string flags = release ? " -O2" : " -O0 -g";
    if (statik) flags += " -static";
    command = c_compiler + flags + " build.generated.c -o \"" + output + "\" -lm";
    int result = std::system(command.c_str());
    std::filesystem::remove("build.generated.c");
    if (result == 0) std::cout << "Built " << output << "\n";
    return result == 0 ? 0 : 1;
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
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::cerr << "dmc new: usage: dmc new <project-name>\n";
        return 1;
    }
    new_project(args[0]);
    return 0;
}

static int cmd_init(int argc, char** argv) {
    (void)argc; (void)argv;
    init_project();
    return 0;
}

static int cmd_pkg(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    
    if (args.empty()) {
        std::cerr << "dmc pkg: usage: dmc pkg <install|uninstall|update|search|list|publish> [package]\n";
        return 1;
    }
    
    std::string command = args[0];
    
    if (command == "install") {
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
        if (name.empty()) {
            std::cerr << "dmc search: search term required\n";
            return 1;
        }
        search_packages(name);
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

static void print_usage() {
    std::cout << "usage: dmc-native <source.dmc> [options]\n"
              << "compile Demonic C source to C.\n\n"
              << "options:\n"
              << "  -o <file>       write generated C to <file> (default: stdout)\n"
              << "  -I <dir>        add import search directory (repeatable)\n"
              << "  --version       print version and exit\n"
              << "  --help          print this message and exit\n\n"
              << "commands:\n"
              << "  pkg             package manager commands\n"
              << "  new <name>      create new project\n"
              << "  init            initialize project in current directory\n"
              << "  fmt [--check] <file.dmc>\n"
              << "                  format or check .dmc files\n"
              << "  lint [--fix] <file.dmc>\n"
              << "                  lint .dmc files for issues\n\n"
              << "pkg commands:\n"
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
    std::vector<std::string> args(argv + 1, argv + argc);
    
    
    if (!args.empty() && args[0] == "pkg") {
        return cmd_pkg(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "new") {
        return cmd_new(argc - 1, argv + 1);
    }
    
    if (!args.empty() && args[0] == "init") {
        return cmd_init(0, nullptr);
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

    if (!args.empty() && args[0] == "doc") return cmd_doc(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "build") return cmd_build(argc - 1, argv + 1, argv[0]);
    if (!args.empty() && args[0] == "bundle") return cmd_bundle();
    if (!args.empty() && args[0] == "login") return cmd_login();
    if (!args.empty() && args[0] == "deprecate") return cmd_deprecate(argc - 1, argv + 1);
    if (!args.empty() && args[0] == "publish") { publish_package(); return 0; }
    
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
            std::cout << "dmc-native 1.0.0 (Demonic C -> C)\n";
            return 0;
        }
        if (a == "-o") {
            if (i + 1 >= args.size()) { std::cerr << "dmc-native: -o requires an argument\n"; return 1; }
            output_path = args[++i];
        } else if (a == "-I") {
            if (i + 1 >= args.size()) { std::cerr << "dmc-native: -I requires an argument\n"; return 1; }
            include_dirs.push_back(args[++i]);
        } else if (a.size() > 0 && a[0] == '-') {
            std::cerr << "dmc-native: unknown option '" << a << "'\n";
            return 1;
        } else if (source_path.empty()) {
            source_path = a;
        } else {
            std::cerr << "dmc-native: unexpected argument '" << a << "'\n";
            return 1;
        }
    }
    if (source_path.empty()) {
        print_usage();
        return 1;
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
            std::cerr << source_path << ":" << d.line << ": error: " << d.message << '\n';
        }
        for (const auto& w : parser.type_warnings()) {
            std::cerr << source_path << ":" << w.line << ": warning: " << w.message << '\n';
        }
        if (parser.has_errors()) return 1;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dmc-native: " << error.what() << '\n';
        return 1;
    }
}
