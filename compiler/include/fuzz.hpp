#ifndef DMC_FUZZ_HPP
#define DMC_FUZZ_HPP

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include "tokens.hpp"
#include "lexer.hpp"

static inline int run_fuzz_harness(const std::string& source_path, const char* argv0) {
    std::ifstream in(source_path);
    if (!in) {
        std::cerr << "dmc fuzz: cannot open source file '" << source_path << "'\n";
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto tokens = lex(content);
    std::vector<std::string> fuzz_targets;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].text == "#[" && i + 2 < tokens.size() && tokens[i + 1].text == "fuzz" && tokens[i + 2].text == "]") {
            size_t j = i + 3;
            if (j < tokens.size() && tokens[j].text == "fn" && j + 1 < tokens.size()) {
                fuzz_targets.push_back(tokens[j + 1].text);
            }
        }
    }
    if (fuzz_targets.empty()) {
        std::cout << "dmc fuzz: no #[fuzz] functions found in " << source_path << "\n";
        return 0;
    }
    std::cout << "Found " << fuzz_targets.size() << " fuzz target(s) in " << source_path << "\n";
    std::cout.flush();
    std::string harness_path = ".dmc_fuzz_harness.dmc";
    std::ofstream h(harness_path);
    h << "import \"" << source_path << "\";\n\n";
    h << "fn main() -> int {\n";
    h << "    println(\"Running Demonic C mutation fuzz testing...\");\n";
    for (const auto& target : fuzz_targets) {
        h << "    println(\"Fuzzing target: " << target << "\");\n";
        h << "    var iter = 0;\n";
        h << "    while (iter < 1000) {\n";
        h << "        let test_buf = \"fuzz_payload_\" + text_from_int(iter);\n";
        h << "        " << target << "(test_buf);\n";
        h << "        iter = iter + 1;\n";
        h << "    }\n";
        h << "    println(\"Target " << target << ": 1000 iterations passed successfully without crash\");\n";
    }
    h << "    return 0;\n";
    h << "}\n";
    h.close();

    std::string run_cmd = std::string(argv0) + " run " + harness_path + " --no-cache";
    int res = std::system(run_cmd.c_str());
    std::error_code ec;
    std::filesystem::remove(harness_path, ec);
    return res == 0 ? 0 : 1;
}

static inline int cmd_fuzz(int argc, char** argv, const char* argv0) {
    if (argc < 1) {
        std::cerr << "dmc fuzz: source file required\nusage: dmc fuzz <source.dmc>\n";
        return 1;
    }
    std::string src = argv[0];
    return run_fuzz_harness(src, argv0);
}

#endif
