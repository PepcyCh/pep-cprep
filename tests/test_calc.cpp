#include "common.hpp"

bool test1(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if 1 + 2 * 3 == 9
int foo();
#else
int bar();
#endif
)";
    auto expected =
R"(


int bar();

)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test2(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if (1 + 2) * 3 != 9
int foo();
#else
int bar();
#endif
)";
    auto expected =
R"(


int bar();

)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test3(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if 1 + 2 * 3, 4 == (1 << 3) / 2 && -10 % 3 < 0
int foo();
#else
int bar();
#endif
)";
    auto expected =
R"(
int foo();



)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test4(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if !!!(1'0u ? 2 ? 0 : 3 : 1)
int foo();
#else
int bar();
#endif
)";
    auto expected =
R"(
int foo();



)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test5(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define PART 1 + 2
#if PART * 3 != (PART) * 3
int foo();
#else
int bar();
#endif
)";
    auto expected =
R"(

int foo();



)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test6(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#if FOO
int foo();
#endif
#define BAR true
#if BAR
int bar();
#endif
)";
    auto expected =
R"(




int bar();

)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

int main() {
    pep::cprep::Preprocessor preprocessor{};
    pep::cprep::EmptyInclude includer{};

    auto pass = true;

    pass &= test1(preprocessor, includer);
    pass &= test2(preprocessor, includer);
    pass &= test3(preprocessor, includer);
    pass &= test4(preprocessor, includer);
    pass &= test5(preprocessor, includer);
    pass &= test6(preprocessor, includer);

    return pass ? 0 : 1;
}
