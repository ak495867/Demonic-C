#ifndef DMC_TYPES_HPP
#define DMC_TYPES_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

static inline std::string ctype(const std::string& type) {
    if (type == "string") return "const char*";
    if (type == "str") return "dmc_str";
    if (type == "const unsigned char*") return "const unsigned char*";
    if (type == "void*" || type == "*void") return "void*";
    if (type == "usize") return "size_t";
    if (type == "isize") return "ptrdiff_t";
    if (type.size() >= 2 && type.size() <= 3 && (type[0] == 'u' || type[0] == 'i')) {
        bool all_digits = true;
        for (size_t i = 1; i < type.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(type[i]))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            int width = std::stoi(type.substr(1));
            if (width >= 1 && width <= 64) {
                if (type[0] == 'u') {
                    if (width <= 8) return "uint8_t";
                    if (width <= 16) return "uint16_t";
                    if (width <= 32) return "uint32_t";
                    return "uint64_t";
                } else {
                    if (width <= 8) return "int8_t";
                    if (width <= 16) return "int16_t";
                    if (width <= 32) return "int32_t";
                    return "int64_t";
                }
            }
        }
    }
    if (type == "i32" || type == "int") return "int";
    if (type == "u32") return "unsigned int";
    if (type == "i64" || type == "u64") return "long long";
    if (type == "u8") return "unsigned char";
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
    if (type.size() >= 2 && type.front() == '(' && type.back() == ')') {
        std::string mangled = "Tuple";
        std::string inner = type.substr(1, type.size() - 2);
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t next_comma = inner.find(',', pos);
            std::string sub = (next_comma == std::string::npos) ? inner.substr(pos) : inner.substr(pos, next_comma - pos);
            while (!sub.empty() && std::isspace(static_cast<unsigned char>(sub.front()))) sub.erase(sub.begin());
            while (!sub.empty() && std::isspace(static_cast<unsigned char>(sub.back()))) sub.pop_back();
            std::string ct = ctype(sub);
            if (ct == "const char*") ct = "string";
            else if (ct == "long long" || ct == "int64_t" || ct == "uint64_t") ct = "i64";
            else if (ct == "unsigned char" || ct == "uint8_t" || ct == "int8_t") ct = "u8";
            else if (ct == "unsigned int" || ct == "int" || ct == "uint32_t" || ct == "int32_t") ct = "u32";
            mangled += "_" + ct;
            if (next_comma == std::string::npos) break;
            pos = next_comma + 1;
        }
        for (char& c : mangled) if (c == '*' || c == ' ') c = '_';
        return mangled;
    }
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

static const std::vector<std::string> BUILTIN_MODULES = {"io", "text", "math", "mem", "process", "collections", "low", "json", "datetime", "regex", "db", "ffi", "py", "js"};

static inline bool is_builtin_module(const std::string& name) {
    for (const auto& m : BUILTIN_MODULES) if (m == name) return true;
    return false;
}

static inline int hdl_type_width(std::string type) {
    if (!type.empty() && type.front() == '*') type = type.substr(1);
    if (type == "u1" || type == "i1" || type == "bool") return 1;
    if (type == "u8" || type == "i8" || type == "char") return 8;
    if (type == "u16" || type == "i16") return 16;
    if (type == "u32" || type == "i32" || type == "int") return 32;
    if (type == "u64" || type == "i64" || type == "usize" || type == "isize") return 64;
    if (type.size() >= 2 && (type[0] == 'u' || type[0] == 'i')) {
        bool all_digits = true;
        for (size_t i = 1; i < type.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(type[i]))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            try {
                int w = std::stoi(type.substr(1));
                if (w >= 1 && w <= 128) return w;
            } catch (...) {}
        }
    }
    return 1;
}

#endif
