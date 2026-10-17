#pragma once
#include "app/kop_editor.hpp"
#include "app/widget_positions.hpp"
#include "ladder/block.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {

// The editor's main window: the project's blocks on the left (like the
// "Program blocks" folder in TIA Portal), the open block on the right, and
// the project's problems under the block list. KOP blocks open in a
// KopEditor (one per block, each with its own undo history); ST blocks in a
// text editor (milestone 6 makes it a real code editor).
class ProjectWindow {
public:
    // `folder` is where the project is saved; empty when it isn't saved
    // anywhere (the built-in example).
    ProjectWindow(ladder::Project project, std::filesystem::path folder);

    void draw();

    bool modified() const;
    const ladder::Project& project() const { return project_; }
    std::size_t currentBlock() const { return current_; }
    KopEditor* kopEditor(std::size_t block) { return block < editors_.size() ? editors_[block].get() : nullptr; }

    // Where some widgets were last drawn: "new block", "block 1", "source",
    // "create", ... For the UI tests.
    const WidgetPositions& widgets() const { return widgets_; }

    // Closing: with unsaved changes, the window first asks what to do.
    void askToClose() { askClose_ = true; }
    bool closeConfirmed() const { return closeConfirmed_; }

private:
    void drawHeader();
    void drawBlockList();
    void drawProblems();
    void drawCurrentBlock();
    void drawNewBlockPopup();
    void drawRenamePopup();
    void drawDeletePopup();
    void drawClosePopup();

    bool save();
    void addBlock(ladder::Block block);
    void deleteBlock(std::size_t index);
    bool blockModified(std::size_t index) const;
    bool nameTaken(const std::string& name, std::size_t except) const;

    ladder::Project project_;
    std::filesystem::path folder_;
    std::vector<std::unique_ptr<KopEditor>> editors_; // one per block; null for ST blocks
    std::vector<std::string> savedSources_;           // ST blocks: their text as saved
    bool structureModified_ = false;                  // blocks added, deleted or renamed since saving
    std::size_t current_ = 0;
    std::string status_;
    WidgetPositions widgets_;

    // The popups' state.
    bool openNewBlock_ = false;
    bool openRename_ = false;
    bool openDelete_ = false;
    bool askClose_ = false;
    bool closeConfirmed_ = false;
    std::string newName_;
    int newKind_ = 0;
    int newLanguage_ = 0;
};

} // namespace app
