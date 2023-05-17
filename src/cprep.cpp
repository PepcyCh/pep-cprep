#include <cprep/cprep.hpp>

#include <unordered_map>
#include <unordered_set>

namespace pep::cprep {

namespace {

struct Define final {
    std::string_view replace;
    std::string_view params;
    std::string_view file;
    uint32_t num_params = 0;
    uint32_t lineon = 0;
};

struct FileInfo final {
    std::string_view path;
    std::string_view content;
};

enum class TokenType {
    eIdentifier,
    eString,
    eChar,
    eNumber,
    eTripleDots,
    eLeftBracket,
    eRightBracket,
    eOperator,
    eQuestion,
    eColon,
    eSeperator,
    eUnknown,
};

struct Token final {
    TokenType type;
    std::string_view value;
};

bool is_identifier(char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '$';
}

bool is_operator(char ch) {
    static const char *ops = "+-*/%<=>&|^!~#";
    return std::strchr(ops, ch) != nullptr;
}

bool is_seperator(char ch) {
    return ch == ',' || ch == ';';
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
        init_states();
        parse_options(options, num_options);

        curr_file.path = input_path;
        curr_file.content = input_content;
        curr_fp = 0;
        return parse_source();
    }

    void init_states() {
        defines.clear();

        curr_line = {};
        curr_lp = 0;
        curr_lineno = 0;
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

        size_t lineno = 0;
        while (true) {
            get_next_line();
            char ch = EOF;
            while (true) {
                ch = get_next_ch();
                while (std::isblank(ch)) {
                    result += ch;
                    ch = get_next_ch();
                }
                if (ch == '#') {
                    parse_directive();
                } else if (ch == EOF) {
                    break;
                } else if (ch == '\n') {
                    result += ch;
                    ++lineno;
                } else {
                    break;
                }
            }
            if (ch == EOF) { break; }

            while (ch != '\n' && ch != EOF) {
                auto [token_type, token_value] = get_next_token(ch);
                if (token_type == TokenType::eIdentifier) {
                    if (auto it = defines.find(token_value); it != defines.end()) {
                        // TODO - replace
                    } else {
                        result += token_value;
                    }
                } else if (token_type == TokenType::eUnknown) {
                    break;
                } else {
                    result += token_value;
                }

                while (std::isblank(ch = get_next_ch())) { result += ch; }
            }
        }

        return result;
    }

    void parse_directive() {
        char ch;
        while (std::isblank(ch = get_next_ch())) {}
        // TODO
    }

    Token get_next_token(char first_ch) {
        const auto start = get_ch_pos() - 1;
        const Token unknown{TokenType::eUnknown, substr(start)};
        if (first_ch == '"' || first_ch == '\'') {
            auto type = first_ch == '"' ? TokenType::eString : TokenType::eChar;
            bool escape = false;
            while (true) {
                auto ch = get_next_ch();
                if (escape) {
                    escape = false;
                } else if (ch == '\\') {
                    escape = true;
                } else if (ch == first_ch) {
                    break;
                } else if (ch == EOF) {
                    return unknown;
                }
            }
            return {type, substr(start + 1, get_ch_pos() - 1)};
        } else if (is_identifier(first_ch)) {
            while (true) {
                auto ch = look_next_ch();
                if (!is_identifier(ch) && !std::isdigit(ch)) { break; }
                skip_next_ch();
            }
            return {TokenType::eIdentifier, substr(start, get_ch_pos())};
        } else if (std::isdigit(first_ch) || first_ch == '.') {
            auto second_ch = look_next_ch();
            if (second_ch == EOF) { return unknown; }
            // number 0
            if (std::isblank(second_ch)) { return {TokenType::eNumber, substr(start, get_ch_pos())}; }
            // single dot
            if (is_identifier(second_ch)) { return {TokenType::eOperator, substr(start, get_ch_pos())}; }
            // triple dots ...
            if (second_ch == '.') {
                skip_next_ch();
                auto ch = get_next_ch();
                if (ch == '.') {
                    return {TokenType::eTripleDots, substr(start, get_ch_pos())};
                } else {
                    return unknown;
                }
            }
            // number
            bool has_dot = false;
            bool exp_start = false;
            bool last_exp_start = false;
            bool has_exp = false;
            bool can_be_sep = true;
            size_t number_end = -1;
            uint32_t base = 10;
            if (first_ch == '0') {
                skip_next_ch();
                if (second_ch == '\'') {
                    second_ch = get_next_ch();
                    if (!std::isdigit(second_ch)) { return unknown; }
                }
                if (second_ch == 'x' || second_ch == 'X') {
                    base = 16;
                    can_be_sep = false;
                } else if (second_ch == 'b' || second_ch == 'B') {
                    base = 2;
                    can_be_sep = false;
                } else if (second_ch == 'e' || second_ch == 'E') {
                    last_exp_start = true;
                    has_exp = true;
                    can_be_sep = false;
                } else if (std::isdigit(second_ch)) {
                    base = 8;
                } else if (second_ch == '.') {
                    has_dot = true;
                    can_be_sep = false;
                } else {
                    number_end = get_ch_pos() - 1;
                }
            } else if (first_ch == '.') {
                has_dot = true;
                can_be_sep = false;
            }
            while (number_end != static_cast<size_t>(-1)) {
                auto ch = look_next_ch();
                bool last_is_sep = ch == '\'';
                if (last_is_sep) {
                    if (!can_be_sep) { return unknown; }
                    skip_next_ch();
                    ch = look_next_ch();
                }
                if (ch == '.') {
                    if (has_dot || has_exp || last_is_sep || base == 2) { return unknown; }
                    has_dot = true;
                    can_be_sep = false;
                    if (base == 8) { base = 10; }
                } else if (base != 16 && (ch == 'e' || ch == 'E')) {
                    if (has_exp || last_is_sep || base == 2) { return unknown; }
                    exp_start = true;
                    has_exp = true;
                    can_be_sep = false;
                    if (base == 8) { base = 10; }
                } else if (base == 16 && (ch == 'p' || ch == 'P')) {
                    if (has_exp || last_is_sep) { return unknown; }
                    exp_start = true;
                    has_exp = true;
                    can_be_sep = false;
                } else if (ch == '-' && !last_exp_start) {
                    return unknown;
                } else if ((has_exp || has_dot) && (ch == 'f' || ch == 'F')) {
                    number_end = get_ch_pos();
                } else if (('a' <= ch && ch <= 'f') || ('A' <= ch && ch <= 'F')) {
                    if (base != 16 || has_exp) { return unknown; }
                    can_be_sep = true;
                } else if (std::isdigit(ch)) {
                    can_be_sep = true;
                } else {
                    number_end = get_ch_pos();
                }
                last_exp_start = exp_start;
                exp_start = false;
                if (number_end == static_cast<size_t>(-1)) { skip_next_ch(); }
            }
            auto number_str = substr(start, number_end);
            if (base == 8) {
                for (auto ch : number_str) {
                    if (ch == '8' || ch == '9') { return unknown; }
                }
            }
            auto remaining = substr(number_end);
            static const char *int_valid_suffices[]{
                "ull", "uLL", "ul", "uL", "u",
                "Ull", "ULL", "Ul", "UL", "U",
                "llu", "llU", "ll", "lu", "lU", "l",
                "LLu", "LLU", "LL", "Lu", "LU", "L",
                nullptr,
            };
            static const char *float_valid_suffices[]{
                "f", "l", "F", "L",
                nullptr,
            };
            auto valid_suffices = (has_exp || has_dot) ? float_valid_suffices : int_valid_suffices;
            size_t match_len = 0;
            for (auto p_suffix = valid_suffices; *p_suffix; p_suffix++) {
                if (remaining.starts_with(*p_suffix)) {
                    match_len = strlen(*p_suffix);
                    break;
                }
            }
            skip_chars(match_len);
            return {TokenType::eNumber, number_str};
        } else if (first_ch == '(' || first_ch == '[' || first_ch == '{') {
            return {TokenType::eLeftBracket, substr(start, get_ch_pos())};
        } else if (first_ch == ')' || first_ch == ']' || first_ch == '}') {
            return {TokenType::eLeftBracket, substr(start, get_ch_pos())};
        } else if (is_operator(first_ch)) {
            while (true) {
                auto ch = look_next_ch();
                if (!is_operator(ch)) { break; }
                skip_next_ch();
            }
            return {TokenType::eOperator, substr(start, get_ch_pos())};
        } else if (is_seperator(first_ch)) {
            return {TokenType::eSeperator, substr(start, get_ch_pos())};
        } else if (first_ch == '?') {
            return {TokenType::eQuestion, substr(start, get_ch_pos())};
        } else if (first_ch == ':') {
            return {TokenType::eColon, substr(start, get_ch_pos())};
        }
        return unknown;
    }

    size_t get_ch_pos() { return curr_lp; }
    char get_next_ch() {
        auto ch = look_next_ch();
        skip_next_ch();
        return ch;
    }
    char look_next_ch() {
        if (curr_lp == curr_line.size()) {
            return EOF;
        }
        return curr_line[curr_lp];
    }
    void skip_next_ch() {
        if (curr_lp != curr_line.size()) { ++curr_lp; }
    }
    void skip_chars(size_t num) {
        curr_lp = std::min(curr_lp + num, curr_line.size());
    }

    std::string_view substr(size_t start, size_t end = static_cast<size_t>(-1)) {
        return curr_line.substr(start, end - start);
    }
    void get_next_line() {
        curr_line = {};
        curr_lp = 0;
        if (curr_fp == curr_file.content.size()) {
            return;
        }
        const auto start = curr_fp;
        while (true) {
            while (curr_fp < curr_file.content.size() && curr_file.content[curr_fp] != '\n') { ++curr_fp; }
            if (curr_fp == curr_file.content.size()) { break; }
            if (curr_fp > start) {
                auto last = curr_file.content[curr_fp - 1];
                if (last == '\r' && curr_fp - 1 > start) { last = curr_file.content[curr_fp - 2]; }
                if (last != '\\') { break; }
            } else {
                break;
            }
            ++curr_fp;
        }
        curr_line = curr_file.content.substr(start, curr_fp - start);
    }

    std::unordered_map<std::string_view, Define> defines;
    FileInfo curr_file;
    size_t curr_fp = 0;
    std::string_view curr_line;
    size_t curr_lp = 0;
    size_t curr_lineno = 0;
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
