#include "app/project_window.hpp"
#include "app/st_colors.hpp"
#include "ladder/project_file.hpp"
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
    if (structureModified_) {
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
    ImGui::End();
}

void ProjectWindow::drawHeader() {
    ImGui::Text("%s", project_.name.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", folder_.empty() ? "(built-in example, not saved)" : folder_.string().c_str());
    if (modified()) {
        ImGui::SameLine(0, 2);
        ImGui::TextUnformatted("*");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(folder_.empty());
    if (ImGui::Button("Save")) {
        save();
    }
    ImGui::SetItemTooltip("Save every block of the project (Ctrl+S)");
    ImGui::SameLine();
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
        if (ImGui::Selectable("##block", current_ == i, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0, ImGui::GetTextLineHeight()))) {
            current_ = i;
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
    if (auto* editor = editors_[current_].get()) {
        app::KopContext context;
        context.boxes = &check_.boxes;
        const auto& parts = check_.translation.parts;
        if (current_ < parts.size() && parts[current_].block == current_) {
            context.translation = &parts[current_].translation;
        }
        if (check_.error && check_.error->block == current_) {
            context.error = check_.error;
        }
        editor->draw(block, context);
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
    ImGui::BeginDisabled(folder_.empty());
    if (ImGui::Button("Save and quit")) {
        closeConfirmed_ = save();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
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

// ---------------------------------------------------------------- Actions

bool ProjectWindow::save() {
    if (folder_.empty()) {
        status_ = "This is the built-in example: start ladder_editor with a folder to save a project.";
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
