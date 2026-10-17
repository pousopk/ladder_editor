#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Colours for Structured Text, from soft_plc's own lexer: what the editor
// shows as a keyword is exactly what soft_plc reads as one.
namespace st_support {

enum class Style {
    Keyword,  // IF, END_IF, VAR, FUNCTION_BLOCK, AND, MOD, ...
    Type,     // BOOL, INT, DINT, REAL, TIME, ARRAY, STRUCT
    Builtin,  // standard blocks and functions: TON, CTU, MAX, LIMIT, REAL_TO_INT, ...
    Literal,  // TRUE, FALSE
    Number,   // 42, 16#FF, 3.14
    Time,     // T#500ms
    Address,  // %IX0.0, %DB1.DBW2
    Operator, // := + ( ) ; ...
    Comment,  // (* ... *) and // ...
};

// A coloured stretch of one line. Lines and columns count from 0; columns
// are bytes, as soft_plc counts them.
struct Span {
    std::size_t line;
    std::size_t column;
    std::size_t length;
    Style style;

    bool operator==(const Span&) const = default;
};

// The spans of `source`, in order. Plain names and spaces have no span.
// Text that soft_plc's lexer can't read (an unknown character, a comment
// that isn't closed yet) doesn't stop the colours before it: an unclosed
// comment colours everything after it as a comment, as it will be once closed.
std::vector<Span> highlight(std::string_view source);

} // namespace st_support

namespace st_support {

// The names on each line that may be variables: `count`, `t1.Q`,
// `p1.timer.ET` (names joined by dots). For online mode, which shows their
// values at the end of the line; the PLC says which ones really are
// variables. Lines count from 0. Nothing if the text can't be read.
std::vector<std::vector<std::string>> namesByLine(std::string_view source);

} // namespace st_support
