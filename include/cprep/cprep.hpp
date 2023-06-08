#pragma once

#include <string>

namespace pep::cprep {

class ShaderIncluder {
public:
    struct Result final {
        // derived class should own header content
        std::string_view header_content;
        std::string header_path;
    };

    virtual ~ShaderIncluder() = default;

    virtual bool require_header(std::string_view header_name, std::string_view file_path, Result &result) = 0;

    // derived class can release owned header contents in 'clear()'
    virtual void clear() {}
};

class EmptyInclude final : public pep::cprep::ShaderIncluder {
public:
    bool require_header(std::string_view header_name, std::string_view file_path, Result &result) override {
        return false;
    }
};


class Preprocesser final {
public:
    Preprocesser();
    ~Preprocesser();

    Preprocesser(const Preprocesser &rhs) = delete;
    Preprocesser &operator=(const Preprocesser &rhs) = delete;

    Preprocesser(Preprocesser &&rhs);
    Preprocesser &operator=(Preprocesser &&rhs);

    struct Result final {
        std::string parsed_result;
        std::string error;
        std::string warning;
    };

    Result do_preprocess(
        std::string_view input_path,
        std::string_view input_content,
        ShaderIncluder &includer,
        const std::string_view *options = nullptr,
        size_t num_options = 0
    );

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}
