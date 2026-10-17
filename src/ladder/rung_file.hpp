#pragma once
#include "ladder/rung.hpp"
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

// A simple text file of rungs, one per line in the notation (notation.hpp):
//
//   // Motor starter: start holds itself, stop always wins
//   (start | motor) stop => motor
//
//   // Lamp on 2 s after the motor starts
//   motor {TON t1, PT := T#2s} => lamp
//
// Lines starting with // right above a rung are its comment (several lines
// make a comment of several lines). An empty line ends a comment, so a
// comment with an empty line after it, like a header at the top of the
// file, belongs to no rung and is ignored. Milestone 5's
// project files will replace this, but it stays handy for examples and tests.
namespace ladder {

// Every rung in the text, or the first problem with its line and column.
tl::expected<std::vector<Rung>, std::string> parseRungFile(std::string_view text);

std::string formatRungFile(const std::vector<Rung>& rungs);

} // namespace ladder
