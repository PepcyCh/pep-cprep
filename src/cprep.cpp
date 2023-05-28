#include <cprep/cprep.hpp>

#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <format>

#include "tokenize.hpp"
#include "evaluate.hpp"

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
    std::string_view included_by_path;
    size_t included_by_lineno;
};

enum class IfState {
    eTrue,
    // if...elif...elif..., no expression is true before
    eFalseWithoutTrueBefore,
    // if...elif...elif..., some expression is true before
    eFalseWithTrueBefore,
};
IfState if_state_from_bool(bool cond) { return cond ? IfState::eTrue : IfState::eFalseWithoutTrueBefore; }
IfState if_state_else(IfState state) {
    return state == IfState::eTrue ? IfState::eFalseWithTrueBefore
        : state == IfState::eFalseWithoutTrueBefore ? IfState::eTrue
        : IfState::eFalseWithTrueBefore;
}

constexpr size_t kMaxMacroExpandDepth = 512;

struct PreprocessError final {
    std::string msg;
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
    Result do_preprocess(
        std::string_view input_path,
        std::string_view input_content,
        ShaderIncluder &includer,
        std::string_view *options,
        size_t num_options
    ) {
        init_states(input_path, input_content);
        this->includer = &includer;
        parse_options(options, num_options);

        Result result{};
        result.parsed_result.reserve(input_content.size());
        try {
            parse_source(result);
        } catch (const PreprocessError &e) {
            result.error += e.msg + '\n';
        }
        clear_states();
        return result;
    }

    void init_states(std::string_view input_path, std::string_view input_content) {
        auto it = parsed_files.insert(std::string{input_path}).first;
        files.push({*it, input_content});
        inputs.emplace(input_content);
        if_stack.push(IfState::eTrue);
    }

    void clear_states() {
        defines.clear();
        parsed_files.clear();
        while (!files.empty()) { files.pop(); }
        while (!inputs.empty()) { inputs.pop(); }
        while (!if_stack.empty()) { if_stack.pop(); }
        includer->clear();
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

    void parse_source(Result &result) {
        while (true) {
            auto token = get_next_token(inputs.top(), result.parsed_result);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                auto top_file = files.top();
                files.pop();
                if (files.empty()) {
                    break;
                } else {
                    result.parsed_result += std::format(
                        "#line {} \"{}\"", top_file.included_by_lineno + 1, top_file.included_by_path
                    );
                }
            } else if (token.type == TokenType::eUnknown) {
                result.parsed_result += token.value;
                add_error(result, std::format(
                    "at file '{}' line {}, failed to parse a valid token",
                    files.top().path, inputs.top().get_lineno()
                ));
                inputs.top().set_line_start(false);
                continue;
            }

            const auto line_start = inputs.top().at_line_start();
            inputs.top().set_line_start(false);

            if (line_start && token.type == TokenType::eSharp) {
                parse_directive(result);
            } else if (if_stack.top() == IfState::eTrue) {
                if (token.type == TokenType::eIdentifier) {
                    if (auto it = defines.find(token.value); it != defines.end()) {
                        result.parsed_result += replace_macro(token.value, it->second);
                    } else if (token.value == "__FILE__") {
                        result.parsed_result += files.top().path;
                    } else if (token.value == "__LINE__") {
                        result.parsed_result += inputs.top().get_lineno();
                    } else {
                        result.parsed_result += token.value;
                    }
                } else {
                    result.parsed_result += token.value;
                }
            }
        }

        if (if_stack.size() > 1) {
            throw PreprocessError{"unterminated conditional directive"};
        }
    }

    void parse_directive(Result &result) {
        auto &input = inputs.top();
        auto token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
        if (token.type != TokenType::eIdentifier) {
            if (token.type != TokenType::eEof) {
                throw PreprocessError{std::format(
                    "at file '{}' line {}, expected an identifier after '#'",
                    files.top().path, input.get_lineno()
                )};
            }
            return;
        }

        try {
            bool unknown_directive = true;

            // not conditional directives
            // - error, warning
            // - pragma
            // - define, undef
            // - include
            if (if_stack.top() == IfState::eTrue) {
                if (token.value == "error" || token.value == "warning") {
                    std::string message{};
                    while (true) {
                        token = get_next_token(input, message, false);
                        if (token.type == TokenType::eEof) {
                            break;
                        }
                        message += token.value;
                    }
                    if (token.value == "error") {
                        add_error(result, std::format(
                            "at file '{}' line {}, {}\n",
                            files.top().path, input.get_lineno(), message
                        ));
                    } else {
                        add_warning(result, std::format(
                            "at file '{}' line {}, {}\n",
                            files.top().path, input.get_lineno(), message
                        ));
                    }
                    unknown_directive = false;
                } else if (token.value == "pragma") {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    if (token.type != TokenType::eIdentifier) {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected an identifier after 'pragma'\n",
                            files.top().path, input.get_lineno()
                        )};
                    }
                    if (token.value == "once") {
                        pragma_once_files.insert(files.top().path);
                    } else {
                        add_warning(result, std::format(
                            "at file '{}' line {}, unknown pragma '{}'\n",
                            files.top().path, input.get_lineno(), token.value
                        ));
                    }
                    unknown_directive = false;
                } else if (token.value == "include") {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    std::string_view header_name{};
                    std::string macro_replaced{};
                    InputState macro_input{macro_replaced};
                    InputState *header_input = &input;
                    if (token.type == TokenType::eIdentifier) {
                        if (auto it = defines.find(token.value); it != defines.end()) {
                            macro_replaced = replace_macro(token.value, it->second);
                            macro_input = InputState{macro_replaced};
                            header_input = &macro_input;
                        } else {
                            throw PreprocessError{std::format(
                                "at file '{}' line {}, expected a header file name\n",
                                files.top().path, input.get_lineno()
                            )};
                        }
                        token = get_next_token(*header_input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    }
                    if (token.type == TokenType::eString) {
                        header_name = token.value.substr(1, token.value.size() - 2);
                    } else if (token.type == TokenType::eLess) {
                        auto start = header_input->get_p_curr();
                        while (true) {
                            auto ch = header_input->look_next_ch();
                            if (ch == '>') {
                                header_name = std::string_view{start, header_input->get_p_curr()};
                                header_input->skip_next_ch();
                                break;
                            } else if (ch == EOF || ch == '\n') {
                                throw PreprocessError{std::format(
                                    "at file '{}' line {}, expected a header file name\n",
                                    files.top().path, input.get_lineno()
                                )};
                            }
                            header_input->skip_next_ch();
                        }
                    } else {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected a header file name\n",
                            files.top().path, input.get_lineno()
                        )};
                    }
                    ShaderIncluder::Result include_result{};
                    if (includer->require_header(header_name, files.top().path, include_result)) {
                        if (!pragma_once_files.contains(include_result.header_path)) {
                            auto it = parsed_files.insert(std::move(include_result.header_path)).first;
                            files.push({
                                *it, include_result.header_content,
                                files.top().path, input.get_lineno(),
                            });
                            result.parsed_result += std::format("#line 1 \"{}\"\n", *it);
                            inputs.emplace(include_result.header_content);
                        }
                    } else {
                        add_warning(result, std::format(
                            "at file '{}' line {}, failed to include header '{}'\n",
                            files.top().path, input.get_lineno(), header_name
                        ));
                    }
                    unknown_directive = false;
                } else if (token.value == "define") {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    if (token.type != TokenType::eIdentifier) {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected an identifier after 'define'\n",
                            files.top().path, input.get_lineno()
                        )};
                    }
                    Define macro{
                        .file = files.top().path,
                        .lineno = input.get_lineno(),
                    };
                    auto macro_name = token.value;
                    auto start = input.get_p_curr();
                    if (auto ch = input.look_next_ch(); ch == '(') {
                        input.skip_next_ch();
                        macro.function_like = true;
                        while (true) {
                            token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                            macro.has_va_params = token.type == TokenType::eTripleDots;
                            if (token.type != TokenType::eIdentifier && token.type != TokenType::eTripleDots) {
                                throw PreprocessError{std::format(
                                    "at file '{}' line {}, expected an identifier or '...' when defining macro paramter\n",
                                    files.top().path, input.get_lineno()
                                )};
                            }
                            if (!macro.has_va_params) { macro.params.push_back(token.value); }
                            token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                            if (token.type == TokenType::eRightBracketRound) { break; }
                            if (token.type != TokenType::eComma) {
                                throw PreprocessError{std::format(
                                    "at file '{}' line {}, expected ',' or ')' after a macro paramter\n",
                                    files.top().path, input.get_lineno()
                                )};
                            }
                            if (macro.has_va_params) {
                                throw PreprocessError{std::format(
                                    "at file '{}' line {}, '...' must be the last macro paramter\n",
                                    files.top().path, input.get_lineno()
                                )};
                            }
                        }
                        start = input.get_p_curr();
                    }
                    while (true) {
                        token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                        if (token.type == TokenType::eEof) { break; }
                    }
                    macro.replace = trim_string_view(input.get_substr_to_curr(start));
                    unknown_directive = false;
                    defines.insert({macro_name, std::move(macro)});
                } else if (token.value == "undef") {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    if (token.type != TokenType::eIdentifier) {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected an identifier after 'undef'\n",
                            files.top().path, input.get_lineno()
                        )};
                    }
                    if (auto it = defines.find(token.value); it != defines.end()) {
                        defines.erase(it);
                    }
                    unknown_directive = false;
                }
            }

            // conditional directives
            // - if, ifdef, ifndef
            // - elif, elifdef, elifndef
            // - else
            // - endif
            if (token.value == "ifdef" || token.value == "ifndef") {
                if (if_stack.top() == IfState::eTrue) {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    if (token.type != TokenType::eIdentifier) {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected an identifier after '{}'\n",
                            files.top().path, input.get_lineno(), token.value
                        )};
                    }
                    if_stack.push(if_state_from_bool((token.value == "ifdef") == defines.contains(token.value)));
                } else {
                    if_stack.push(IfState::eFalseWithoutTrueBefore);
                }
            } else if (token.value == "if") {
                if_stack.push(if_state_from_bool(evaluate()));
            } else if (token.value == "else") {
                if (if_stack.size() == 1) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, '#else' without '#if'",
                        files.top().path, input.get_lineno()
                    )};
                }
                if_stack.top() = if_state_else(if_stack.top());
            } else if (token.value == "elifdef" || token.value == "elifndef") {
                if (if_stack.size() == 1) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, '{}' without '#if'",
                        files.top().path, input.get_lineno(), token.value
                    )};
                }
                if (if_stack.top() == IfState::eFalseWithoutTrueBefore) {
                    token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
                    if (token.type != TokenType::eIdentifier) {
                        throw PreprocessError{std::format(
                            "at file '{}' line {}, expected an identifier after '{}'\n",
                            files.top().path, input.get_lineno(), token.value
                        )};
                    }
                    if_stack.top() = if_state_from_bool((token.value == "elifdef") == defines.contains(token.value));
                } else {
                    if_stack.top() = IfState::eFalseWithTrueBefore;
                }
            } else if (token.value == "elif") {
                if (if_stack.size() == 1) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, '#elif' without '#if'",
                        files.top().path, input.get_lineno()
                    )};
                }
                if (if_stack.top() == IfState::eFalseWithoutTrueBefore) {
                    if_stack.top() = if_state_from_bool(evaluate());
                } else {
                    if_stack.top() = IfState::eFalseWithTrueBefore;
                }
            } else if (token.value == "endif") {
                if (if_stack.size() == 1) {
                    throw PreprocessError{std::format(
                        "at file '{}' line {}, '#endif' without '#if'",
                        files.top().path, input.get_lineno()
                    )};
                }
                if_stack.pop();
            } else if (unknown_directive) {
                add_warning(result, std::format(
                    "at file '{}' line {}, unknown directive '{}'",
                    files.top().path, input.get_lineno(), token.value
                ));
            }
        } catch (const PreprocessError &e) {
            add_error(result, e.msg);
        }

        // forward to line end
        while (true) {
            token = get_next_token(input, result.parsed_result, false, SpaceKeepType::eNewLine);
            if (token.type == TokenType::eEof) { break; }
        }
    }

    bool evaluate() {
        std::string replaced{};
        auto err_loc = std::format("at file '{}' line {}", files.top().path, inputs.top().get_lineno());

        // replace macro and defined()
        while (true) {
            auto token = get_next_token(inputs.top(), replaced, false);
            if (token.type == TokenType::eEof) { break; }
            if (token.type == TokenType::eUnknown) {
                throw PreprocessError{std::format("{}, failed to parse a valid token", err_loc)};
            }
            if (token.type == TokenType::eIdentifier) {
                if (auto it = defines.find(token.value); it != defines.end()) {
                    replaced += replace_macro(token.value, it->second);
                } else if (token.value == "defined") {
                    token = get_next_token(inputs.top(), replaced, false);
                    bool value;
                    if (token.type == TokenType::eIdentifier) {
                        value = defines.contains(token.value);
                    } else {
                        if (token.type != TokenType::eLeftBracketRound) {
                            throw PreprocessError{std::format(
                                "{}, expected a '(' or an identifier after 'defined'", err_loc
                            )};
                        }
                        token = get_next_token(inputs.top(), replaced, false);
                        if (token.type != TokenType::eIdentifier) {
                            throw PreprocessError{std::format("{}, expected an identifier inside 'defined'", err_loc)};
                        }
                        value = defines.contains(token.value);
                        token = get_next_token(inputs.top(), replaced, false);
                        if (token.type != TokenType::eRightBracketRound) {
                            throw PreprocessError{std::format("{}, expected a ')' after 'defined'", err_loc)};
                        }
                    }
                    replaced += value ? "1" : "0";
                } else if (token.value == "true") {
                    replaced += "1";
                } else {
                    // both 'false' and unknown identifier are replaced with '0'
                    replaced += "0";
                }
            } else {
                replaced += token.value;
            }
        }

        // evaluate expression
        try {
            InputState input{replaced};
            return evaluate_expression(input);
        } catch (const EvaluateError &e) {
            throw PreprocessError{std::format("{}, {}", err_loc, e.msg)};
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

        // parse arguments for function-like macro
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
            auto last_begin = token.value.data() + token.value.size();
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
                        args.push_back(trim_string_view({last_begin, token.value.data()}));
                        break;
                    } else {
                        --num_brackets;
                    }
                } else if (token.type == TokenType::eComma) {
                    if (num_brackets == 0) {
                        args.push_back(trim_string_view({last_begin, token.value.data()}));
                        last_begin = token.value.data() + token.value.size();
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
        std::string result_phase1{};
        auto token = get_next_token(inputs.top(), result_phase1, true, SpaceKeepType::eSpace);
        std::string spaces{};
        std::string concat_str{};
        while (true) {
            auto next_token = get_next_token(inputs.top(), spaces, true, SpaceKeepType::eSpace);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                break;
            } else if (token.type == TokenType::eUnknown) {
                inputs.pop();
                throw PreprocessError{std::format("{}, failed to parse a valid token", err_loc)};
            }
            // stringify
            if (token.type == TokenType::eSharp) {
                if (next_token.type != TokenType::eIdentifier) {
                    inputs.pop();
                    throw PreprocessError{std::format("{}, expected a macro parameter after '#'", err_loc)};
                }
                if (next_token.value == "__VA_ARGS__") {
                    if (!macro.has_va_params) {
                        inputs.pop();
                        throw PreprocessError{std::format(
                            "{}, '__VA_ARGS__' is used after '#' but macro doesn't have variable number of paramters",
                            err_loc
                        )};
                    }
                    for (size_t i = macro.params.size(); i < args.size(); i++) {
                        result_phase1 += i == macro.params.size() ? '"' : ' ';
                        result_phase1 += stringify(args[i]);
                    }
                    result_phase1 += '"';
                } else {
                    auto it = std::find(macro.params.begin(), macro.params.end(), next_token.value);
                    if (it == macro.params.end()) {
                        inputs.pop();
                        throw PreprocessError{std::format(
                            "{}, expected a macro parameter after '#'",
                            err_loc
                        )};
                    }
                    result_phase1 += '"' + stringify(args[it - macro.params.begin()]) + '"';
                }
                token = get_next_token(inputs.top(), result_phase1, true, SpaceKeepType::eSpace);
                spaces.clear();
                continue;
            }
            // concat
            if (next_token.type == TokenType::eDoubleSharp) {
                next_token = get_next_token(inputs.top(), spaces, true, SpaceKeepType::eSpace);
                if (concat_str.empty()) {
                    concat_str = token.value;
                }
                concat_str += next_token.value;
                token = next_token;
                spaces.clear();
                continue;
            } else if (!concat_str.empty()) {
                result_phase1 += concat_str;
                result_phase1 += spaces;
                token = next_token;
                spaces.clear();
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
            result_phase1 += spaces;
            spaces.clear();
        }

        // replace macro in replaced string
        inputs.emplace(result_phase1);
        std::string result{};
        while (true) {
            token = get_next_token(inputs.top(), result);
            if (token.type == TokenType::eEof) {
                inputs.pop();
                break;
            }
            if (token.type == TokenType::eIdentifier) {
                if (auto it = defines.find(token.value); it != defines.end()) {
                    result += replace_macro(token.value, it->second, false, depth + 1);
                } else if (macro.has_va_params && token.value == "__VA_OPT__") {
                    token = get_next_token(inputs.top(), result);
                    if (token.type == TokenType::eLeftBracketRound) {
                        size_t num_brackets = 0;
                        auto opt_true = args.size() > macro.params.size();
                        while (true) {
                            token = get_next_token(inputs.top(), result);
                            if (token.type == TokenType::eEof) {
                                break;
                            } else if (token.type == TokenType::eLeftBracketRound) {
                                ++num_brackets;
                                if (opt_true) { result += token.value; }
                            } else if (token.type == TokenType::eRightBracketRound) {
                                if (num_brackets == 0) {
                                    break;
                                }
                                --num_brackets;
                                if (opt_true) { result += token.value; }
                            } else {
                                if (opt_true) { result += token.value; }
                            }
                        }
                    } else {
                        result += token.value;
                    }
                } else {
                    result += token.value;
                }
            } else {
                result += token.value;
            }
        }

        return result;
    }

    void add_error(Result &result, std::string_view msg) {
        result.error += std::format("error: {}\n", msg);
    }
    void add_warning(Result &result, std::string_view msg) {
        result.warning += std::format("warning: {}\n", msg);
    }

    std::unordered_map<std::string_view, Define> defines;
    std::unordered_set<std::string, StringHash, std::equal_to<>> parsed_files;
    std::unordered_set<std::string_view> pragma_once_files;
    std::stack<FileState> files;
    std::stack<InputState> inputs;
    std::stack<IfState> if_stack;
    ShaderIncluder *includer = nullptr;
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

Preprocesser::Result Preprocesser::do_preprocess(
    std::string_view input_path,
    std::string_view input_content,
    ShaderIncluder &includer,
    std::string_view *options,
    size_t num_options
) {
    return impl_->do_preprocess(input_path, input_content, includer, options, num_options);
}

}
