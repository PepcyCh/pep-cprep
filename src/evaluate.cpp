#include "evaluate.hpp"

#include <vector>
#include <format>
#include <variant>
#include <cmath>
#include <cctype>

#include "tokenize.hpp"

namespace pep::cprep {

namespace {

[[noreturn]] void unreachable() { std::abort(); }

int char_to_number(char ch) {
    if ('a' <= ch && ch <= 'f') {
        return ch - 'a' + 10;
    } else if ('A' <= ch && ch <= 'F') {
        return ch - 'A' + 10;
    } else {
        return ch - '0';
    }
}

int64_t str_to_number(std::string_view str) {
    int64_t base = 10;
    auto is_integer = str.find('.') != std::string_view::npos
        || (!str.starts_with("0x") && str.find_first_of("eE") != std::string_view::npos)
        || (str.starts_with("0x") && str.find_first_of("pP") != std::string_view::npos);
    if (!is_integer) {
        throw EvaluateError{"floating point literal in preprocessor expression."};
    }
    size_t i = 0;
    if (str[0] == '0') {
        if (str.size() == 1) { return 0; }
        if (str[1] == 'x') {
            base = 16;
            i = 2;
        } else if (str[1] == 'b') {
            base = 2;
            i = 2;
        } else if (std::isdigit(str[1])) {
            base = 8;
            i = 2;
        } else if (str[1] == '\'' && std::isdigit(str[2])) {
            base = 8;
            i = 3;
        } else {
            i = 1;
        }
    }
    int64_t value = 0.0;
    for (; i < str.size(); i++) {
        if (str[i] == '\'') { continue; }
        value = value * base + char_to_number(str[i]);
    }
    return value;
}

struct Operator final {
    TokenType op;
    // 0 - ,
    // 1 - ?:
    // 2 - ||
    // 3 - &&
    // 4 - |
    // 5 - ^
    // 6 - &
    // 7 - == !=
    // 8 - < <= > >=
    // 9 - << >>
    // 10 - + - (binary)
    // 11 - * / %
    // 12 - + - ! ~ (unary)
    uint32_t priority;

    bool is_unary() const { return priority == 12; }

    static Operator from_token(const Token &token, bool prev_is_number) {
        switch (token.type) {
            case TokenType::eAdd:
            case TokenType::eSub:
                return {token.type, prev_is_number ? 10u : 12u};
            case TokenType::eBNot:
            case TokenType::eLNot:
                return {token.type, 12u};
            case TokenType::eMul:
            case TokenType::eDiv:
            case TokenType::eMod:
                return {token.type, 11u};
            case TokenType::eBShl:
            case TokenType::eBShr:
                return {token.type, 9u};
            case TokenType::eLess:
            case TokenType::eLessEq:
            case TokenType::eGreater:
            case TokenType::eGreaterEq:
                return {token.type, 8u};
            case TokenType::eEq:
            case TokenType::eNotEq:
                return {token.type, 7u};
            case TokenType::eBAnd:
                return {token.type, 6u};
            case TokenType::eBXor:
                return {token.type, 5u};
            case TokenType::eBOr:
                return {token.type, 4u};
            case TokenType::eLAnd:
                return {token.type, 3u};
            case TokenType::eLOr:
                return {token.type, 2u};
            case TokenType::eQuestion:
            case TokenType::eColon:
                return {token.type, 1u};
            case TokenType::eComma:
                return {token.type, 0u};
            default:
                throw EvaluateError{std::format("operator '{}' not allowed here", token.value)};
        }
    }
};

int64_t do_binary_op(TokenType op, int64_t a, int64_t b) {
    // TODO
}

}

bool evaluate_expression(InputState &input) {
    std::vector<int64_t> values;
    std::vector<Operator> ops;
    bool prev_is_number = false;
    std::string temp{};
    while (true) {
        auto token = get_next_token(input, temp, false);
        if (token.type == TokenType::eEof) {
            break;
        }
        if (token.type == TokenType::eNumber) {
            auto value = str_to_number(token.value);
            if (!ops.empty() && ops.back().is_unary()) {
                switch (ops.back().op) {
                    case TokenType::eAdd: break;
                    case TokenType::eSub: value = -value; break;
                    case TokenType::eBNot: value = ~value; break;
                    case TokenType::eLNot: value = value != 0 ? 1 : 0; break;
                    default: unreachable();
                }
                ops.pop_back();
            }
            values.push_back(value);
            prev_is_number = true;
        } else {
            // TODO - brackets
            auto op = Operator::from_token(token, prev_is_number);
            size_t start = ops.size();
            while (start > 0 && ops[start - 1].priority > op.priority) {
                --start;
            }
            int64_t value = values[start];
            for (size_t i = start; i < ops.size(); i++) {
                if (ops[i].op != TokenType::eQuestion && ops[i].op != TokenType::eColon) {
                    value = do_binary_op(ops[i].op, value, values[i + 1]);
                } else {
                    // TODO - ternary
                }
            }
            values.resize(start + 1);
            values[start] = value;
            ops.resize(start);
        }
    }
    int64_t value = values[0];
    for (size_t i = 0; i < ops.size(); i++) {
        if (ops[i].op != TokenType::eQuestion && ops[i].op != TokenType::eColon) {
            value = do_binary_op(ops[i].op, value, values[i + 1]);
        } else {
            // TODO - ternary
        }
    }
    return value;
}

}
