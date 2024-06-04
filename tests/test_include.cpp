#include "common.hpp"

class TestIncluder final : public pep::cprep::ShaderIncluder {
public:
    bool require_header(std::string_view header_name, std::string_view file_path, Result &result) override {
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

bool test1(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#ifndef FOO
#include "a.hpp"
#endif
#include "a.hpp"
#include <a.hpp>
#define B <b.hpp>
#include B
#include "b.hpp"
int main() {
    return 0;
}
)";
    auto expected =
R"(


#line 1 "/a.hpp"

int func_a();

#line 5 "/test.cpp"


#line 1 "/b.hpp"


int func_b();


#line 8 "/test.cpp"
#line 1 "/b.hpp"





#line 9 "/test.cpp"
int main() {
    return 0;
}
)";
    std::string_view options[]{"-DFOO=1"};
    return expect_ok(preprocessor, includer, in_src, expected, options, 1);
}

bool test2(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if __has_include("a.hpp")
#define FOO 1
#else
#define FOO 0
#endif
#if __has_include("c.hpp")
#define BAR 1
#else
#define BAR 0
#endif
int main() {
    return FOO * BAR;
}
)";
    auto expected =
R"(









int main() {
    return 1 * 0;
}
)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

int main() {
    pep::cprep::Preprocessor preprocessor{};
    TestIncluder includer{};

    auto pass = true;

    pass &= test1(preprocessor, includer);
    pass &= test2(preprocessor, includer);

    return pass ? 0 : 1;
}
