#pragma once
#include "ladder/boxes.hpp"
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Where power flows in a rung, from the values of its operands in the PLC:
// what online mode draws in green.
//
// The rung is evaluated the way the translation runs it (translate.hpp):
// a contact passes power when its operand is TRUE (FALSE for normally
// closed), parallel sides are ORed, and a box gives out what its power
// output says (t1.Q), whatever came in: a TOF's Q stays TRUE after its
// power is gone. The values are read after a cycle, so a rung that writes
// what it reads (start/stop holding itself) shows the state at the end of
// that cycle.
namespace ladder {

struct ItemPower {
    bool in = false;                // power reaches its left side
    bool out = false;               // power leaves its right side
    std::optional<bool> conducting; // a contact: would it pass power (its operand's state); unknown if not read
};

struct RungPower {
    std::vector<std::pair<ItemPath, ItemPower>> items;

    const ItemPower* find(const ItemPath& path) const;
};

// The value of a BOOL operand (a variable, t1.Q, TRUE), if it's known.
using BoolLookup = std::function<std::optional<bool>(const std::string& operand)>;

RungPower evaluatePower(const Rung& rung, const BoolLookup& lookup, const BoxCatalog& boxes = standardBoxes());

} // namespace ladder
