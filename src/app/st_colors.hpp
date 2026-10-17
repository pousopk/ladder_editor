#pragma once
#include "view/code_editor.hpp"
#include <string_view>
#include <vector>

namespace app {

// The colours of an ST text's parts, for a code editor.
std::vector<view::ColoredSpan> stColors(std::string_view source);

} // namespace app
