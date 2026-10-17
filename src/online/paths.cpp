#include "online/paths.hpp"
#include "ladder/translate.hpp"
#include <algorithm>
#include <cctype>

namespace online {

namespace {

std::string upper(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

} // namespace

std::optional<bool> boolLiteral(const std::string& operand) {
    const auto text = upper(operand);
    if (text == "TRUE") {
        return true;
    }
    if (text == "FALSE") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> monitorPath(const std::string& operand, const std::string& instance) {
    if (operand.empty() || boolLiteral(operand) || operand.find('#') != std::string::npos) {
        return std::nullopt;
    }
    const auto first = static_cast<unsigned char>(operand.front());
    if (operand.front() == '%') {
        return instance.empty() ? std::optional(ladder::addressVariable(operand)) : std::nullopt;
    }
    if (std::isalpha(first) == 0 && operand.front() != '_') {
        return std::nullopt; // a number
    }
    return instance.empty() ? operand : instance + "." + operand;
}

} // namespace online
