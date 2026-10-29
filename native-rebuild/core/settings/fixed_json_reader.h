#pragma once
#include <charconv>
#include <string>
#include <string_view>

namespace lume::settings {
// Strict reader for fixed private JSON schemas; strings intentionally reject escapes.
class Reader {
public:
    explicit Reader(std::string_view input) : input_(input) {}
    bool Take(char expected) {
        Space();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }
    bool String(std::string& result) {
        if (!Take('"')) return false;
        const auto start = position_;
        while (position_ < input_.size() && input_[position_] != '"') {
            const auto ch = static_cast<unsigned char>(input_[position_]);
            if (ch < 32 || ch == '\\') return false;
            ++position_;
        }
        if (position_ >= input_.size()) return false;
        result.assign(input_.substr(start, position_ - start));
        ++position_;
        return true;
    }
    bool Integer(int& value) {
        Space();
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        const auto digits = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        if (position_ == digits || (position_ > digits + 1 && input_[digits] == '0')) return false;
        const auto conversion = std::from_chars(input_.data() + start, input_.data() + position_, value);
        return conversion.ec == std::errc{} && conversion.ptr == input_.data() + position_;
    }
    bool Boolean(bool& result) {
        Space();
        if (input_.substr(position_, 4) == "true") { position_ += 4; result = true; return true; }
        if (input_.substr(position_, 5) == "false") { position_ += 5; result = false; return true; }
        return false;
    }
    bool End() { Space(); return position_ == input_.size(); }
private:
    void Space() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
    }
    std::string_view input_;
    std::size_t position_ = 0;
};
}  // namespace lume::settings
