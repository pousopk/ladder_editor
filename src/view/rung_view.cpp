#include "view/rung_view.hpp"
#include "ladder/boxes.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
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
// Online, as in TIA Portal: power flows along solid green, not along dashed
// blue. A contact is green when it's closed, blue when it's open.
const ImU32 kPowered = IM_COL32(70, 210, 100, 255);
const ImU32 kNoPower = IM_COL32(80, 130, 230, 255);
const ImU32 kValue = IM_COL32(120, 215, 135, 255); // online: a value read from the PLC

enum class Flow { Offline, Powered, Unpowered };

Flow flowOf(bool online, bool powered) {
    return !online ? Flow::Offline : powered ? Flow::Powered : Flow::Unpowered;
}

// A straight wire: plain offline, green with power, dashed blue without.
void wire(ImDrawList* draw, ImVec2 from, ImVec2 to, Flow flow, float thickness) {
    if (flow != Flow::Unpowered) {
        draw->AddLine(from, to, flow == Flow::Powered ? kPowered : kWire, thickness);
        return;
    }
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float dash = thickness * 3.5F; // dashes and gaps scale with the zoom, like the thickness
    const float gap = thickness * 2.5F;
    for (float at = 0.0F; at < length; at += dash + gap) {
        const float end = std::min(at + dash, length);
        draw->AddLine({from.x + dx * at / length, from.y + dy * at / length},
                      {from.x + dx * end / length, from.y + dy * end / length}, kNoPower, thickness);
    }
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
    wire(draw, {left, y}, {center - halfSymbol, y}, flowOf(power != nullptr, power && power->in), thickness);
    wire(draw, {center + halfSymbol, y}, {right, y}, flowOf(power != nullptr, power && power->out), thickness);
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
    // Online, a closed contact (one that would pass power) is green, with or
    // without power; an open one blue; one whose value isn't known yet plain.
    const auto symbol = power == nullptr || !power->conducting ? kWire : *power->conducting ? kPowered : kNoPower;
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
    const auto symbol = power == nullptr ? kWire : power->in ? kPowered : kNoPower;
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
    wire(draw, {left, y}, {left + inset, y}, flowOf(power != nullptr, power && power->in), thickness);
    wire(draw, {right - inset, y}, {right, y}, flowOf(power != nullptr, power && power->out), thickness);
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
    // Inputs on the left half, outputs on the right half. An operand too
    // long for its half is cut short (the tooltip shows it whole); online,
    // its value goes on the line below, so it's never the part cut off.
    const float half = (pinRight - pinLeft) / 2.0F - g.scale(2.0F);
    const auto valueOf = [&](const std::string& operand) -> std::optional<std::string> {
        if (online == nullptr || !online->value) {
            return std::nullopt;
        }
        return online->value(operand);
    };
    for (std::size_t i = 0; i < type->inputs.size(); ++i) {
        const auto operand = operandOf(box.inputs, type->inputs[i]);
        const auto text = fitText(type->inputs[i] + " " + operand, half);
        draw->AddText({pinLeft, textY(i + 1)}, kPinText, text.c_str());
        if (const auto value = valueOf(operand)) {
            const auto shown = fitText("= " + *value, half);
            draw->AddText({pinLeft, textY(i + 1) + line}, kValue, shown.c_str());
        }
    }
    for (std::size_t i = 0; i < type->outputs.size(); ++i) {
        const auto operand = operandOf(box.outputs, type->outputs[i]);
        const auto pin = " " + type->outputs[i];
        const auto text = fitText(operand, half - ImGui::CalcTextSize(pin.c_str()).x) + pin;
        draw->AddText({pinRight - ImGui::CalcTextSize(text.c_str()).x, textY(i + 1)}, kPinText, text.c_str());
        if (const auto value = valueOf(operand)) {
            const auto shown = fitText("= " + *value, half);
            draw->AddText({pinRight - ImGui::CalcTextSize(shown.c_str()).x, textY(i + 1) + line}, kValue,
                          shown.c_str());
        }
    }
}

// Everything about a box, uncut: its pins, their operands and, online, their values.
void boxTooltip(const Box& box, const BoxCatalog& boxes, const RungOnline* online) {
    ImGui::Text("%s %s", box.type.c_str(), box.instance.c_str());
    const auto* type = boxes.find(box.type);
    if (type == nullptr) {
        return;
    }
    const auto row = [&](const std::string& pin, const std::string& operand, const char* arrow) {
        std::optional<std::string> value;
        if (online != nullptr && online->value) {
            value = online->value(operand);
        }
        ImGui::Text("  %-4s %s %s", pin.c_str(), arrow, operand.c_str());
        if (value) {
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kValue), "= %s", value->c_str());
        }
    };
    for (const auto& pin : type->inputs) {
        row(pin, operandOf(box.inputs, pin), ":=");
    }
    for (const auto& pin : type->outputs) {
        row(pin, operandOf(box.outputs, pin), "=>");
    }
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

std::string fitText(std::string_view text, float width) {
    if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <= width) {
        return std::string(text);
    }
    const float dots = ImGui::CalcTextSize("..").x;
    std::size_t length = text.size();
    while (length > 0 && ImGui::CalcTextSize(text.data(), text.data() + length).x + dots > width) {
        --length;
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
            --length; // never cut a UTF-8 character in half
        }
    }
    return std::string(text.substr(0, length)) + "..";
}

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
    draw->AddLine({g.x(0), railTop}, {g.x(0), railBottom}, online != nullptr ? kPowered : kWire,
                  g.scale(3.0F)); // online, it's live
    draw->AddLine({g.x(layout.width), railTop}, {g.x(layout.width), railBottom}, kWire, g.scale(3.0F));

    std::optional<PoweredPoints> powered;
    if (online != nullptr) {
        powered.emplace(layout, online->power);
    }
    const auto at = [&](std::size_t column, std::size_t row) {
        return flowOf(online != nullptr, powered && powered->has(column, row));
    };
    for (const auto& w : layout.wires) {
        wire(draw, {g.x(w.fromColumn), g.y(w.row)}, {g.x(w.toColumn), g.y(w.row)}, at(w.fromColumn, w.row), thickness);
    }
    for (const auto& link : layout.links) {
        wire(draw, {g.x(link.column), g.y(link.fromRow)}, {g.x(link.column), g.y(link.toRow)},
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
    // Hovering a box shows all of it, uncut (after the usual tooltip delay).
    if (result.hovered && std::holds_alternative<ElementPath>(*result.hovered)) {
        const auto* box = std::get_if<Box>(elementAt(rung, std::get<ElementPath>(*result.hovered)));
        if (box != nullptr && ImGui::BeginItemTooltip()) { // the item: the rung's invisible button
            boxTooltip(*box, boxes, online);
            ImGui::EndTooltip();
        }
    }
    return result;
}

} // namespace view
