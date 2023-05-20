#pragma once

#include <string>
#include <functional>

namespace pep::cprep {

class ShaderIncluder {
public:
    struct Result {
        std::string_view header_content;
        std::string header_path;
    };

    virtual ~ShaderIncluder() = default;

    virtual bool require_header(std::string_view header_name, std::string_view file_path, Result &result) const = 0;
};

class Preprocesser final {
public:
    Preprocesser();
    ~Preprocesser();

    Preprocesser(const Preprocesser &rhs) = delete;
    Preprocesser &operator=(const Preprocesser &rhs) = delete;

    Preprocesser(Preprocesser &&rhs);
    Preprocesser &operator=(Preprocesser &&rhs);

    std::string do_preprocess(
        std::string_view input_path,
        std::string_view input_content,
        ShaderIncluder *includer,
        std::string_view *options = nullptr,
        size_t num_options = 0
    );

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}
