#include <cprep/cprep.hpp>

#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <format>

#include "tokenize.hpp"

namespace pep::cprep {

namespace {

struct Define final {
    std::string_view replace;
    std::vector<std::string_view> params;
    bool function_like = false;
    bool has_va_params = false;
    std::string_view file;
    size_t lineno = 0;
};

struct FileState final {
    std::string_view path;
    std::string_view content;
};

constexpr size_t kMaxMacroExpandDepth = 512;

struct PreprocessError final {
    std::string err_str;

    const char *what() const { return err_str.c_str(); }
};

struct StringHash final {
    using is_transparent = void;

    size_t operator()(const std::string &s) const noexcept { return std::hash<std::string_view>{}(s); }
    size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    size_t operator()(const char *s) const noexcept { return std::hash<std::string_view>{}(s); }
};

std::string_view trim_string_view(std::string_view s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && std::isblank(s[start])) { ++start; }
    while (start < end && std::isblank(s[end - 1])) { --end; }
    return s.substr(start, end - start);
}

std::string stringify(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (auto ch : input) {
        if (ch == '"') {
            output += "\\\"";
        } else if (ch == '\\') {
            output += "\\\\";
        } else {
            output += ch;
        }
    }
    return output;
}

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

        try {
            parse_source();
        } catch (const PreprocessError &e) {
            //
            parsed_result.clear();
        }
        clear_states();
        return parsed_result;
    }

    void init_states(std::string_view input_path, std::string_view input_content) {
        auto it = parsed_files.insert(std::string{input_path}).first;
        files.push({*it, input_content});
        inputs.emplace(input_content);
        parsed_result.clear();
        parsed_result.reserve(input_content.size());
    }

    void clear_states() {
        defines.clear();
        parsed_files.clear();
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

    void parse_source() {
        while (true) {
            auto token = get_next_token(inputs.top(), parsed_result);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                files.pop();
                if (files.empty()) { break; }
            } else if (token.type == TokenType::eUnknown) {
                parsed_result += token.value;
                throw PreprocessError{std::format(
                    "at file '{}' line {}, failed to parse a valid token",
                    files.top().path, inputs.top().get_lineno()
                )};
            }

            const auto line_start = inputs.top().at_line_start();
            inputs.top().set_line_start(false);

            if (line_start && token.type == TokenType::eSharp) {
                parse_directive();
            } else if (token.type == TokenType::eIdentifier) {
                if (auto it = defines.find(token.value); it != defines.end()) {
                    parsed_result += replace_macro(token.value, it->second);
                } else if (token.value == "__FILE__") {
                    parsed_result += files.top().path;
                } else if (token.value == "__LINE__") {
                    parsed_result += inputs.top().get_lineno();
                } else {
                    parsed_result += token.value;
                }
            }
        }
    }

    void parse_directive() {
        auto token = get_next_token(inputs.top(), parsed_result, false);
        if (token.type != TokenType::eIdentifier) {
            if (token.type != TokenType::eEof) {
                throw PreprocessError{std::format(
                    "at file '{}' line {}, token after '#' must be an identifier",
                    files.top().path, inputs.top().get_lineno()
                )};
            }
            return;
        }

        if (token.value == "error") {
            // TODO
        } else if (token.value == "warning") {
            // TODO
        } else if (token.value == "include") {
            // TODO
        } else if (token.value == "define") {
            // TODO
        } else if (token.value == "undef") {
            // TODO
        } else if (token.value == "pragma") {
            // TODO
        } else if (token.value == "ifdef") {
            // TODO
        } else if (token.value == "if") {
            // TODO
        } else if (token.value == "else") {
            // TODO
        } else if (token.value == "elifdef") {
            // TODO
        } else if (token.value == "elif") {
            // TODO
        } else if (token.value == "endif") {
            // TODO
        } else {
            // TODO
        }
    }

    std::string replace_macro(
        std::string_view macro_name, const Define &macro, bool is_param = false, size_t depth = 1
    ) {
        if (depth > kMaxMacroExpandDepth) {
            return std::string{macro_name};
        }

        if (depth == 1 && !is_param) {
            curr_file = files.top().path;
            curr_line = inputs.top().get_lineno();
        }

        std::string result_phase1{};
        std::vector<std::string_view> args;
        if (macro.function_like) {
            std::string fallback{macro_name};
            args.reserve(macro.params.size());
            auto token = get_next_token(inputs.top(), fallback);
            if (token.type != TokenType::eLeftBracketRound) {
                fallback += token.value;
                return fallback;
            }
            size_t num_brackets = 0;
            auto last_begin = token.value.end();
            while (true) {
                token = get_next_token(inputs.top(), fallback);
                if (token.type == TokenType::eEof) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, when replacing function-like macro '{}', "
                        "find end of input before finding corresponding ')'",
                        curr_file, curr_line, macro_name
                    )};
                }
                if (token.type == TokenType::eUnknown) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, when replacing function-like macro '{}', failed to parse a valid token",
                        curr_file, curr_line, macro_name
                    )};
                }
                if (token.type == TokenType::eLeftBracketRound) {
                    ++num_brackets;
                } else if (token.type == TokenType::eRightBracketRound) {
                    if (num_brackets == 0) {
                        args.push_back(trim_string_view({last_begin, token.value.begin()}));
                        break;
                    } else {
                        --num_brackets;
                    }
                } else if (token.type == TokenType::eComma) {
                    if (num_brackets == 0) {
                        args.push_back(trim_string_view({last_begin, token.value.begin()}));
                        last_begin = token.value.end();
                    }
                }
            }
            if (macro.has_va_params) {
                if (args.size() < macro.params.size()) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, when replacing function-like macro '{}', "
                        "the macro needs at least {} arguments but {} are given",
                        curr_file, curr_line, macro_name, macro.params.size(), args.size()
                    )};
                }
            } else {
                if (args.size() != macro.params.size()) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, when replacing function-like macro '{}', "
                        "the macro needs {} arguments but {} are given",
                        curr_file, curr_line, macro_name, macro.params.size(), args.size()
                    )};
                }
            }
        }

        auto err_loc = std::format(
            "at file '{}' line {}, when replacing macro{} '{}' (defined at file '{}' line {})",
            curr_file, curr_line, is_param ? " parameter" : "", macro_name, macro.file, macro.lineno
        );
        inputs.emplace(macro.replace);

        // replace macro parameters and __VA_ARGS__, do stringification and concatenation
        auto token = get_next_token(inputs.top(), result_phase1);
        std::string concat_str{};
        while (true) {
            auto next_token = get_next_token(inputs.top(), result_phase1);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                break;
            } else if (token.type == TokenType::eUnknown) {
                throw PreprocessError{std::format("{}, failed to parse a valid token", err_loc)};
            }
            // stringify
            if (token.type == TokenType::eSharp) {
                // TODO check this in define instead
                if (next_token.type != TokenType::eIdentifier) {
                    throw PreprocessError{std::format("{}, token after '#' must be an identifier", err_loc)};
                }
                if (next_token.value == "__VA_ARGS__") {
                    if (!macro.has_va_params) {
                        throw PreprocessError{std::format(
                            "{}, '__VA_ARGS__' is used but macro doesn't have variable number of paramters",
                            err_loc
                        )};
                    }
                    for (size_t i = macro.params.size(); i < args.size(); i++) {
                        result_phase1 += i == macro.params.size() ? '"' : ' ';
                        result_phase1 += stringify(args[i]);
                    }
                    result_phase1 += '"';
                } else {
                    auto it = std::find(macro.params.begin(), macro.params.end(), token.value);
                    if (it == macro.params.end()) {
                        throw PreprocessError{std::format(
                            "{}, identifier after '#' must be a macro parameter",
                            err_loc
                        )};
                    }
                    result_phase1 += '"' + stringify(args[it - macro.params.begin()]) + '"';
                }
                token = next_token;
                continue;
            }
            // concat
            if (next_token.type == TokenType::eDoubleSharp) {
                next_token = get_next_token(inputs.top(), result_phase1);
                if (concat_str.empty()) {
                    concat_str = token.value;
                }
                concat_str += next_token.value;
                token = next_token;
                continue;
            } else if (!concat_str.empty()) {
                result_phase1 += concat_str;
                result_phase1 += " ";
                token = next_token;
                continue;
            }
            // others
            if (token.type == TokenType::eIdentifier) {
                if (macro.has_va_params) {
                    if (token.value == "__VA_ARGS__") {
                        for (size_t i = macro.params.size(); i < args.size(); i++) {
                            if (i > macro.params.size()) {
                                result_phase1 += ", ";
                            }
                            Define param{
                                .replace = args[i],
                                .file = macro.file,
                                .lineno = macro.lineno,
                            };
                            result_phase1 += replace_macro(token.value, param, true, depth);
                        }
                    }
                }
                if (
                    auto it = std::find(macro.params.begin(), macro.params.end(), token.value);
                    it != macro.params.end()
                ) {
                    Define param{
                        .replace = args[it - macro.params.begin()],
                        .file = macro.file,
                        .lineno = macro.lineno,
                    };
                    result_phase1 += replace_macro(token.value, param, true, depth);
                } else {
                    result_phase1 += token.value;
                }
            } else {
                result_phase1 += token.value;
            }
            token = next_token;
        }

        // replace macro in replaced string
        inputs.emplace(result_phase1);
        std::string result{};
        token = get_next_token(inputs.top(), result);
        while (true) {
            auto next_token = get_next_token(inputs.top(), result);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                break;
            }
            if (token.type == TokenType::eIdentifier) {
                if (auto it = defines.find(token.value); it != defines.end()) {
                    result += replace_macro(token.value, it->second, false, depth + 1);
                } else if (macro.has_va_params && token.value == "__VA_OPT__") {
                    if (next_token.type == TokenType::eLeftBracketRound) {
                        size_t num_brackets = 0;
                        auto opt_true = args.size() > macro.params.size();
                        while (true) {
                            next_token = get_next_token(inputs.top(), result);
                            if (next_token.type == TokenType::eEof) {
                                break;
                            } else if (next_token.type == TokenType::eLeftBracketRound) {
                                ++num_brackets;
                                if (opt_true) { result += token.value; }
                            } else if (next_token.type == TokenType::eRightBracketRound) {
                                if (num_brackets == 0) {
                                    break;
                                }
                                --num_brackets;
                                if (opt_true) { result += token.value; }
                            } else {
                                if (opt_true) { result += token.value; }
                            }
                        }
                        // first token after ')' used for next loop
                        next_token = get_next_token(inputs.top(), result);
                    } else {
                        result += token.value;
                    }
                } else {
                    result += token.value;
                }
            } else {
                result += token.value;
            }
            token = next_token;
        }

        return result;
    }

    std::unordered_map<std::string_view, Define> defines;
    std::unordered_set<std::string, StringHash, std::equal_to<>> parsed_files;
    std::stack<FileState> files;
    std::stack<InputState> inputs;
    std::string parsed_result{};
    std::string_view curr_file; // used in replace_macro
    size_t curr_line; // used in replace_macro
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
