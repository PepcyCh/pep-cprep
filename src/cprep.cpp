#include <cprep/cprep.hpp>

#include <unordered_map>
#include <unordered_set>
#include <stack>

#include "tokenize.hpp"

namespace pep::cprep {

namespace {

struct Define final {
    std::string_view replace;
    std::string_view params;
    size_t num_params = 0; // -1 for non-function-like macro
    std::string_view file;
    size_t lineno = 0;
};

struct FileState final {
    std::string_view path;
    std::string_view content;
};

}

struct Preprocesser::Impl final {
    std::string do_preprocess(
        std::string_view input_path,
        std::string_view input_content,
        ShaderIncluder *includer,
        std::string_view *options,
        size_t num_options
    ) {
        init_states(input_path, input_content);
        parse_options(options, num_options);

        auto result = parse_source();
        clear_states();
        return result;
    }

    void init_states(std::string_view input_path, std::string_view input_content) {
        files.push({input_path, input_content});
        inputs.emplace(input_content);
    }

    void clear_states() {
        defines.clear();
        while (!files.empty()) { files.pop(); }
        while (!inputs.empty()) { inputs.pop(); }
    }

    void parse_options(std::string_view *options, size_t num_options) {
        std::unordered_set<std::string_view> undefines;

        auto fetch_and_trim_option = [options](size_t i) {
            auto opt = options[i];
            auto opt_b = opt.begin();
            auto opt_e = opt.end();
            while (opt_b < opt_e && std::isblank(*opt_b)) { ++opt_b; }
            while (opt_b < opt_e && std::isblank(*(opt_e - 1))) { --opt_e; }
            return std::make_pair(opt_b, opt_e);
        };

        for (size_t i = 0; i < num_options; i++) {
            auto [opt_b, opt_e] = fetch_and_trim_option(i);
            if (opt_b + 1 >= opt_e || *opt_b != '-') { continue; }
            ++opt_b;
            if (*opt_b == 'D') {
                ++opt_b;
                if (opt_b == opt_e) {
                    ++i;
                    if (i == num_options) { continue; }
                    std::tie(opt_b, opt_e) = fetch_and_trim_option(i);
                }
                auto eq = std::find(opt_b, opt_e, '=');
                std::string_view name{opt_b, eq};
                Define def{};
                if (eq + 1 >= opt_e) {
                    def.replace = "";
                } else {
                    def.replace = std::string_view{eq + 1, opt_e};
                }
                defines.insert({name, def});
            } else if (*opt_b == 'U') {
                ++opt_b;
                if (opt_b == opt_e) {
                    ++i;
                    if (i == num_options) { continue; }
                    std::tie(opt_b, opt_e) = fetch_and_trim_option(i);
                }
                std::string_view name{opt_b, opt_e};
                undefines.insert(name);
            }
        }

        for (auto def : undefines) {
            if (auto it = defines.find(def); it != defines.end()) {
                defines.erase(it);
            }
        }
    }

    std::string parse_source() {
        std::string result{};

        while (true) {
            auto token = get_next_token(inputs.top(), result);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                files.pop();
                if (files.empty()) { break; }
            } else if (token.type == TokenType::eUnknown) {
                result += token.value; // TODO - log error
                break;
            }

            const auto line_start = inputs.top().at_line_start();
            inputs.top().set_line_start(false);

            if (line_start && token.type == TokenType::eSharp) {
                parse_directive();
            } else if (token.type == TokenType::eIdentifier) {
                if (auto it = defines.find(token.value); it != defines.end()) {
                    replace_macro(it->second);
                } else if (token.value == "__FILE__") {
                    result += files.top().path;
                } else if (token.value == "__LINE__") {
                    result += inputs.top().get_lineno();
                } else {
                    result += token.value;
                }
            }
        }

        return result;
    }

    void parse_directive() {
        // TODO
    }

    void replace_macro(const Define &macro, size_t replace_depth = 1) {
        if (macro.num_params != static_cast<size_t>(-1)) {
            // TODO
        }
        inputs.emplace(macro.replace);
    }

    std::unordered_map<std::string_view, Define> defines;
    std::stack<FileState> files;
    std::stack<InputState> inputs;
};

Preprocesser::Preprocesser() {
    impl_ = new Impl{};
}

Preprocesser::~Preprocesser() {
    if (impl_) { delete impl_; }
}

Preprocesser::Preprocesser(Preprocesser &&rhs) {
    impl_ = rhs.impl_;
    rhs.impl_ = nullptr;
}

Preprocesser &Preprocesser::operator=(Preprocesser &&rhs) {
    if (impl_) { delete impl_; }
    impl_ = rhs.impl_;
    rhs.impl_ = nullptr;
    return *this;
}

std::string Preprocesser::do_preprocess(
    std::string_view input_path,
    std::string_view input_content,
    ShaderIncluder *includer,
    std::string_view *options,
    size_t num_options
) {
    return impl_->do_preprocess(input_path, input_content, includer, options, num_options);
}

}
