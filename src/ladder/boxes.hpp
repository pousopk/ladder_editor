#pragma once
#include <string_view>
#include <vector>

// The function blocks that can be placed in a rung as boxes, and how power
// goes through each one. The standard blocks are built in; boxes for the
// project's own function blocks come once the editor can read the
// interfaces of ST blocks too, with the translation to ST (milestone 7).
//
//            ┌── TON ──┐
//   power ──►│IN      Q│──► power
//    T#5s ──►│PT     ET│──► elapsed
//            └─────────┘
namespace ladder {

struct BoxType {
    std::string_view name;
    std::string_view powerInput;           // the BOOL input the rung's power goes into
    std::string_view powerOutput;          // the BOOL output the power comes out of
    std::vector<std::string_view> inputs;  // the other inputs, connected to operands
    std::vector<std::string_view> outputs; // the other outputs, connected to variables
};

// The standard block with this name (any case), or null.
const BoxType* findBoxType(std::string_view name);

// All the standard blocks, for a palette in the editor.
const std::vector<BoxType>& standardBoxTypes();

} // namespace ladder
