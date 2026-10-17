#include "ladder/boxes.hpp"
#include <algorithm>
#include <cctype>

namespace ladder {

namespace {

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

} // namespace

const std::vector<BoxType>& standardBoxTypes() {
    // The same blocks, with the same pins, as soft_plc's standard library.
    static const std::vector<BoxType> types = {
        {"TON", "IN", "Q", {"PT"}, {"ET"}},
        {"TOF", "IN", "Q", {"PT"}, {"ET"}},
        {"TP", "IN", "Q", {"PT"}, {"ET"}},
        {"CTU", "CU", "Q", {"R", "PV"}, {"CV"}},
        {"CTD", "CD", "Q", {"LD", "PV"}, {"CV"}},
        {"R_TRIG", "CLK", "Q", {}, {}},
        {"F_TRIG", "CLK", "Q", {}, {}},
        {"SR", "S1", "Q1", {"R"}, {}},
        {"RS", "S", "Q1", {"R1"}, {}},
    };
    return types;
}

const BoxType* findBoxType(std::string_view name) {
    const auto& types = standardBoxTypes();
    const auto it = std::find_if(types.begin(), types.end(),
                                 [&](const BoxType& type) { return equalsIgnoringCase(type.name, name); });
    return it == types.end() ? nullptr : &*it;
}

} // namespace ladder
