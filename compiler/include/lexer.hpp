#ifndef DMC_LEXER_HPP
#define DMC_LEXER_HPP

#include "tokens.hpp"
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>

static inline std::vector<Token> lex(const std::string& source) {
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
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" || pair == "<<" || pair == ">>" || pair == "->" || pair == "=>" || pair == "::" || pair == "#[" || pair == ".." || pair == "?*") {
                result.push_back({pair, line, col}); i += 2; col += 2; continue;
            }
        }
        result.push_back({std::string(1, static_cast<char>(c)), line, col});
        ++i; ++col;
    }
    result.push_back({"<eof>", line, col});
    return result;
}

#endif
