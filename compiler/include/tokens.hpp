#ifndef DMC_TOKENS_HPP
#define DMC_TOKENS_HPP

#include <string>
#include <vector>

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

#endif
