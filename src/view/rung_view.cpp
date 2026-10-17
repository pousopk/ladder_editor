#include "view/rung_view.hpp"
#include "ladder/boxes.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace view {

namespace {

using namespace ladder;

const ImU32 kWire = IM_COL32(200, 200, 205, 255);
const ImU32 kText = IM_COL32(230, 230, 235, 255);
const ImU32 kPinText = IM_COL32(160, 165, 175, 255);
const ImU32 kHover = IM_COL32(90, 140, 220, 60);
const ImU32 kSelected = IM_COL32(90, 140, 220, 120);
const ImU32 kSelectedBorder = IM_COL32(120, 170, 255, 255);
const ImU32 kMissing = IM_COL32(240, 160, 70, 255);
const ImU32 kPowered = IM_COL32(70, 210, 100, 255); // online: power flows here
const ImU32 kValue = IM_COL32(120, 215, 135, 255);  // online: a value read from the PLC

ImU32 wireColor(bool powered) {
    return powered ? kPowered : kWire;
}

// An operand as shown: "???" in orange while it's still empty.
void operandText(ImDrawList* draw, float centerX, float y, const std::string& operand);

// Converts grid positions to pixels for one rung.
class Grid {
public:
    Grid(ImVec2 origin, float zoom) : origin_(origin), zoom_(zoom) {}

    float x(std::size_t column) const { return origin_.x + (m_.rail + static_cast<float>(column) * m_.column) * zoom_; }
    // The power line of a row.
    float y(std::size_t row) const { return origin_.y + (static_cast<float>(row) * m_.row + m_.power) * zoom_; }
    float top(std::size_t row) const { return origin_.y + static_cast<float>(row) * m_.row * zoom_; }
    float scale(float pixels) const { return pixels * zoom_; }

    // The rectangle an item covers, for highlighting and for the mouse.
    ImVec4 bounds(const PlacedItem& item) const {
        return {x(item.column), top(item.row), x(item.column + item.width), top(item.row + item.height)};
    }

private:
    ImVec2 origin_;
    float zoom_;
    Metrics m_;
};

void centeredText(ImDrawList* draw, float centerX, float y, ImU32 color, const std::string& text) {
    const auto size = ImGui::CalcTextSize(text.c_str());
    draw->AddText({centerX - size.x / 2.0F, y}, color, text.c_str());
}

void operandText(ImDrawList* draw, float centerX, float y, const std::string& operand) {
    centeredText(draw, centerX, y, operand.empty() ? kMissing : kText, operand.empty() ? "???" : operand);
}

// A wire from the left edge of a cell to a symbol, and from the symbol to the right edge.
void stubs(ImDrawList* draw, float left, float right, float y, float halfSymbol, float thickness,
           const ItemPower* power) {
    const float center = (left + right) / 2.0F;
    draw->AddLine({left, y}, {center - halfSymbol, y}, wireColor(power && power->in), thickness);
    draw->AddLine({center + halfSymbol, y}, {right, y}, wireColor(power && power->out), thickness);
}

void drawContact(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Contact& contact,
                 const ItemPower* power) {
    const float left = g.x(item.column);
    const float right = g.x(item.column + 1);
    const float y = g.y(item.row);
    const float center = (left + right) / 2.0F;
    const float half = g.scale(8.0F);
    const float tall = g.scale(11.0F);
    const float thickness = g.scale(1.6F);
    stubs(draw, left, right, y, half, thickness, power);
    // Online, a closed contact (one that would pass power) is green, with or without power.
    const auto symbol = wireColor(power && power->conducting == true);
    if (power && power->conducting == true) {
        draw->AddRectFilled({center - half, y - tall}, {center + half, y + tall}, IM_COL32(70, 210, 100, 60));
    }
    draw->AddLine({center - half, y - tall}, {center - half, y + tall}, symbol, thickness);
    draw->AddLine({center + half, y - tall}, {center + half, y + tall}, symbol, thickness);
    if (contact.kind == ContactKind::NormallyClosed) {
        draw->AddLine({center - half - g.scale(3), y + tall}, {center + half + g.scale(3), y - tall}, symbol,
                      thickness);
    }
    operandText(draw, center, y - tall - g.scale(18.0F), contact.operand);
}

void drawCoil(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Coil& coil, const ItemPower* power) {
    const float left = g.x(item.column);
    const float right = g.x(item.column + 1);
    const float y = g.y(item.row);
    const float center = (left + right) / 2.0F;
    const float radius = g.scale(11.0F);
    const float thickness = g.scale(1.6F);
    stubs(draw, left, right, y, radius, thickness, power);
    // ( ): two arcs of a circle, opening towards each other.
    const auto symbol = wireColor(power && power->in);
    draw->PathArcTo({center, y}, radius, 2.4F, 3.9F, 12);
    draw->PathStroke(symbol, 0, thickness);
    draw->PathArcTo({center, y}, radius, -0.75F, 0.75F, 12);
    draw->PathStroke(symbol, 0, thickness);
    const char* mark = coil.kind == CoilKind::Set ? "S" : coil.kind == CoilKind::Reset ? "R" : nullptr;
    if (mark != nullptr) {
        centeredText(draw, center, y - ImGui::GetTextLineHeight() / 2.0F, kText, mark);
    } else if (coil.kind == CoilKind::Negated) {
        draw->AddLine({center - g.scale(5), y + g.scale(7)}, {center + g.scale(5), y - g.scale(7)}, symbol, thickness);
    }
    operandText(draw, center, y - radius - g.scale(18.0F), coil.operand);
}

// The operand connected to a pin, or "..." if nothing is (as in TIA Portal).
std::string operandOf(const std::vector<Pin>& pins, std::string_view name) {
    for (const auto& pin : pins) {
        if (pin.name.size() == name.size() &&
            std::equal(pin.name.begin(), pin.name.end(), name.begin(),
                       [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == b; })) {
            return pin.operand;
        }
    }
    return "...";
}

void drawBox(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Box& box, const BoxCatalog& boxes,
             const RungOnline* online) {
    const auto* power = online != nullptr ? online->power.find(item.path) : nullptr;
    const float left = g.x(item.column);
    const float right = g.x(item.column + item.width);
    const float y = g.y(item.row);
    const float inset = g.scale(10.0F);
    const float thickness = g.scale(1.6F);
    const float boxTop = y - g.scale(14.0F);
    const float boxBottom = g.top(item.row + item.height) - g.scale(8.0F);
    draw->AddLine({left, y}, {left + inset, y}, wireColor(power && power->in), thickness);
    draw->AddLine({right - inset, y}, {right, y}, wireColor(power && power->out), thickness);
    draw->AddRect({left + inset, boxTop}, {right - inset, boxBottom}, kWire, g.scale(3.0F), 0, thickness);

    const float center = (left + right) / 2.0F;
    const float line = ImGui::GetTextLineHeight();
    operandText(draw, center, boxTop - line - g.scale(2.0F), box.instance);
    centeredText(draw, center, boxTop + g.scale(2.0F), kText, box.type);

    const auto* type = boxes.find(box.type);
    if (type == nullptr) {
        return; // unknown: just the frame and the names (check() reports it)
    }
    const float pinLeft = left + inset + g.scale(4.0F);
    const float pinRight = right - inset - g.scale(4.0F);
    const auto textY = [&](std::size_t row) {
        return y + static_cast<float>(row) * g.scale(Metrics{}.row) - line / 2.0F;
    };
    draw->AddText({pinLeft, textY(0)}, kPinText, std::string(type->powerInput).c_str());
    const auto out = std::string(type->powerOutput);
    draw->AddText({pinRight - ImGui::CalcTextSize(out.c_str()).x, textY(0)}, kPinText, out.c_str());
    // Inputs on the left half, outputs on the right half; each clipped to its
    // half, so long operands can't run into each other.
    const float middle = (pinLeft + pinRight) / 2.0F;
    draw->PushClipRect({pinLeft, boxTop}, {middle - g.scale(2.0F), boxBottom}, true);
    // Online: a pin's operand with its value, "PV preset = 100", in green.
    const auto valueOf = [&](const std::string& operand) -> std::optional<std::string> {
        if (online == nullptr || !online->value) {
            return std::nullopt;
        }
        return online->value(operand);
    };
    for (std::size_t i = 0; i < type->inputs.size(); ++i) {
        const auto operand = operandOf(box.inputs, type->inputs[i]);
        const auto value = valueOf(operand);
        const auto text = std::string(type->inputs[i]) + " " + operand + (value ? " = " + *value : "");
        draw->AddText({pinLeft, textY(i + 1)}, value ? kValue : kPinText, text.c_str());
    }
    draw->PopClipRect();
    draw->PushClipRect({middle + g.scale(2.0F), boxTop}, {pinRight, boxBottom}, true);
    for (std::size_t i = 0; i < type->outputs.size(); ++i) {
        const auto operand = operandOf(box.outputs, type->outputs[i]);
        const auto value = valueOf(operand);
        const auto text = operand + (value ? " = " + *value : "") + " " + std::string(type->outputs[i]);
        draw->AddText({pinRight - ImGui::CalcTextSize(text.c_str()).x, textY(i + 1)}, value ? kValue : kPinText,
                      text.c_str());
    }
    draw->PopClipRect();
}

// Online: which points of the grid have power. A point is a column boundary
// on a row; wires and links join points into one electrical net, so power
// at one point of a net is power at all of them. Power enters at the left
// rail and wherever an element gives it out.
class PoweredPoints {
public:
    PoweredPoints(const RungLayout& layout, const RungPower& power) {
        mark(0, 0);
        for (const auto& item : layout.items) {
            const auto* p = power.find(item.path);
            if (p != nullptr && p->out) {
                mark(item.column + item.width, item.row);
            }
        }
        for (bool changed = true; changed;) { // until no net gains power
            changed = false;
            for (const auto& wire : layout.wires) {
                if (has(wire.fromColumn, wire.row) != has(wire.toColumn, wire.row)) {
                    mark(wire.fromColumn, wire.row);
                    mark(wire.toColumn, wire.row);
                    changed = true;
                }
            }
            for (const auto& link : layout.links) {
                bool any = false;
                for (auto row = link.fromRow; row <= link.toRow; ++row) {
                    any = any || has(link.column, row);
                }
                for (auto row = link.fromRow; any && row <= link.toRow; ++row) {
                    if (!has(link.column, row)) {
                        mark(link.column, row);
                        changed = true;
                    }
                }
            }
        }
    }

    bool has(std::size_t column, std::size_t row) const {
        return std::find(points_.begin(), points_.end(), std::pair{column, row}) != points_.end();
    }

private:
    void mark(std::size_t column, std::size_t row) {
        if (!has(column, row)) {
            points_.emplace_back(column, row);
        }
    }

    std::vector<std::pair<std::size_t, std::size_t>> points_;
};

bool contains(const ImVec4& r, ImVec2 p) {
    return p.x >= r.x && p.x < r.z && p.y >= r.y && p.y < r.w;
}

} // namespace

ImVec2 itemCenter(const PlacedItem& item, ImVec2 origin, float zoom) {
    const Grid g(origin, zoom);
    return {(g.x(item.column) + g.x(item.column + item.width)) / 2.0F, g.y(item.row)};
}

RungView drawRung(const Rung& rung, const RungLayout& layout, int id, const std::optional<ItemPath>& selected,
                  float zoom, const BoxCatalog& boxes, const RungOnline* online) {
    const Metrics m;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{(m.rail * 2.0F + static_cast<float>(layout.width) * m.column) * zoom,
                      static_cast<float>(layout.height) * m.row * zoom};
    ImGui::PushID(id);
    ImGui::InvisibleButton("rung", size); // takes the space, and lets ImGui know when the mouse is over it
    const bool overRung = ImGui::IsItemHovered();
    ImGui::PopID();

    auto* draw = ImGui::GetWindowDrawList();
    const Grid g(origin, zoom);
    const float thickness = g.scale(1.6F);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    RungView result{size, std::nullopt, overRung};

    // Highlights first, so the symbols are drawn over them.
    for (const auto& item : layout.items) {
        const auto r = g.bounds(item);
        if (selected && *selected == item.path) {
            draw->AddRectFilled({r.x, r.y}, {r.z, r.w}, kSelected, g.scale(4.0F));
            draw->AddRect({r.x, r.y}, {r.z, r.w}, kSelectedBorder, g.scale(4.0F));
        } else if (overRung && contains(r, mouse)) {
            draw->AddRectFilled({r.x, r.y}, {r.z, r.w}, kHover, g.scale(4.0F));
        }
        if (overRung && contains(r, mouse)) {
            result.hovered = item.path;
        }
    }

    // The rails: the left one feeds power, the right one closes the circuit.
    const float railTop = g.top(0) + g.scale(6.0F);
    const float railBottom = g.top(layout.height) - g.scale(6.0F);
    draw->AddLine({g.x(0), railTop}, {g.x(0), railBottom}, wireColor(online != nullptr),
                  g.scale(3.0F)); // online, it's live
    draw->AddLine({g.x(layout.width), railTop}, {g.x(layout.width), railBottom}, kWire, g.scale(3.0F));

    std::optional<PoweredPoints> powered;
    if (online != nullptr) {
        powered.emplace(layout, online->power);
    }
    const auto at = [&](std::size_t column, std::size_t row) {
        return wireColor(powered && powered->has(column, row));
    };
    for (const auto& wire : layout.wires) {
        draw->AddLine({g.x(wire.fromColumn), g.y(wire.row)}, {g.x(wire.toColumn), g.y(wire.row)},
                      at(wire.fromColumn, wire.row), thickness);
    }
    for (const auto& link : layout.links) {
        draw->AddLine({g.x(link.column), g.y(link.fromRow)}, {g.x(link.column), g.y(link.toRow)},
                      at(link.column, link.fromRow), thickness);
    }
    const auto powerOf = [&](const PlacedItem& item) { return online ? online->power.find(item.path) : nullptr; };
    for (const auto& item : layout.items) {
        if (const auto* coilPath = std::get_if<CoilPath>(&item.path)) {
            drawCoil(draw, g, item, rung.coils[coilPath->index], powerOf(item));
            continue;
        }
        const auto* element = elementAt(rung, std::get<ElementPath>(item.path));
        if (const auto* contact = std::get_if<Contact>(element)) {
            drawContact(draw, g, item, *contact, powerOf(item));
        } else if (const auto* box = std::get_if<Box>(element)) {
            drawBox(draw, g, item, *box, boxes, online);
        }
    }
    return result;
}

} // namespace view
