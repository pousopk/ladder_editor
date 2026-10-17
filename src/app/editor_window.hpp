#pragma once
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <optional>
#include <string>
#include <vector>

namespace app {

// Which element is selected: a rung, and an item in it.
struct Selection {
    std::size_t rung = 0;
    ladder::ItemPath item;
};

// The editor's main window: every rung of the block, one under the other,
// like the networks of a LAD block in TIA Portal, and details about the
// selected element. Milestone 2 only shows and selects; editing comes in 3.
class EditorWindow {
public:
    EditorWindow(std::string title, std::vector<ladder::Rung> rungs);

    void draw();

private:
    void drawToolbar();
    void drawNetworks();
    void drawDetails();

    std::string title_;
    std::vector<ladder::Rung> rungs_;
    std::vector<ladder::RungLayout> layouts_; // one per rung, recomputed when a rung changes
    std::optional<Selection> selection_;
    float zoom_ = 1.0F;
};

} // namespace app
