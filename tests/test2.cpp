#include <iostream>

#include <cprep/cprep.hpp>

class TestIncluder final : public pep::cprep::ShaderIncluder {
public:
    bool require_header(std::string_view header_name, std::string_view file_path, Result &result) const {
        if (header_name == "a.hpp") {
            result.header_path = "/a.hpp";
            result.header_content = "#pragma once\nint func_a();\n";
            return true;
        }
        if (header_name == "b.hpp") {
            result.header_path = "/b.hpp";
            result.header_content = "#ifndef B_HPP_\n#define B_HPP_\nint func_b();\n#endif\n";
            return true;
        }
        return false;
    }
};

int main() {
    auto in_src = R"(
#include "a.hpp"
#include <a.hpp>
#define B <b.hpp>
#include B
#include "b.hpp"
int main() {
    return 0;
}
)";

    pep::cprep::Preprocesser preprocesser{};
    TestIncluder includer{};

    auto result = preprocesser.do_preprocess("/", in_src, includer);
    std::cout << "result:\n" << result.parsed_result << std::endl;
    std::cout << "error:\n" << result.error << std::endl;
    std::cout << "warning:\n" << result.warning << std::endl;

    return 0;
}
