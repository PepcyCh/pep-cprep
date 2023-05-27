#include <iostream>

#include <cprep/cprep.hpp>

int main() {
    auto in_src = R"(
#define FOO abc
int func() { return 1'000'000'000u; }
#if defined(FOO) & 0
void func2() {}
#elif 1 + 2 * 3 == 9
struct Foo {
    A<Foo> a;
};
#endif
#define assert(expr) do { \
        if (!(expr)) { \
            abort(); \
        } \
    }
assert(1);
#define STRINGIFY(x) #x
#define STRINGIFY_MACRO(x) STRINGIFY(x)
STRINGIFY(FOO);
STRINGIFY_MACRO(FOO);
    )";

    pep::cprep::Preprocesser preprocesser{};
    pep::cprep::EmptyInclude includer{};

    auto result = preprocesser.do_preprocess("/", in_src, includer);
    std::cout << "result:\n" << result.parsed_result << std::endl;
    std::cout << "error:\n" << result.error << std::endl;
    std::cout << "warning:\n" << result.warning << std::endl;

    return 0;
}
