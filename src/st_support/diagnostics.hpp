#pragma once
#include "ladder/block.hpp"
#include <cstddef>
#include <optional>
#include <string>

// Live errors for the ST blocks, from soft_plc's own compiler.
//
// The project's ST blocks are compiled together, the way soft_plc will
// compile the whole project, so a block that uses a function block from
// another ST block is checked against it. Until the KOP blocks are
// translated (milestone 7), they aren't part of this: if the PROGRAM is a
// KOP block, a stand-in empty PROGRAM takes its place, and ST code that uses
// a KOP block's function block reports it as an unknown type.
namespace st_support {

struct StDiagnostic {
    std::size_t block;  // index in the project's blocks
    std::size_t line;   // from 0, in that block's text
    std::size_t column; // from 0, in bytes
    std::string message;

    bool operator==(const StDiagnostic&) const = default;
};

// The first problem in the project's ST blocks, if there is one: either a
// block whose text doesn't declare what the project says it is, or the
// first error soft_plc's compiler finds.
std::optional<StDiagnostic> checkStBlocks(const ladder::Project& project);

} // namespace st_support
