# cprep

cprep is a preprocesser for C-like language written in C++20.

## Basic Usage

```c++
#include <cprep/cprep.hpp>

class Includer final : public pep::cprep::ShaderIncluder {
public:
    bool require_header(std::string_view header_name, std::string_view file_path, Result &result) const {
        // handle `include`
        // ...
    }
};

pep::cprep::Preprocesser preprocesser{};
TestIncluder includer{};
auto in_src_path = "foo.cpp";
auto in_src_count = read_all_from_file(in_src_path);
auto result = preprocesser.do_preprocess(in_src_path, in_src_content, includer);

result.parsed_result;
result.error;
result.warning;
```

## Features

Supported directives
* `define`, `undef`
* `if`, `else`, `elif`, `endif`, `ifdef`, `ifndef`
* `elifdef`, `elifndef`
* `error`, `warning`
* `include`
* `pragma once`

Supported features
* basic object-like and function-like macro, stringification `#` and concatenation `##`
* `-D` and `-U` options (both `-Dxxx=xxx` and `-D xxx=xxx` are ok)
* `__FILE__`, `__LINE__`
* variable number of parameters, `__VA_ARGS__`, `__VA_OPT__`
* allow single `'` between numbers in integer or floating-point litteral
* customized include handler
