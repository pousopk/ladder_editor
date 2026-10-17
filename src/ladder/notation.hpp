#pragma once
#include "ladder/rung.hpp"
#include <string>
#include <string_view>
#include <tl/expected.hpp>

// A one-line text form of a rung, for tests and for copying rungs as text:
//
//   start (motor | /stop) {TON t1, PT := T#5s, ET => elapsed} => lamp, S:alarm
//
//   start           a normally open contact
//   /stop           a normally closed contact
//   (a b | c)       a parallel branch: "a then b", or "c"
//   {TON t1, ...}   a box: type, instance, then inputs (PIN := value) and outputs (PIN => variable)
//   => lamp, ...    the coils: lamp (normal), /lamp (negated), S:lamp (set), R:lamp (reset)
//
// Elements in a series are separated by spaces. A rung with no logic is
// written "=> lamp". formatRung(parseRung(text)) gives back the text in
// this canonical spacing, and parseRung(formatRung(rung)) gives back the rung.
namespace ladder {

std::string formatRung(const Rung& rung);
std::string formatElement(const Element& element); // one element: start, /stop, (a | b), {TON t1, ...}

// Reads a rung, or describes the first problem and where it is.
tl::expected<Rung, std::string> parseRung(std::string_view text);

} // namespace ladder
