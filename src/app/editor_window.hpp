#pragma once
#include "ladder/edit.hpp"
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

namespace app {

// What's selected: a network, and maybe an element or coil in it.
struct Selection {
    std::size_t rung = 0;
    std::optional<ladder::ItemPath> item;
};

// The editor's main window: every rung of the block, one under the other,
// like the networks of a LAD block in TIA Portal; a toolbar and keyboard
// shortcuts to edit them; and a panel with details about the selection.
//
// Every change goes through the structured edit operations (edit.hpp), so
// the rungs are always valid. Operands are edited in place, in a popup.
class EditorWindow {
public:
    // `path` is the file to save to; empty for the built-in example (no saving).
    EditorWindow(std::string path, std::vector<ladder::Rung> rungs);

    void draw();

    bool modified() const { return modified_; }
    const std::vector<ladder::Rung>& rungs() const { return rungs_; }
    const std::optional<Selection>& selection() const { return selection_; }
    float zoom() const { return zoom_; }

    // Where network `index` was drawn in the last frame (its top-left corner
    // on screen), or nothing if it wasn't drawn. For the UI tests, and for
    // scrolling to a network.
    std::optional<ImVec2> rungOrigin(std::size_t index) const;

private:
    void drawToolbar();
    void drawNetworks();
    void drawDetails();
    void drawEditPopup();
    void drawBoxPopup();
    void handleShortcuts();

    // Actions, from the toolbar or the keyboard.
    void insertElement(ladder::Element element);
    void addBranch();
    void addSide();
    void addCoil();
    void deleteSelected();
    void moveSelected(int direction);
    void addNetwork();
    void deleteNetwork(std::size_t index);
    void moveNetwork(std::size_t index, int direction);
    void save();

    void select(std::size_t rung, std::optional<ladder::ItemPath> item, bool edit = false);
    void changed() { modified_ = true; }
    void report(std::string message) { status_ = std::move(message); }
    std::string newInstanceName(std::string_view type) const;

    std::string path_;
    std::vector<ladder::Rung> rungs_;
    std::optional<Selection> selection_;
    bool openEditor_ = false;   // open the edit popup on the next frame
    bool openBoxes_ = false;    // open the box palette on the next frame
    std::size_t boxChoice_ = 0; // the highlighted entry in the box palette
    bool modified_ = false;
    std::string status_; // the result of the last action, when it failed
    float zoom_ = 1.0F;
    std::vector<ImVec2> origins_; // where each network was drawn, last frame
};

} // namespace app
