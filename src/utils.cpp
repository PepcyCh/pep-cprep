#include "utils.hpp"

namespace pep::cprep {

char InputState::look_next_ch() const {
    return is_end() ? EOF : *p_curr_;
}

char InputState::look_next_ch(size_t offset) const {
    offset = std::min<size_t>(offset, p_end_ - p_curr_);
    auto p = p_curr_ + offset;
    return p == p_end_ ? EOF : *p;
}

char InputState::get_next_ch() {
    auto ch = look_next_ch();
    skip_next_ch();
    return ch;
}

void InputState::skip_next_ch() {
    if (!is_end()) {
        ++p_curr_;
        ++col_;
    }
}

void InputState::skip_chars(size_t count) {
    auto real_count = std::min<size_t>(count, p_end_ - p_curr_);
    p_curr_ += real_count;
    col_ += real_count;
}

void InputState::skip_to_end() {
    p_curr_ = p_end_;
}

void InputState::unget_chars(size_t count) {
    p_curr_ -= count;
}

std::string_view InputState::get_substr(
    std::string_view::const_iterator p_start, std::string_view::const_iterator p_end
) const {
    return make_string_view(p_start, p_end);
}

std::string_view InputState::get_substr(std::string_view::const_iterator p_start, size_t count) const {
    auto real_count = std::min<size_t>(count, p_end_ - p_start);
    return make_string_view(p_start, p_start + real_count);
}

std::string_view InputState::get_substr_to_end(std::string_view::const_iterator p_start) const {
    return make_string_view(p_start, p_end_);
}

std::string_view InputState::get_substr_to_curr(std::string_view::const_iterator p_start) const {
    return make_string_view(p_start, p_curr_);
}

}
