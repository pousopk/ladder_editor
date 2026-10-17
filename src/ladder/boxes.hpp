#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The function blocks that can be placed in a rung as boxes, and how power
// goes through each one.
//
//            ┌── TON ──┐
//   power ──►│IN      Q│──► power
//    T#5s ──►│PT     ET│──► elapsed
//            └─────────┘
//
// The standard blocks are built in. The project's own function blocks, in
// KOP or ST, can be boxes too: their first BOOL input carries the power in,
// and their first BOOL output carries it out.
namespace ladder {

struct BoxType {
    std::string name;
    std::string powerInput;           // the BOOL input the rung's power goes into
    std::string powerOutput;          // the BOOL output the power comes out of; empty: it passes through
    std::vector<std::string> inputs;  // the other inputs, connected to operands
    std::vector<std::string> outputs; // the other outputs, connected to variables
};

// The box types an editor or a translation knows about.
class BoxCatalog {
public:
    BoxCatalog(); // the standard blocks

    // Adds a project block, unless a box of that name is already known.
    void add(BoxType type);

    const BoxType* find(std::string_view name) const; // any case; null if unknown
    const std::vector<BoxType>& all() const { return types_; }

private:
    std::vector<BoxType> types_;
};

// The catalog of the standard blocks, shared.
const BoxCatalog& standardBoxes();

// Shortcuts for the standard blocks.
const BoxType* findBoxType(std::string_view name);
const std::vector<BoxType>& standardBoxTypes();

// One input or output in a function block's interface.
struct PinDeclaration {
    std::string name;
    std::string type; // BOOL, INT, ...
    bool input = true;
};

// A project function block as a box: the first BOOL input and the first
// BOOL output carry the power, the rest are pins. Without a BOOL output the
// power passes through the box unchanged (like ENO in TIA Portal); without
// a BOOL input it can't be a box: nothing for the power to go into.
std::optional<BoxType> boxTypeFromInterface(std::string name, const std::vector<PinDeclaration>& pins);

} // namespace ladder
