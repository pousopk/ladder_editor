#include "app/st_display.hpp"
#include "online/paths.hpp"
#include "st_support/highlight.hpp"
#include <algorithm>

namespace app {

namespace {

// The colours of ST's parts.
ImU32 colorOf(st_support::Style style) {
    switch (style) {
    case st_support::Style::Keyword:
        return IM_COL32(110, 160, 240, 255);
    case st_support::Style::Type:
        return IM_COL32(90, 200, 190, 255);
    case st_support::Style::Builtin:
        return IM_COL32(220, 200, 120, 255);
    case st_support::Style::Literal:
        return IM_COL32(200, 140, 230, 255);
    case st_support::Style::Number:
        return IM_COL32(180, 220, 150, 255);
    case st_support::Style::Time:
        return IM_COL32(180, 220, 150, 255);
    case st_support::Style::Address:
        return IM_COL32(240, 160, 110, 255);
    case st_support::Style::Operator:
        return IM_COL32(170, 175, 185, 255);
    case st_support::Style::Comment:
        return IM_COL32(110, 150, 110, 255);
    }
    return IM_COL32_WHITE;
}

} // namespace

std::vector<view::ColoredSpan> stColors(std::string_view source) {
    std::vector<view::ColoredSpan> colors;
    for (const auto& span : st_support::highlight(source)) {
        colors.push_back(view::ColoredSpan{span.line, span.column, span.length, colorOf(span.style)});
    }
    return colors;
}

std::vector<std::string> onlineNotes(std::string_view source, const online::Session& session,
                                     const std::string& instance, std::vector<std::string>& watched) {
    const auto lines = st_support::namesByLine(source);
    std::vector<std::string> notes(lines.size());
    for (std::size_t line = 0; line < lines.size(); ++line) {
        for (const auto& name : lines[line]) {
            const auto path = online::monitorPath(name, instance);
            if (!path) {
                continue;
            }
            if (std::find(watched.begin(), watched.end(), *path) == watched.end()) {
                watched.push_back(*path);
            }
            if (const auto value = session.value(*path)) {
                notes[line] += (notes[line].empty() ? "" : "   ") + name + " = " + *value;
            }
        }
    }
    return notes;
}

} // namespace app
