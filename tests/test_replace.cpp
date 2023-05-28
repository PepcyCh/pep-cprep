#include "common.hpp"

bool test1(pep::cprep::Preprocesser &preprocesser, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO(a, b, c) a ## b # c
FOO(xyz, 123, str);
)";
    auto expected =
R"(
xyz123 "str";
)";
    return expect_ok(preprocesser, includer, in_src, expected, nullptr, 0);
}

bool test2(pep::cprep::Preprocesser &preprocesser, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO(a, b) a + b
FOO(1, 2);
FOO(,);
)";
    auto expected =
R"(
1 + 2;
 + ;
)";
    return expect_ok(preprocesser, includer, in_src, expected, nullptr, 0);
}

bool test3(pep::cprep::Preprocesser &preprocesser, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO(...) #__VA_ARGS__;
FOO(a)
FOO(a,  b)
FOO(a,  b, "\n")
FOO(a,  b, R"_(\n)_")
)";
    auto expected =
R"(
"a";
"a, b";
"a, b, \"\\n\"";
"a, b, R\"_(\\n)_\"";
)";
    return expect_ok(preprocesser, includer, in_src, expected, nullptr, 0);
}

bool test4(pep::cprep::Preprocesser &preprocesser, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO(sname, ...) sname foo __VA_OPT__({__VA_ARGS__});
FOO(Foo)
FOO(Foo, )
FOO(Foo, a, b, c)
)";
    auto expected =
R"(
Foo foo ;
Foo foo ;
Foo foo {a, b, c};
)";
    return expect_ok(preprocesser, includer, in_src, expected, nullptr, 0);
}

bool test5(pep::cprep::Preprocesser &preprocesser, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define BAR(...) __VA_ARGS__ ## OO
BAR();
BAR(,);
BAR(a, b);
BAR(a, F);
)";
    auto expected =
R"(
OO;
, OO;
a, bOO;
a, 123;
)";
    std::string_view options[]{"-D", "FOO=123"};
    return expect_ok(preprocesser, includer, in_src, expected, options, 2);
}

int main() {
    pep::cprep::Preprocesser preprocesser{};
    pep::cprep::EmptyInclude includer{};

    auto pass = true;

    pass &= test1(preprocesser, includer);
    pass &= test2(preprocesser, includer);
    pass &= test3(preprocesser, includer);
    pass &= test4(preprocesser, includer);
    pass &= test5(preprocesser, includer);

    return pass ? 0 : 1;
}