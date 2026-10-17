#include "ladder/rung_file.hpp"
#include "ladder/notation.hpp"
#include <fmt/format.h>

namespace ladder {

namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

} // namespace

tl::expected<std::vector<Rung>, std::string> parseRungFile(std::string_view text) {
    std::vector<Rung> rungs;
    std::string comment;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const auto line = trim(text.substr(0, newline));
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;

        if (line.empty()) {
            comment.clear(); // a comment must be right above its rung
            continue;
        }
        if (line.substr(0, 2) == "//") {
            if (!comment.empty()) {
                comment += '\n';
            }
            comment += trim(line.substr(2));
            continue;
        }
        auto rung = parseRung(line);
        if (!rung) {
            return tl::unexpected(fmt::format("line {}, {}", lineNumber, rung.error()));
        }
        rung->comment = std::move(comment);
        comment.clear();
        rungs.push_back(std::move(*rung));
    }
    return rungs;
}

std::string formatRungFile(const std::vector<Rung>& rungs) {
    std::string text;
    for (std::size_t i = 0; i < rungs.size(); ++i) {
        if (i > 0) {
            text += '\n';
        }
        std::string_view comment = rungs[i].comment;
        while (!comment.empty()) {
            const auto newline = comment.find('\n');
            text += "// " + std::string(comment.substr(0, newline)) + "\n";
            comment.remove_prefix(newline == std::string_view::npos ? comment.size() : newline + 1);
        }
        text += formatRung(rungs[i]) + "\n";
    }
    return text;
}

} // namespace ladder
