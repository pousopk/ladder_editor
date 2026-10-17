#pragma once
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <imgui.h>
#include <optional>

// Draws one rung with Dear ImGui, from its layout, and tells which element
// the mouse is over. Only drawing: nothing here changes the rung.
namespace view {

// Sizes in pixels at zoom 1.
struct Metrics {
    float column = 84.0F; // width of one grid column
    float row = 62.0F;    // height of one grid row
    float power = 30.0F;  // where the power line runs inside a row, from its top
    float rail = 14.0F;   // space left of the left rail
};

struct RungView {
    ImVec2 size;                             // the space the rung takes, including the rails
    std::optional<ladder::ItemPath> hovered; // the element under the mouse, if any
    bool overRung = false;                   // the mouse is somewhere over the rung
};

// The centre of a placed item in pixels, for a rung drawn at `origin`.
ImVec2 itemCenter(const ladder::PlacedItem& item, ImVec2 origin, float zoom);

// Draws `rung` at the current ImGui cursor and moves the cursor below it.
// `selected` is drawn highlighted. `id` must be unique per rung in the window.
RungView drawRung(const ladder::Rung& rung, const ladder::RungLayout& layout, int id,
                  const std::optional<ladder::ItemPath>& selected, float zoom);

} // namespace view
