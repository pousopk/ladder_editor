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

BoxCatalog::BoxCatalog()
    // The same blocks, with the same pins, as soft_plc's standard library.
    : types_{
          {"TON", "IN", "Q", {"PT"}, {"ET"}},
          {"TOF", "IN", "Q", {"PT"}, {"ET"}},
          {"TP", "IN", "Q", {"PT"}, {"ET"}},
          {"CTU", "CU", "Q", {"R", "PV"}, {"CV"}},
          {"CTD", "CD", "Q", {"LD", "PV"}, {"CV"}},
          {"R_TRIG", "CLK", "Q", {}, {}},
          {"F_TRIG", "CLK", "Q", {}, {}},
          {"SR", "S1", "Q1", {"R"}, {}},
          {"RS", "S", "Q1", {"R1"}, {}},
      } {}

void BoxCatalog::add(BoxType type) {
    if (find(type.name) == nullptr) {
        types_.push_back(std::move(type));
    }
}

const BoxType* BoxCatalog::find(std::string_view name) const {
    const auto it = std::find_if(types_.begin(), types_.end(),
                                 [&](const BoxType& type) { return equalsIgnoringCase(type.name, name); });
    return it == types_.end() ? nullptr : &*it;
}

const BoxCatalog& standardBoxes() {
    static const BoxCatalog catalog;
    return catalog;
}

const BoxType* findBoxType(std::string_view name) {
    return standardBoxes().find(name);
}

const std::vector<BoxType>& standardBoxTypes() {
    return standardBoxes().all();
}

std::optional<BoxType> boxTypeFromInterface(std::string name, const std::vector<PinDeclaration>& pins) {
    BoxType type;
    type.name = std::move(name);
    for (const auto& pin : pins) {
        const bool isBool = equalsIgnoringCase(pin.type, "BOOL");
        auto& power = pin.input ? type.powerInput : type.powerOutput;
        if (isBool && power.empty()) {
            power = pin.name;
        } else {
            (pin.input ? type.inputs : type.outputs).push_back(pin.name);
        }
    }
    if (type.powerInput.empty()) {
        return std::nullopt;
    }
    return type;
}

} // namespace ladder
