#pragma once
#include "ladder/block.hpp"
#include "ladder/boxes.hpp"
#include "ladder/translate.hpp"
#include <cstddef>
#include <optional>
#include <string>

// Live errors for the whole project, from soft_plc's own compiler.
//
// The KOP blocks are translated to ST (translate.hpp) and put together with
// the ST blocks, the way soft_plc will run the project, and the result is
// compiled. An error is traced back to where it came from: a line of an ST
// block, or a network (or the interface) of a KOP block.
namespace st_support {

struct StDiagnostic {
    std::size_t block;  // index in the project's blocks
    std::size_t line;   // from 0, in that block's text (a KOP block: its translation)
    std::size_t column; // from 0, in bytes
    std::string message;
    std::optional<std::size_t> network; // a KOP block: the network it's in; nothing for the interface

    bool operator==(const StDiagnostic&) const = default;
};

// The box types the project's rungs may use: the standard ones and the
// project's own function blocks (KOP blocks from their interface, ST
// blocks from their text, if it parses).
ladder::BoxCatalog projectBoxes(const ladder::Project& project);

struct CodeCheck {
    ladder::BoxCatalog boxes;
    ladder::TranslatedProject translation; // what was compiled
    // The first problem: either an ST block whose text doesn't declare
    // what the project says it is, or the first error soft_plc finds.
    std::optional<StDiagnostic> error;
};

CodeCheck checkCode(const ladder::Project& project);

} // namespace st_support
