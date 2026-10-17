#pragma once
#include "ladder/edit.hpp"
#include "ladder/history.hpp"
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <imgui.h>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace app {

using ladder::Selection;

// The editor's main window: every rung of the block, one under the other,
// like the networks of a LAD block in TIA Portal; a toolbar and keyboard
// shortcuts to edit them; and a panel with details about the selection.
//
// Every change goes through the structured edit operations (edit.hpp), so
// the rungs are always valid, and is recorded as a command in the history
// (history.hpp), so it can be undone. Operands are edited in place, in a
// popup; the whole popup session becomes one step.
class EditorWindow {
public:
    // `path` is the file to save to; empty for the built-in example (no saving).
    EditorWindow(std::string path, std::vector<ladder::Rung> rungs);

    void draw();

    bool modified() const { return history_.modified(); }
    const std::vector<ladder::Rung>& rungs() const { return rungs_; }
    const std::optional<Selection>& selection() const { return selection_; }
    const ladder::History& history() const { return history_; }
    float zoom() const { return zoom_; }

    // Where network `index` was drawn in the last frame (its top-left corner
    // on screen), or nothing if it wasn't drawn. For the UI tests, and for
    // scrolling to a network.
    std::optional<ImVec2> rungOrigin(std::size_t index) const;
    // The centre of the selected network's comment field, last frame (for the UI tests).
    std::optional<ImVec2> commentFieldCenter() const { return commentField_; }

private:
    // An edit made directly in a field (the popup, a comment), recorded as
    // one step when it's finished.
    struct PendingEdit {
        std::size_t rung;
        ladder::Rung before;
        std::optional<Selection> selection;
        std::string what;
        bool mergeWithLast; // part of the step just before (an insertion)
    };

    using EditResult = tl::expected<std::optional<ladder::ItemPath>, std::string>;

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
    void undo();
    void redo();
    void save();

    // Runs `change` on a copy of network `index`; if it works, records the
    // change as one step and selects what it returns. With `edit`, goes
    // straight on to editing the new selection (and merges that edit in).
    template <class Change> void editRung(std::size_t index, std::string what, Change change, bool edit = false);
    void finishPendingEdit();

    void select(std::size_t rung, std::optional<ladder::ItemPath> item, bool edit = false);
    void report(std::string message) { status_ = std::move(message); }
    std::string newInstanceName(std::string_view type) const;

    std::string path_;
    std::vector<ladder::Rung> rungs_;
    ladder::History history_;
    std::optional<Selection> selection_;
    std::optional<PendingEdit> pending_;     // the edit popup's session, while it's open
    std::optional<PendingEdit> commentEdit_; // the comment field's session, while it's active
    bool mergeNextEdit_ = false;             // the next popup session belongs to the last step
    bool openEditor_ = false;                // open the edit popup on the next frame
    bool openBoxes_ = false;                 // open the box palette on the next frame
    std::size_t boxChoice_ = 0;              // the highlighted entry in the box palette
    std::string status_;                     // the result of the last action, when it failed
    float zoom_ = 1.0F;
    std::vector<ImVec2> origins_; // where each network was drawn, last frame
    std::optional<ImVec2> commentField_;
};

} // namespace app
