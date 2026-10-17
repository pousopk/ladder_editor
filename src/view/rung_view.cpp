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

// A wire from the left edge of a cell to a symbol, and from the symbol to the right edge.
void stubs(ImDrawList* draw, float left, float right, float y, float halfSymbol, float thickness) {
    const float center = (left + right) / 2.0F;
    draw->AddLine({left, y}, {center - halfSymbol, y}, kWire, thickness);
    draw->AddLine({center + halfSymbol, y}, {right, y}, kWire, thickness);
}

void drawContact(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Contact& contact) {
    const float left = g.x(item.column);
    const float right = g.x(item.column + 1);
    const float y = g.y(item.row);
    const float center = (left + right) / 2.0F;
    const float half = g.scale(8.0F);
    const float tall = g.scale(11.0F);
    const float thickness = g.scale(1.6F);
    stubs(draw, left, right, y, half, thickness);
    draw->AddLine({center - half, y - tall}, {center - half, y + tall}, kWire, thickness);
    draw->AddLine({center + half, y - tall}, {center + half, y + tall}, kWire, thickness);
    if (contact.kind == ContactKind::NormallyClosed) {
        draw->AddLine({center - half - g.scale(3), y + tall}, {center + half + g.scale(3), y - tall}, kWire, thickness);
    }
    centeredText(draw, center, y - tall - g.scale(18.0F), kText, contact.operand);
}

void drawCoil(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Coil& coil) {
    const float left = g.x(item.column);
    const float right = g.x(item.column + 1);
    const float y = g.y(item.row);
    const float center = (left + right) / 2.0F;
    const float radius = g.scale(11.0F);
    const float thickness = g.scale(1.6F);
    stubs(draw, left, right, y, radius, thickness);
    // ( ): two arcs of a circle, opening towards each other.
    draw->PathArcTo({center, y}, radius, 2.4F, 3.9F, 12);
    draw->PathStroke(kWire, 0, thickness);
    draw->PathArcTo({center, y}, radius, -0.75F, 0.75F, 12);
    draw->PathStroke(kWire, 0, thickness);
    const char* mark = coil.kind == CoilKind::Set ? "S" : coil.kind == CoilKind::Reset ? "R" : nullptr;
    if (mark != nullptr) {
        centeredText(draw, center, y - ImGui::GetTextLineHeight() / 2.0F, kText, mark);
    } else if (coil.kind == CoilKind::Negated) {
        draw->AddLine({center - g.scale(5), y + g.scale(7)}, {center + g.scale(5), y - g.scale(7)}, kWire, thickness);
    }
    centeredText(draw, center, y - radius - g.scale(18.0F), kText, coil.operand);
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

void drawBox(ImDrawList* draw, const Grid& g, const PlacedItem& item, const Box& box) {
    const float left = g.x(item.column);
    const float right = g.x(item.column + item.width);
    const float y = g.y(item.row);
    const float inset = g.scale(10.0F);
    const float thickness = g.scale(1.6F);
    const float boxTop = y - g.scale(14.0F);
    const float boxBottom = g.top(item.row + item.height) - g.scale(8.0F);
    draw->AddLine({left, y}, {left + inset, y}, kWire, thickness);
    draw->AddLine({right - inset, y}, {right, y}, kWire, thickness);
    draw->AddRect({left + inset, boxTop}, {right - inset, boxBottom}, kWire, g.scale(3.0F), 0, thickness);

    const float center = (left + right) / 2.0F;
    const float line = ImGui::GetTextLineHeight();
    centeredText(draw, center, boxTop - line - g.scale(2.0F), kText, box.instance);
    centeredText(draw, center, boxTop + g.scale(2.0F), kText, box.type);

    const auto* type = findBoxType(box.type);
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
    for (std::size_t i = 0; i < type->inputs.size(); ++i) {
        const auto text = std::string(type->inputs[i]) + " " + operandOf(box.inputs, type->inputs[i]);
        draw->AddText({pinLeft, textY(i + 1)}, kPinText, text.c_str());
    }
    draw->PopClipRect();
    draw->PushClipRect({middle + g.scale(2.0F), boxTop}, {pinRight, boxBottom}, true);
    for (std::size_t i = 0; i < type->outputs.size(); ++i) {
        const auto text = operandOf(box.outputs, type->outputs[i]) + " " + std::string(type->outputs[i]);
        draw->AddText({pinRight - ImGui::CalcTextSize(text.c_str()).x, textY(i + 1)}, kPinText, text.c_str());
    }
    draw->PopClipRect();
}

bool contains(const ImVec4& r, ImVec2 p) {
    return p.x >= r.x && p.x < r.z && p.y >= r.y && p.y < r.w;
}

} // namespace

RungView drawRung(const Rung& rung, const RungLayout& layout, int id, const std::optional<ItemPath>& selected,
                  float zoom) {
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
    RungView result{size, std::nullopt};

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
    draw->AddLine({g.x(0), railTop}, {g.x(0), railBottom}, kWire, g.scale(3.0F));
    draw->AddLine({g.x(layout.width), railTop}, {g.x(layout.width), railBottom}, kWire, g.scale(3.0F));

    for (const auto& wire : layout.wires) {
        draw->AddLine({g.x(wire.fromColumn), g.y(wire.row)}, {g.x(wire.toColumn), g.y(wire.row)}, kWire, thickness);
    }
    for (const auto& link : layout.links) {
        draw->AddLine({g.x(link.column), g.y(link.fromRow)}, {g.x(link.column), g.y(link.toRow)}, kWire, thickness);
    }
    for (const auto& item : layout.items) {
        if (const auto* coilPath = std::get_if<CoilPath>(&item.path)) {
            drawCoil(draw, g, item, rung.coils[coilPath->index]);
            continue;
        }
        const auto* element = elementAt(rung, std::get<ElementPath>(item.path));
        if (const auto* contact = std::get_if<Contact>(element)) {
            drawContact(draw, g, item, *contact);
        } else if (const auto* box = std::get_if<Box>(element)) {
            drawBox(draw, g, item, *box);
        }
    }
    return result;
}

} // namespace view
