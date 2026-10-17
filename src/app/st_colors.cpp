#include "app/st_colors.hpp"
#include "st_support/highlight.hpp"

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

} // namespace app
