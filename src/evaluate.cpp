#include "evaluate.hpp"

#include <vector>
#include <format>
#include <stack>
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
    auto is_floating = str.find('.') != std::string_view::npos
        || (!str.starts_with("0x") && str.find_first_of("eE") != std::string_view::npos)
        || (str.starts_with("0x") && str.find_first_of("pP") != std::string_view::npos);
    if (is_floating) {
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
    // 13 - ( )
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
            case TokenType::eLeftBracketRound:
            case TokenType::eRightBracketRound:
            case TokenType::eEof: // treat eof as )
                return {token.type, 13u};
            default:
                throw EvaluateError{std::format("operator '{}' not allowed here", token.value)};
        }
    }
};

int64_t do_binary_op(TokenType op, int64_t a, int64_t b) {
    switch (op) {
        case TokenType::eAdd: return a + b;
        case TokenType::eSub: return a - b;
        case TokenType::eMul: return a * b;
        case TokenType::eDiv: return a / b;
        case TokenType::eMod: return a % b;
        case TokenType::eBShl: return a << b;
        case TokenType::eBShr: return a >> b;
        case TokenType::eLess: return a < b;
        case TokenType::eLessEq: return a <= b;
        case TokenType::eGreater: return a > b;
        case TokenType::eGreaterEq: return a >= b;
        case TokenType::eEq: return a == b;
        case TokenType::eNotEq: return a != b;
        case TokenType::eBAnd: return a & b;
        case TokenType::eBXor: return a ^ b;
        case TokenType::eBOr: return a | b;
        case TokenType::eLAnd: return a && b;
        case TokenType::eLOr: return a || b;
        default: unreachable();
    }
}

}

bool evaluate_expression(InputState &input) {
    std::vector<int64_t> values;
    std::vector<Operator> ops;
    std::stack<size_t> left_brackets;
    left_brackets.push(0);
    bool prev_is_number = false;
    size_t num_questions = 0;
    std::stack<size_t> num_questions_left_bracket;
    num_questions_left_bracket.push(0);
    std::string temp{};
    while (true) {
        auto token = get_next_token(input, temp, false);
        if (token.type == TokenType::eNumber) {
            if (prev_is_number) {
                throw EvaluateError{std::format("expected an operator after a number")};
            }
            auto value = str_to_number(token.value);
            while (!ops.empty() && ops.back().is_unary()) {
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
            auto op = Operator::from_token(token, prev_is_number);
            if (op.op == TokenType::eLeftBracketRound) {
                if (prev_is_number) {
                    throw EvaluateError{std::format("expected an operator before '('")};
                }
                left_brackets.push(values.size());
                num_questions_left_bracket.push(num_questions);
                prev_is_number = false;
            } else if (op.op == TokenType::eComma) {
                // clear stack since expressions before comma will not have any (side) effect
                if (!prev_is_number) {
                    throw EvaluateError{std::format("expected a number after an operator other than ')'")};
                }
                if (num_questions > num_questions_left_bracket.top()) {
                    throw EvaluateError{std::format("'?' without a ':'")};
                }
                values.resize(left_brackets.top());
                ops.resize(left_brackets.top());
                prev_is_number = false;
            } else {
                if (!prev_is_number) {
                    throw EvaluateError{std::format("expected a number after an operator other than ')'")};
                }
                if (op.op == TokenType::eQuestion) {
                    ++num_questions;
                } else if (op.op == TokenType::eColon) {
                    if (num_questions == num_questions_left_bracket.top()) {
                        throw EvaluateError{std::format("':' before a '?'")};
                    }
                    --num_questions;
                }
                size_t start = ops.size();
                // leave ternary op at last
                while (
                    start > left_brackets.top() && ops[start - 1].priority < op.priority && ops[start - 1].priority > 1
                ) {
                    --start;
                }
                int64_t value = values[start];
                for (size_t i = start; i < ops.size(); i++) {
                    value = do_binary_op(ops[i].op, value, values[i + 1]);
                }
                values.resize(start + 1);
                values[start] = value;
                if (op.op == TokenType::eRightBracketRound || op.op == TokenType::eEof) {
                    if (num_questions > num_questions_left_bracket.top()) {
                        throw EvaluateError{std::format("'?' without a ':'")};
                    }
                    // calc ternary backward
                    std::stack<int64_t> ternary_values;
                    ternary_values.push(values.back());
                    for (size_t i = start; i > left_brackets.top(); i--) {
                        if (ops[i - 1].op == TokenType::eColon) {
                            ternary_values.push(values[i - 1]);
                        } else {
                            auto t = ternary_values.top();
                            ternary_values.pop();
                            auto f = ternary_values.top();
                            ternary_values.pop();
                            ternary_values.push(values[i - 1] ? t : f);
                        }
                    }
                    ops.resize(start);
                    left_brackets.pop();
                    num_questions_left_bracket.pop();
                    if (op.op == TokenType::eEof) { break; }
                    // set to TRUE, '+' and '-' after ')' is add/sub instead of pos/neg
                    prev_is_number = true;
                } else {
                    ops.resize(start + 1);
                    ops[start] = op;
                    prev_is_number = false;
                }
            }
        }
    }
    if (num_questions > 0) {
        throw EvaluateError{std::format("'?' without a ':'")};
    }
    return values[0];
}

}
