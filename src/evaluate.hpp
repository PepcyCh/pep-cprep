#pragma once

#include "utils.hpp"

namespace pep::cprep {

struct EvaluateError final {
    std::string msg;
};

bool evaluate_expression(InputState &input);

}
