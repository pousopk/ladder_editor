#pragma once
#include "ladder/rung.hpp"
#include <string>
#include <vector>

// The rules a rung must follow that its structure doesn't already guarantee.
//
// The tree of series and parallel branches can't describe a broken
// connection, so these checks are about content: every coil and contact has
// an operand, every parallel branch really has two sides, every box is a
// known block with real pins. Whether the operands exist and have the right
// types is soft_plc's analyzer's job, once the block is translated to ST.
namespace ladder {

struct Problem {
    std::string message;

    bool operator==(const Problem&) const = default;
};

// Every problem in the rung, in order from left to right; empty if it's fine.
std::vector<Problem> check(const Rung& rung);

} // namespace ladder
