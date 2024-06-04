#include "common.hpp"

bool test1(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO(a, b, c) a ## b # c
FOO(xyz, 123, str);
#define BAR # c
BAR;
)";
    auto expected =
R"(
xyz123 "str";

# c;
)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test2(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
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
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test3(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
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
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test4(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
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
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test5(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
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
    return expect_ok(preprocessor, includer, in_src, expected, options, 2);
}

bool test6(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define ARRAY(a, b, c) {(a), (b), (c)}
#define TYPE unsigned int
TYPE test[]ARRAY(
    1, 2, 3
);
)";
    auto expected =
R"(

unsigned int test[]{(1), (2), (3)}

;
)";
    std::string_view options[]{"-D", "FOO=123"};
    return expect_ok(preprocessor, includer, in_src, expected, options, 2);
}

bool test7(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define BAR Foo(2d)
#define Foo(x) foo##x
int BAR;
)";
    auto expected =
R"(

int foo2d;
)";
    return expect_ok(preprocessor, includer, in_src, expected, nullptr, 0);
}

bool test8(pep::cprep::Preprocessor &preprocessor, pep::cprep::ShaderIncluder &includer) {
    auto in_src =
R"(#define FOO1(a, b, ...) func(a, b __VA_OPT__(,) __VA_ARGS__)
#define FOO2(a, b, ...) FOO1(a, b __VA_OPT__(,) __VA_ARGS__)
FOO2(var1, var2);
FOO2(var1, var2, var3);
)";
    auto expected =
R"(

func(var1, var2  );
func(var1, var2 , var3);
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
    pass &= test7(preprocessor, includer);
    pass &= test8(preprocessor, includer);

    return pass ? 0 : 1;
}
