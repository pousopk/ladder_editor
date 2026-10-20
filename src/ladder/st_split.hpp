#pragma once
#include "ladder/block.hpp"
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

// Splits an ST program, as soft_plc runs it, into the editor's blocks: one
// per PROGRAM, FUNCTION_BLOCK, FUNCTION and TYPE, in order. This is what
// Upload does with the program it gets from a running soft_plc.
//
//   (* counts boxes *)               block "Counter": the comment, and the
//   FUNCTION_BLOCK Counter           FUNCTION_BLOCK up to the end of its
//   ...                              END_FUNCTION_BLOCK line
//   END_FUNCTION_BLOCK
//
//   PROGRAM Main                     block "Main"
//   ...
//   END_PROGRAM
//
// Each block keeps its text exactly, comments included: the comments before
// a declaration belong to it, and the ones after the last declaration to
// the last block. Blank lines between blocks are dropped. A TYPE has no
// name in ST, so TYPE blocks are named Types, Types2, ...
namespace ladder {

// The blocks of an ST program, each holding its own text, all ST. Or what's
// wrong, and on which line.
tl::expected<std::vector<Block>, std::string> splitStSource(std::string_view source);

// An uploaded program as a project: its blocks, named after its PROGRAM
// ("uploaded" if it has none), with an empty watch table.
tl::expected<Project, std::string> projectFromSt(std::string_view source);

} // namespace ladder
