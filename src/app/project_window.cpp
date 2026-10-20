#include "app/project_window.hpp"
#include "app/st_display.hpp"
#include "ladder/project_file.hpp"
#include "ladder/st_split.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace app {

namespace {

using namespace ladder;

const ImVec4 kProblem{0.95F, 0.45F, 0.40F, 1.0F};
const ImVec4 kDim{0.6F, 0.6F, 0.65F, 1.0F};
const ImVec4 kKop{0.45F, 0.70F, 0.95F, 1.0F};
const ImVec4 kSt{0.85F, 0.70F, 0.40F, 1.0F};
const ImVec4 kOnline{0.45F, 0.85F, 0.50F, 1.0F};
const ImVec4 kStop{0.95F, 0.70F, 0.30F, 1.0F};

constexpr float kListWidth = 250.0F;

std::unique_ptr<view::CodeEditor> codeEditorFor(const Block& block) {
    return block.language == Language::St ? std::make_unique<view::CodeEditor>(block.source) : nullptr;
}
constexpr BlockKind kKinds[] = {BlockKind::Program, BlockKind::FunctionBlock, BlockKind::Function, BlockKind::Types};

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

} // namespace

ProjectWindow::ProjectWindow(Project project, std::filesystem::path folder)
    : project_(std::move(project)), folder_(std::move(folder)) {
    makeEditors();
    savedWatch_ = project_.watch;
}

void ProjectWindow::makeEditors() {
    editors_.clear();
    codeEditors_.clear();
    coloredVersion_.clear();
    savedSources_.clear();
    for (const auto& block : project_.blocks) {
        editors_.push_back(block.language == Language::Kop ? std::make_unique<KopEditor>() : nullptr);
        codeEditors_.push_back(codeEditorFor(block));
        coloredVersion_.push_back(static_cast<std::size_t>(-1)); // not coloured yet
        savedSources_.push_back(block.source);
    }
}

bool ProjectWindow::blockModified(std::size_t index) const {
    if (editors_[index]) {
        return editors_[index]->modified();
    }
    return project_.blocks[index].source != savedSources_[index];
}

bool ProjectWindow::modified() const {
    if (structureModified_ || project_.watch != savedWatch_) {
        return true;
    }
    for (std::size_t i = 0; i < project_.blocks.size(); ++i) {
        if (blockModified(i)) {
            return true;
        }
    }
    return false;
}

void ProjectWindow::draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ladder_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    widgets_.clear();
    if (!checked_ || *checked_ != project_) {
        checkCode(); // something changed since the last check
    }
    drawHeader();
    drawOnline();
    ImGui::Separator();

    ImGui::BeginChild("blocks", ImVec2(kListWidth, 0), ImGuiChildFlags_Borders);
    drawBlockList();
    drawProblems();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("editor", ImVec2(0, 0));
    drawCurrentBlock();
    ImGui::EndChild();

    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        save();
    }
    drawNewBlockPopup();
    drawRenamePopup();
    drawDeletePopup();
    drawClosePopup();
    drawSavePopup();
    drawUploadPopup();
    ImGui::End();
}

void ProjectWindow::drawHeader() {
    ImGui::Text("%s", project_.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", folder_.empty() ? "(not saved yet)" : folder_.string().c_str());
    if (modified()) {
        ImGui::SameLine(0, 2);
        ImGui::TextUnformatted("*");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save();
    }
    ImGui::SetItemTooltip("Save every block of the project (Ctrl+S)");
    widgets_.remember("save");
    ImGui::SameLine();
    ImGui::BeginDisabled(folder_.empty());
    if (ImGui::Button("Export ST")) {
        exportSt();
    }
    ImGui::SetItemTooltip("Write the whole project as one ST file, KOP blocks translated, for soft_plc");
    widgets_.remember("export st");
    ImGui::EndDisabled();
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kProblem, "%s", status_.c_str());
    } else if (!note_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", note_.c_str());
    }
}

void ProjectWindow::drawBlockList() {
    ImGui::TextColored(kDim, "Blocks");
    for (std::size_t i = 0; i < project_.blocks.size(); ++i) {
        const auto& block = project_.blocks[i];
        ImGui::PushID(static_cast<int>(i));
        const auto start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##block", current_ == i && !showWatch_, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0, ImGui::GetTextLineHeight()))) {
            current_ = i;
            showWatch_ = false;
        }
        widgets_.remember(fmt::format("block {}", i));
        ImGui::SetCursorPos(start);
        ImGui::TextColored(block.language == Language::Kop ? kKop : kSt, "%-3s", languageName(block.language));
        ImGui::SameLine();
        ImGui::Text("%s%s", block.name.c_str(), blockModified(i) ? " *" : "");
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", kindName(block.kind));
        ImGui::PopID();
    }
    // The watch table, under the blocks (as in TIA Portal's project tree).
    const auto watchLabel =
        fmt::format("Watch table ({}){}###watch", project_.watch.size(), project_.watch != savedWatch_ ? " *" : "");
    if (ImGui::Selectable(watchLabel.c_str(), showWatch_)) {
        showWatch_ = true;
    }
    widgets_.remember("watch table");
    ImGui::Spacing();
    if (ImGui::SmallButton("New block")) {
        openNewBlock_ = true;
    }
    widgets_.remember("new block");
    ImGui::SameLine();
    ImGui::BeginDisabled(project_.blocks.empty());
    if (ImGui::SmallButton("Rename")) {
        openRename_ = true;
    }
    widgets_.remember("rename");
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        openDelete_ = true;
    }
    widgets_.remember("delete");
    ImGui::EndDisabled();
}

void ProjectWindow::drawProblems() {
    const auto problems = checkProject(project_);
    const auto& error = check_.error;
    const auto count = problems.size() + (error ? 1 : 0);
    if (count == 0) {
        return;
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(kProblem, "%zu problem%s", count, count == 1 ? "" : "s");
    if (error && error->block < project_.blocks.size()) {
        // soft_plc's first error; a click opens its block.
        const auto& block = project_.blocks[error->block];
        const auto where = block.language == Language::St ? fmt::format("line {}", error->line + 1)
                           : error->network               ? fmt::format("network {}", *error->network + 1)
                                                          : std::string("interface");
        const auto text = fmt::format("{}, {}: {}", block.name, where, error->message);
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kProblem, "%s", text.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemClicked()) {
            current_ = error->block;
        }
        widgets_.remember("st problem");
    }
    for (const auto& problem : problems) {
        const auto text = problem.block.empty() ? problem.message : problem.block + ": " + problem.message;
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kProblem, "%s", text.c_str());
        ImGui::PopTextWrapPos();
    }
}

void ProjectWindow::drawCurrentBlock() {
    if (showWatch_) {
        ImGui::TextUnformatted("Watch table");
        watchTable_.draw(project_.watch, session_.get());
        updateWatch(watchTable_.watched());
        return;
    }
    if (project_.blocks.empty()) {
        ImGui::TextColored(kDim, "No blocks yet: click \"New block\".");
        return;
    }
    current_ = std::min(current_, project_.blocks.size() - 1);
    auto& block = project_.blocks[current_];
    ImGui::Text("%s %s", kindName(block.kind), block.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(block.language == Language::Kop ? kKop : kSt, "(%s)", languageName(block.language));
    // Each block's popups and fields are its own. The editor object never
    // moves (the blocks' vector may), so its address is a stable ID.
    ImGui::PushID(editors_[current_] ? static_cast<const void*>(editors_[current_].get())
                                     : static_cast<const void*>(codeEditors_[current_].get()));
    instance_ = session_ ? chooseInstance(block) : std::nullopt;
    const auto& instance = instance_;
    if (auto* editor = editors_[current_].get()) {
        app::KopContext context;
        if (instance) {
            context.session = session_.get();
            context.instance = *instance;
        }
        context.boxes = &check_.boxes;
        const auto& parts = check_.translation.parts;
        if (current_ < parts.size() && parts[current_].block == current_) {
            context.translation = &parts[current_].translation;
        }
        if (check_.error && check_.error->block == current_) {
            context.error = check_.error;
        }
        editor->draw(block, context);
        updateWatch(instance ? editor->watched() : std::vector<std::string>{});
    } else {
        drawStBlock(current_);
    }
    ImGui::PopID();
}

void ProjectWindow::drawStBlock(std::size_t index) {
    auto& block = project_.blocks[index];
    auto& editor = *codeEditors_[index];
    if (coloredVersion_[index] != editor.document().version()) {
        editor.setColors(stColors(editor.document().text()));
        coloredVersion_[index] = editor.document().version();
    }
    const auto errorIn = [&](std::size_t i) { return check_.error && check_.error->block == i; };
    editor.setError(
        errorIn(index) ? std::optional(view::ErrorMark{check_.error->line, check_.error->column, check_.error->message})
                       : std::nullopt);

    std::vector<std::string> watched;
    editor.setNotes(session_ && instance_ ? onlineNotes(editor.document().text(), *session_, *instance_, watched)
                                          : std::vector<std::string>{});
    updateWatch(watched);

    const float messageHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0F;
    if (editor.draw("##source", ImVec2(-1.0F, -messageHeight))) {
        block.source = editor.document().text();
        checkCode(); // right away: the error follows each keystroke (and may be gone now)
    }
    widgets_.remember("source");
    if (errorIn(index)) {
        ImGui::TextColored(kProblem, "line %zu, column %zu: %s", check_.error->line + 1, check_.error->column + 1,
                           check_.error->message.c_str());
    } else if (check_.error) {
        ImGui::TextColored(kDim, "No errors here. soft_plc's first error is in block %s.",
                           project_.blocks[check_.error->block].name.c_str());
    } else {
        ImGui::TextColored(kDim, "No errors: soft_plc compiles the whole project.");
    }
}

void ProjectWindow::checkCode() {
    check_ = st_support::checkCode(project_);
    checked_ = project_;
}

void ProjectWindow::exportSt() {
    const auto path = folder_ / (project_.name + ".st");
    std::ofstream file(path, std::ios::binary);
    file << check_.translation.text;
    file.close();
    if (!file) {
        status_ = fmt::format("Couldn't write {}", path.string());
        return;
    }
    status_.clear();
    note_ = fmt::format("Exported {}{}", path.filename().string(),
                        check_.error ? ", but soft_plc reports an error in it" : "");
}

// ---------------------------------------------------------------- Online

void ProjectWindow::drawOnline() {
    if (!session_) {
        if (ImGui::Button("Simulate")) {
            simulate();
        }
        ImGui::SetItemTooltip("Run the project in a soft_plc inside the editor, and go online");
        widgets_.remember("simulate");
        ImGui::SameLine();
        if (ImGui::Button("Connect")) {
            connect(static_cast<std::uint16_t>(port_));
        }
        ImGui::SetItemTooltip("Go online with a soft_plc started with --monitor=%d:\n"
                              "Upload takes its program, Download sends this project",
                              port_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0F);
        if (ImGui::InputInt("port", &port_, 0)) {
            port_ = std::clamp(port_, 1, 65535);
        }
        return;
    }
    const auto status = session_->status();
    const ImVec4 color = !status.connected ? kProblem : status.running ? kOnline : kStop;
    ImGui::TextColored(color, "%s",
                       !status.connected ? "OFFLINE: connection lost"
                       : status.running  ? "ONLINE  RUN"
                                         : "ONLINE  STOP");
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", simulator_ ? "simulator" : fmt::format("soft_plc on port {}", port_).c_str());
    ImGui::SameLine();
    if (ImGui::Button("Go offline")) {
        goOffline();
        return;
    }
    widgets_.remember("go offline");
    if (!status.connected && (uploadPending_ || pendingDownload_)) {
        status_ = uploadPending_ ? "Upload failed: connection lost" : "Download failed: connection lost";
        uploadPending_ = false;
        pendingDownload_.reset();
    }
    checkDownload();
    checkUpload();
    if (status.connected && !simulator_) {
        ImGui::SameLine();
        ImGui::BeginDisabled(uploadPending_ || pendingDownload_.has_value());
        if (ImGui::Button("Upload")) {
            upload();
        }
        ImGui::SetItemTooltip("Replace the project with the program the PLC runs: one ST block per PROGRAM, "
                              "FUNCTION_BLOCK, FUNCTION and TYPE");
        widgets_.remember("upload");
        ImGui::EndDisabled();
    }
    if (status.connected && (!downloaded_ || *downloaded_ != project_)) {
        ImGui::SameLine();
        ImGui::TextColored(kStop, "%s",
                           downloaded_ ? "The PLC runs an older version."
                                       : "The PLC runs a version this editor didn't load.");
        ImGui::SameLine();
        ImGui::BeginDisabled(pendingDownload_.has_value() || uploadPending_);
        if (ImGui::Button("Download")) {
            download();
        }
        ImGui::SetItemTooltip("Load the project as it is now into the running PLC, between two cycles. Variables "
                              "that are still there keep their values; new ones start at their initial value.");
        widgets_.remember("download");
        ImGui::EndDisabled();
    }
    if (simulator_) {
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            simulate();
            return;
        }
        ImGui::SetItemTooltip("Restart the simulator with the project as it is now: every value starts over");
        widgets_.remember("restart");
    }
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", status.details.c_str());
    if (!status.lastError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kProblem, "%s", status.lastError.c_str());
    }
}

bool ProjectWindow::simulate() {
    if (!checked_ || *checked_ != project_) {
        checkCode();
    }
    if (check_.error) {
        status_ = "soft_plc can't compile the project yet: fix the errors first.";
        return false;
    }
    goOffline();
    auto started = online::Simulator::start(check_.translation.text);
    if (!started) {
        status_ = started.error();
        return false;
    }
    auto connected = online::Session::connect((*started)->port());
    if (!connected) {
        status_ = connected.error();
        return false;
    }
    simulator_ = std::move(*started);
    session_ = std::move(*connected);
    downloaded_ = project_;
    status_.clear();
    return true;
}

bool ProjectWindow::connect(std::uint16_t port) {
    goOffline();
    auto connected = online::Session::connect(port);
    if (!connected) {
        status_ = fmt::format("No soft_plc on port {}: {}", port, connected.error());
        return false;
    }
    session_ = std::move(*connected);
    port_ = port;
    status_.clear();
    return true;
}

bool ProjectWindow::download() {
    if (!session_) {
        return false;
    }
    if (!checked_ || *checked_ != project_) {
        checkCode();
    }
    if (check_.error) {
        status_ = "soft_plc can't compile the project yet: fix the errors first.";
        return false;
    }
    if (uploadPending_ || pendingDownload_) {
        return false; // one at a time
    }
    session_->load(check_.translation.text);
    pendingDownload_ = project_;
    return true;
}

bool ProjectWindow::upload() {
    if (!session_ || simulator_ || uploadPending_ || pendingDownload_) {
        return false;
    }
    if (modified()) {
        askUpload_ = true;
        return false;
    }
    startUpload();
    return true;
}

void ProjectWindow::startUpload() {
    session_->requestSource();
    uploadPending_ = true;
}

// An upload's answer, once it's there.
void ProjectWindow::checkUpload() {
    if (!uploadPending_ || !session_) {
        return;
    }
    const auto result = session_->lastSource();
    if (!result || result->number <= uploadsSeen_) {
        return;
    }
    uploadsSeen_ = result->number;
    uploadPending_ = false;
    if (!result->ok) {
        status_ = "Upload failed: " + result->message;
        return;
    }
    auto uploaded = projectFromSt(result->source);
    if (!uploaded) {
        status_ = "Upload failed: " + uploaded.error();
        return;
    }
    replaceProject(std::move(*uploaded));
    downloaded_ = project_; // the PLC runs exactly this
    status_.clear();
    note_ = fmt::format("Uploaded {} blocks from the PLC.", project_.blocks.size());
}

void ProjectWindow::replaceProject(Project project) {
    project_ = std::move(project);
    folder_.clear();
    makeEditors();
    std::fill(savedSources_.begin(), savedSources_.end(), std::string()); // never saved: every block is new
    savedWatch_ = project_.watch;
    structureModified_ = true;
    current_ = 0;
    for (std::size_t i = 0; i < project_.blocks.size(); ++i) {
        if (project_.blocks[i].kind == BlockKind::Program) {
            current_ = i;
        }
    }
    showWatch_ = false;
    instances_.clear();
    instance_.reset();
    checked_.reset();
}

void ProjectWindow::drawUploadPopup() {
    if (askUpload_) {
        ImGui::OpenPopup("Replace the project?");
        askUpload_ = false;
    }
    if (!ImGui::BeginPopupModal("Replace the project?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("The project has changes that aren't saved. Upload replaces it with the PLC's program.");
    if (ImGui::Button("Upload anyway")) {
        if (session_ && !uploadPending_ && !pendingDownload_) {
            startUpload();
        }
        ImGui::CloseCurrentPopup();
    }
    widgets_.remember("upload anyway");
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    widgets_.remember("upload cancel");
    ImGui::EndPopup();
}

// A download's answer, once it's there.
void ProjectWindow::checkDownload() {
    if (!pendingDownload_ || !session_) {
        return;
    }
    const auto result = session_->lastLoad();
    if (!result || result->number <= loadsSeen_) {
        return;
    }
    loadsSeen_ = result->number;
    if (result->ok) {
        downloaded_ = std::move(*pendingDownload_);
        status_.clear();
        std::string startOver;
        for (const auto& path : result->initialized) {
            startOver += (startOver.empty() ? "" : ", ") + path;
        }
        note_ = fmt::format("Downloaded: {} variables kept their values{}", result->kept,
                            startOver.empty() ? "." : "; these start at their initial value: " + startOver);
    } else {
        status_ = "Download refused: " + result->message;
    }
    pendingDownload_.reset();
}

void ProjectWindow::goOffline() {
    session_.reset();
    simulator_.reset();
    downloaded_.reset();
    pendingDownload_.reset();
    loadsSeen_ = 0;
    uploadPending_ = false;
    uploadsSeen_ = 0;
    watching_.clear();
}

void ProjectWindow::updateWatch(const std::vector<std::string>& paths) {
    if (session_ && paths != watching_) {
        session_->watch(paths);
        watching_ = paths;
    }
}

std::optional<std::string> ProjectWindow::chooseInstance(const Block& block) {
    switch (block.kind) {
    case BlockKind::Program:
        return std::string();
    case BlockKind::Types:
        return std::nullopt;
    case BlockKind::Function:
        ImGui::TextColored(kDim, "Online: a FUNCTION keeps nothing between calls, so there are no values to watch.");
        return std::nullopt;
    case BlockKind::FunctionBlock:
        break;
    }
    const auto found = check_.instances.find(upper(block.name));
    if (found == check_.instances.end() || found->second.empty()) {
        ImGui::TextColored(kDim, "Online: the PROGRAM has no instance of %s, so there are no values to watch.",
                           block.name.c_str());
        return std::nullopt;
    }
    const auto& paths = found->second;
    auto& chosen = instances_[upper(block.name)];
    if (std::find(paths.begin(), paths.end(), chosen) == paths.end()) {
        chosen = paths.front();
    }
    ImGui::SetNextItemWidth(200.0F);
    if (ImGui::BeginCombo("Instance", chosen.c_str())) {
        for (const auto& path : paths) {
            if (ImGui::Selectable(path.c_str(), path == chosen)) {
                chosen = path;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The instance whose values are shown: each instance has its own memory");
    widgets_.remember("instance");
    return chosen;
}

// ---------------------------------------------------------------- Popups

void ProjectWindow::drawNewBlockPopup() {
    if (openNewBlock_) {
        ImGui::OpenPopup("New block");
        openNewBlock_ = false;
        newName_.clear();
    }
    if (!ImGui::BeginPopup("New block")) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(200.0F);
    ImGui::InputTextWithHint("Name", "e.g. Conveyor", &newName_);
    ImGui::SetNextItemWidth(200.0F);
    if (ImGui::BeginCombo("Kind", kindName(kKinds[newKind_]))) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(kindName(kKinds[i]), i == newKind_)) {
                newKind_ = i;
            }
        }
        ImGui::EndCombo();
    }
    const bool typesBlock = kKinds[newKind_] == BlockKind::Types;
    if (typesBlock) {
        newLanguage_ = 1; // TYPE blocks are always ST
    }
    ImGui::BeginDisabled(typesBlock);
    ImGui::RadioButton("KOP", &newLanguage_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ST", &newLanguage_, 1);
    ImGui::EndDisabled();

    const bool valid = isIdentifier(newName_) && !nameTaken(newName_, project_.blocks.size());
    if (!newName_.empty() && !valid) {
        ImGui::TextColored(kProblem, isIdentifier(newName_) ? "That name is taken." : "Use letters, digits and _.");
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Create") || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        addBlock(newBlock(newName_, kKinds[newKind_], newLanguage_ == 0 ? Language::Kop : Language::St));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectWindow::drawRenamePopup() {
    if (openRename_ && !project_.blocks.empty()) {
        ImGui::OpenPopup("Rename block");
        newName_ = project_.blocks[current_].name;
    }
    openRename_ = false;
    if (!ImGui::BeginPopup("Rename block")) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(200.0F);
    ImGui::InputText("New name", &newName_);
    auto& block = project_.blocks[current_];
    const bool valid = isIdentifier(newName_) && !nameTaken(newName_, current_);
    if (block.language == Language::St) {
        ImGui::TextColored(kDim, "Rename it in the ST text too.");
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Rename") || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        if (block.name != newName_) {
            block.name = newName_;
            structureModified_ = true;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectWindow::drawDeletePopup() {
    if (openDelete_ && !project_.blocks.empty()) {
        ImGui::OpenPopup("Delete block");
    }
    openDelete_ = false;
    if (!ImGui::BeginPopupModal("Delete block", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::Text("Delete block %s? This can't be undone.", project_.blocks[current_].name.c_str());
    if (ImGui::Button("Delete") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        deleteBlock(current_);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectWindow::drawClosePopup() {
    if (askClose_) {
        ImGui::OpenPopup("Unsaved changes");
        askClose_ = false;
    }
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("The project has changes that aren't saved.");
    if (ImGui::Button("Save and quit")) {
        quitAfterSave_ = folder_.empty(); // the Save project popup quits once it has saved
        closeConfirmed_ = save();
        ImGui::CloseCurrentPopup();
    }
    widgets_.remember("save and quit");
    ImGui::SameLine();
    if (ImGui::Button("Quit without saving")) {
        closeConfirmed_ = true;
        ImGui::CloseCurrentPopup();
    }
    widgets_.remember("quit without saving");
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Where to save a project that has no folder yet.
void ProjectWindow::drawSavePopup() {
    if (openSave_) {
        ImGui::OpenPopup("Save project");
        saveFolder_ = (std::filesystem::current_path() / project_.name).string();
        openSave_ = false;
    }
    if (!ImGui::BeginPopupModal("Save project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(500.0F);
    ImGui::InputTextWithHint("Folder", "e.g. /home/me/plc/conveyor", &saveFolder_);
    widgets_.remember("save folder");
    std::error_code error;
    std::filesystem::path folder(saveFolder_);
    if (folder.filename().empty()) {
        folder = folder.parent_path(); // typed with a trailing /
    }
    const bool taken = !saveFolder_.empty() && std::filesystem::exists(folder / "project.json", error);
    if (taken) {
        ImGui::TextColored(kStop, "That folder already holds a project: saving replaces it.");
        widgets_.remember("overwrite warning");
    }
    if (!status_.empty()) {
        ImGui::TextColored(kProblem, "%s", status_.c_str());
    }
    const bool valid = !folder.filename().empty();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(taken ? "Overwrite" : "Save") || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        // The project takes the folder's name, as when ladder_editor opens a new folder.
        const auto previousName = project_.name;
        project_.name = folder.filename().string();
        folder_ = folder;
        if (save()) {
            // Online, the PLC still runs this project: only its name changed.
            for (auto* running : {&downloaded_, &pendingDownload_}) {
                if (*running) {
                    (*running)->name = project_.name;
                }
            }
            closeConfirmed_ = quitAfterSave_;
            quitAfterSave_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            project_.name = previousName;
            folder_.clear();
        }
    }
    widgets_.remember("save here");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        quitAfterSave_ = false;
        ImGui::CloseCurrentPopup();
    }
    widgets_.remember("save cancel");
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Actions

bool ProjectWindow::save() {
    if (folder_.empty()) {
        openSave_ = true;
        return false;
    }
    auto saved = saveProject(project_, folder_);
    if (!saved) {
        status_ = saved.error();
        return false;
    }
    for (std::size_t i = 0; i < project_.blocks.size(); ++i) {
        if (editors_[i]) {
            editors_[i]->markSaved();
        }
        savedSources_[i] = project_.blocks[i].source;
    }
    savedWatch_ = project_.watch;
    structureModified_ = false;
    status_.clear();
    return true;
}

void ProjectWindow::addBlock(Block block) {
    editors_.push_back(block.language == Language::Kop ? std::make_unique<KopEditor>() : nullptr);
    codeEditors_.push_back(codeEditorFor(block));
    coloredVersion_.push_back(static_cast<std::size_t>(-1));
    savedSources_.emplace_back(); // never saved: any text counts as a change
    project_.blocks.push_back(std::move(block));
    current_ = project_.blocks.size() - 1;
    structureModified_ = true;
}

void ProjectWindow::deleteBlock(std::size_t index) {
    if (index >= project_.blocks.size()) {
        return;
    }
    project_.blocks.erase(project_.blocks.begin() + static_cast<std::ptrdiff_t>(index));
    editors_.erase(editors_.begin() + static_cast<std::ptrdiff_t>(index));
    codeEditors_.erase(codeEditors_.begin() + static_cast<std::ptrdiff_t>(index));
    coloredVersion_.erase(coloredVersion_.begin() + static_cast<std::ptrdiff_t>(index));
    savedSources_.erase(savedSources_.begin() + static_cast<std::ptrdiff_t>(index));
    current_ = project_.blocks.empty() ? 0 : std::min(index, project_.blocks.size() - 1);
    structureModified_ = true;
}

bool ProjectWindow::nameTaken(const std::string& name, std::size_t except) const {
    for (std::size_t i = 0; i < project_.blocks.size(); ++i) {
        if (i != except && upper(project_.blocks[i].name) == upper(name)) {
            return true;
        }
    }
    return false;
}

} // namespace app
