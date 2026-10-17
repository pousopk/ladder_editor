#pragma once
#include "online/session.hpp"
#include "view/code_editor.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace app {

// The colours of an ST text's parts, for a code editor.
std::vector<view::ColoredSpan> stColors(std::string_view source);

// Online: a note for each line of an ST text with the values of its
// variables, "count = 3   t1.Q = TRUE", as seen from `instance` (empty: the
// PROGRAM). Every name is added to `watched`, so it's read for the next
// frame; names soft_plc doesn't know (keywords, types, pins) show nothing.
std::vector<std::string> onlineNotes(std::string_view source, const online::Session& session,
                                     const std::string& instance, std::vector<std::string>& watched);

} // namespace app
