#pragma once
#include "app/widget_positions.hpp"
#include "ladder/block.hpp"
#include "ladder/edit.hpp"
#include "ladder/history.hpp"
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include "ladder/translate.hpp"
#include "st_support/diagnostics.hpp"
#include "view/code_editor.hpp"
#include <imgui.h>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace app {

using ladder::Selection;

// What a KOP editor needs to know from the rest of the project.
struct KopContext {
    const ladder::BoxCatalog* boxes = &ladder::standardBoxes(); // the box types, the project's own included
    const ladder::TranslatedBlock* translation = nullptr;       // the block as ST, if it's been translated
    std::optional<st_support::StDiagnostic> error;              // soft_plc's error, if it's in this block
};

// The editor for one KOP block: its interface (a table of variables, like
// a block interface in TIA Portal), its networks one under the other, a
// toolbar and keyboard shortcuts to edit them, and details about the
// selection.
//
// Every change goes through the structured edit operations (edit.hpp), so
// the rungs are always valid, and is recorded as a command in the block's
// history (history.hpp), so it can be undone. Operands and interface cells
// are edited in place; each popup or cell session becomes one step.
//
// Next to the networks it can show the block as ST: the code soft_plc
// compiles, with soft_plc's error (if it's in this block) marked in it and
// under the network it comes from.
//
// It draws into the current ImGui window, so the project window decides
// where it goes.
class KopEditor {
public:
    // Draws the editor for `block` and handles its input. Keep passing the
    // same block: the history refers to it.
    void draw(ladder::Block& block, const KopContext& context = {});

    bool modified() const { return history_.modified(); }
    void markSaved() { history_.markSaved(); }
    const ladder::History& history() const { return history_; }

    // The block drawn last (for the tests and the project window).
    const std::vector<ladder::Rung>& rungs() const { return block_->networks; }
    const std::optional<Selection>& selection() const { return selection_; }
    float zoom() const { return zoom_; }
    bool showingSt() const { return showSt_; }
    const view::CodeEditor& stView() const { return stView_; }

    // Where network `index` was drawn in the last frame (its top-left corner
    // on screen), or nothing if it wasn't drawn. For the UI tests, and for
    // scrolling to a network.
    std::optional<ImVec2> rungOrigin(std::size_t index) const;
    // Where some widgets were last drawn: "comment", "+ variable", "name 0"
    // (the first variable's name cell), ... For the UI tests.
    const WidgetPositions& widgets() const { return widgets_; }

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

    void drawToolbar();
    void drawInterface();
    void drawNetworks();
    void drawDetails();
    void drawSt();
    void drawProblems(std::size_t network);
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
    void addVariable();
    void deleteVariable(std::size_t index);
    void undo();
    void redo();

    // Runs `change` on a copy of network `index`; if it works, records the
    // change as one step and selects what it returns. With `edit`, goes
    // straight on to editing the new selection (and merges that edit in).
    template <class Change> void editRung(std::size_t index, std::string what, Change change, bool edit = false);
    void finishPendingEdit();
    void changeInterface(ladder::Interface after, std::string what);

    void select(std::size_t rung, std::optional<ladder::ItemPath> item, bool edit = false);
    void report(std::string message) { status_ = std::move(message); }
    std::string newInstanceName(std::string_view type) const;

    ladder::Block* block_ = nullptr;      // the block being drawn (set by draw)
    const KopContext* context_ = nullptr; // and what it knows about the project
    const ladder::BoxCatalog& boxes() const { return *context_->boxes; }
    ladder::History history_;
    std::optional<Selection> selection_;
    std::optional<PendingEdit> pending_;        // the edit popup's session, while it's open
    std::optional<PendingEdit> commentEdit_;    // the comment field's session, while it's active
    std::optional<ladder::Interface> cellEdit_; // an interface cell's session: the interface before
    bool mergeNextEdit_ = false;                // the next popup session belongs to the last step
    bool openEditor_ = false;                   // open the edit popup on the next frame
    bool openBoxes_ = false;                    // open the box palette on the next frame
    std::size_t boxChoice_ = 0;                 // the highlighted entry in the box palette
    std::string status_;                        // the result of the last action, when it failed
    float zoom_ = 1.0F;
    bool showSt_ = false;         // the ST panel is shown
    view::CodeEditor stView_;     // the ST panel: read-only
    std::string stShown_;         // the text the ST panel shows
    std::vector<ImVec2> origins_; // where each network was drawn, last frame
    WidgetPositions widgets_;
};

} // namespace app
