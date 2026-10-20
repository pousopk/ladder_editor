#pragma once
#include "app/kop_editor.hpp"
#include "app/watch_table.hpp"
#include "app/widget_positions.hpp"
#include "ladder/block.hpp"
#include "online/session.hpp"
#include "online/simulator.hpp"
#include "st_support/diagnostics.hpp"
#include "view/code_editor.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

// The editor's main window: the project's blocks on the left (like the
// "Program blocks" folder in TIA Portal), the open block on the right, and
// the project's problems under the block list. The whole project is
// translated to ST and compiled by soft_plc whenever it changes.
//
// Online mode runs the project in a simulator inside the editor, or
// connects to a soft_plc started with --monitor, and shows the open block's
// live values: power flow on the rungs, values next to ST lines. KOP blocks open in a
// KopEditor (one per block, each with its own undo history); ST blocks in a
// code editor with colours and live errors from soft_plc's compiler.
class ProjectWindow {
public:
    // `folder` is where the project is saved; empty when it isn't saved
    // anywhere yet (then saving asks where).
    ProjectWindow(ladder::Project project, std::filesystem::path folder);

    void draw();

    bool modified() const;
    // Saves every block. With no folder yet, it asks where first (and
    // returns false: the popup saves).
    bool save();
    const ladder::Project& project() const { return project_; }
    const std::filesystem::path& folder() const { return folder_; } // empty: not saved anywhere yet
    std::size_t currentBlock() const { return current_; }
    bool showingWatchTable() const { return showWatch_; }
    const WatchTable& watchTable() const { return watchTable_; }
    KopEditor* kopEditor(std::size_t block) { return block < editors_.size() ? editors_[block].get() : nullptr; }
    const view::CodeEditor* codeEditor(std::size_t block) const {
        return block < codeEditors_.size() ? codeEditors_[block].get() : nullptr;
    }

    // Where some widgets were last drawn: "new block", "block 1", "source",
    // "create", ... For the UI tests.
    const WidgetPositions& widgets() const { return widgets_; }

    // Closing: with unsaved changes, the window first asks what to do.
    void askToClose() { askClose_ = true; }
    bool closeConfirmed() const { return closeConfirmed_; }

    // The first problem soft_plc's compiler finds in the project, if any.
    const std::optional<st_support::StDiagnostic>& codeError() const { return check_.error; }
    // Online mode, as the header's buttons do it. False (and a status
    // message) if it can't: the project has errors, nothing listens on the port.
    bool simulate();
    bool connect(std::uint16_t port);
    // Online change: loads the project as it is now into the running PLC,
    // which keeps the values of the variables still there. The answer comes
    // a moment later (the header shows it).
    bool download();
    bool downloadPending() const { return pendingDownload_.has_value(); }
    // Upload: replaces the project with the program the connected PLC runs,
    // one ST block per declaration. Asks first when the project has unsaved
    // changes. The answer comes a moment later.
    bool upload();
    bool uploadPending() const { return uploadPending_; }
    const std::string& note() const { return note_; }
    const std::string& status() const { return status_; }
    void goOffline();
    online::Session* session() { return session_.get(); }
    // Online: the instance the open block is watched from ("" for the
    // PROGRAM), or nothing if it isn't watched.
    const std::optional<std::string>& instance() const { return instance_; }

    // The project as ST, as it was last checked.
    const ladder::TranslatedProject& translation() const { return check_.translation; }

private:
    void drawHeader();
    void drawBlockList();
    void drawProblems();
    void drawCurrentBlock();
    void drawNewBlockPopup();
    void drawRenamePopup();
    void drawDeletePopup();
    void drawClosePopup();
    void drawStBlock(std::size_t index);
    void checkCode();
    void drawOnline();
    void updateWatch(const std::vector<std::string>& paths);
    void checkDownload();
    void startUpload();
    void checkUpload();
    void drawUploadPopup();
    // Swaps in an uploaded project: not saved anywhere, every block new.
    void replaceProject(ladder::Project project);
    void makeEditors(); // one KOP or code editor per block, as the project is now
    // Online: the instance a FUNCTION_BLOCK is shown from, chosen in a combo.
    // Nothing when the block can't be watched (a FUNCTION, an unused FB).
    std::optional<std::string> chooseInstance(const ladder::Block& block);
    void exportSt();

    void drawSavePopup();
    void addBlock(ladder::Block block);
    void deleteBlock(std::size_t index);
    bool blockModified(std::size_t index) const;
    bool nameTaken(const std::string& name, std::size_t except) const;

    ladder::Project project_;
    std::filesystem::path folder_;
    std::vector<std::unique_ptr<KopEditor>> editors_;            // one per block; null for ST blocks
    std::vector<std::unique_ptr<view::CodeEditor>> codeEditors_; // one per block; null for KOP blocks
    std::vector<std::size_t> coloredVersion_;                    // the text version each code editor's colours are for
    st_support::CodeCheck check_;                                // the project translated and compiled, as it was...
    std::optional<ladder::Project> checked_; // ...here: when the project differs, it's checked again
    std::vector<std::string> savedSources_;  // ST blocks: their text as saved
    bool structureModified_ = false;
    WatchTable watchTable_;
    bool showWatch_ = false; // the watch table is open instead of a block
    std::vector<ladder::WatchEntry>
        savedWatch_; // the watch table as saved         // blocks added, deleted or renamed since saving
    std::size_t current_ = 0;
    // Online mode.
    std::unique_ptr<online::Simulator> simulator_;   // the PLC inside the editor, when simulating
    std::unique_ptr<online::Session> session_;       // the connection to the PLC; null when offline
    std::optional<ladder::Project> downloaded_;      // the project as the PLC runs it, if this editor loaded it
    std::optional<ladder::Project> pendingDownload_; // sent, waiting for soft_plc's answer
    std::uint64_t loadsSeen_ = 0;                    // the session's loads already handled
    bool uploadPending_ = false;                     // SOURCE sent, waiting for soft_plc's answer
    std::uint64_t uploadsSeen_ = 0;                  // the session's SOURCE answers already handled
    std::vector<std::string> watching_;              // what the session reads
    std::map<std::string, std::string> instances_;   // per FUNCTION_BLOCK: the instance shown
    std::optional<std::string> instance_;
    int port_ = 5020;

    std::string status_; // the last action's error
    std::string note_;   // the last action's result, when it worked
    WidgetPositions widgets_;

    // The popups' state.
    bool openNewBlock_ = false;
    bool openRename_ = false;
    bool openDelete_ = false;
    bool askClose_ = false;
    bool closeConfirmed_ = false;
    bool openSave_ = false;
    bool askUpload_ = false;
    bool quitAfterSave_ = false; // Save and quit is waiting for the Save project popup
    std::string saveFolder_;
    std::string newName_;
    int newKind_ = 0;
    int newLanguage_ = 0;
};

} // namespace app
