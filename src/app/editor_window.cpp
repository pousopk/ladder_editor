#include "app/editor_window.hpp"
#include "ladder/boxes.hpp"
#include "ladder/check.hpp"
#include "ladder/notation.hpp"
#include "ladder/rung_file.hpp"
#include "view/rung_view.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace app {

namespace {

using namespace ladder;

const ImVec4 kComment{0.55F, 0.75F, 0.55F, 1.0F};
const ImVec4 kProblem{0.95F, 0.45F, 0.40F, 1.0F};
const ImVec4 kDim{0.6F, 0.6F, 0.65F, 1.0F};

constexpr const char* kEditPopup = "Edit element";

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

// Every box instance name used in a series, recursively.
void collectInstances(const Series& series, std::vector<std::string>& names) {
    for (const auto& element : series.elements) {
        if (const auto* box = std::get_if<Box>(&element)) {
            names.push_back(lower(box->instance));
        } else if (const auto* parallel = std::get_if<Parallel>(&element)) {
            for (const auto& side : parallel->branches) {
                collectInstances(side, names);
            }
        }
    }
}

// An input or output pin's operand, as a string ImGui can edit; editing it
// to empty disconnects the pin.
void pinField(const char* label, std::string_view pinName, std::vector<Pin>& pins) {
    auto it = std::find_if(pins.begin(), pins.end(), [&](const Pin& pin) { return lower(pin.name) == lower(pinName); });
    std::string operand = it == pins.end() ? std::string() : it->operand;
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::InputTextWithHint(label, "not connected", &operand)) {
        if (operand.empty()) {
            if (it != pins.end()) {
                pins.erase(it);
            }
        } else if (it == pins.end()) {
            pins.push_back(Pin{std::string(pinName), operand});
        } else {
            it->operand = operand;
        }
    }
}

// What an element is called in the Undo button: "edit contact", "insert box".
std::string kindOf(const Element& element) {
    if (std::holds_alternative<Contact>(element))
        return "contact";
    if (std::holds_alternative<Box>(element))
        return "box";
    return "branch";
}

} // namespace

EditorWindow::EditorWindow(std::string path, std::vector<Rung> rungs)
    : path_(std::move(path)), rungs_(std::move(rungs)) {}

void EditorWindow::draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ladder_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    // A popup closed by a click elsewhere is already closed when the frame
    // starts: record its edit before that click does anything else.
    if (pending_ && !ImGui::IsPopupOpen(kEditPopup)) {
        finishPendingEdit();
    }
    drawToolbar();
    ImGui::Separator();
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 7.0F;
    ImGui::BeginChild("networks", ImVec2(0, -detailsHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawNetworks();
    ImGui::EndChild();
    ImGui::Separator();
    drawDetails();
    handleShortcuts(); // before the popups: the Enter that closes one mustn't open it again
    drawEditPopup();
    drawBoxPopup();
    ImGui::End();
}

void EditorWindow::drawToolbar() {
    ImGui::TextUnformatted(path_.empty() ? "built-in example (not saved)" : path_.c_str());
    if (modified()) {
        ImGui::SameLine(0, 2);
        ImGui::TextUnformatted("*");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(path_.empty());
    if (ImGui::Button("Save")) {
        save();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.canUndo());
    if (ImGui::Button("Undo")) {
        undo();
    }
    ImGui::SetItemTooltip("Undo %s (Ctrl+Z)", history_.undoDescription().c_str());
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.canRedo());
    if (ImGui::Button("Redo")) {
        redo();
    }
    ImGui::SetItemTooltip("Redo %s (Ctrl+Y)", history_.redoDescription().c_str());
    ImGui::EndDisabled();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0F);
    ImGui::SetNextItemWidth(160.0F);
    ImGui::SliderFloat("Zoom", &zoom_, 0.6F, 2.0F, "%.1fx");

    // The edit buttons, with their keyboard shortcuts.
    const auto button = [](const char* label, const char* tooltip) {
        const bool pressed = ImGui::Button(label);
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
        return pressed;
    };
    if (button("-| |-", "Insert a normally open contact after the selection (C)"))
        insertElement(Contact{});
    if (button("-|/|-", "Insert a normally closed contact (N)"))
        insertElement(Contact{ContactKind::NormallyClosed, ""});
    if (button("[Box]", "Insert a box: a timer, counter, edge or flip-flop (B)"))
        openBoxes_ = true;
    if (button("Branch", "Put a parallel branch around the selected element (P)"))
        addBranch();
    if (button("+Side", "Add a side to the branch the selected element is on (Shift+P)"))
        addSide();
    if (button("-( )-", "Add a coil (O)"))
        addCoil();
    if (button("<", "Move the selection left (Ctrl+Left)"))
        moveSelected(-1);
    if (button(">", "Move the selection right (Ctrl+Right)"))
        moveSelected(+1);
    if (button("Edit", "Edit the selected element (Enter, or double-click)"))
        openEditor_ = selection_ && selection_->item;
    if (button("Delete", "Delete the selected element, or the network if only the network is selected (Del)")) {
        deleteSelected();
    }
    if (button("+Network", "Add a network after the selected one (Ctrl+N)"))
        addNetwork();
    if (!status_.empty()) {
        ImGui::TextColored(kProblem, "%s", status_.c_str());
    }
    ImGui::NewLine();
}

void EditorWindow::drawNetworks() {
    // Text scales with the drawing, so labels stay where the symbols are.
    ImGui::SetWindowFontScale(zoom_);
    origins_.assign(rungs_.size(), ImVec2{-1.0F, -1.0F});
    if (rungs_.empty()) {
        ImGui::TextColored(kDim, "No networks yet: press Ctrl+N or click +Network to add one.");
    }
    std::optional<std::size_t> deleteRequest;
    std::optional<std::pair<std::size_t, int>> moveRequest;
    for (std::size_t i = 0; i < rungs_.size(); ++i) {
        const auto& rung = rungs_[i];
        ImGui::PushID(static_cast<int>(i));
        const bool networkSelected = selection_ && selection_->rung == i;
        if (ImGui::Selectable(fmt::format("Network {}", i + 1).c_str(), networkSelected && !selection_->item,
                              ImGuiSelectableFlags_None, ImGui::CalcTextSize("Network 000"))) {
            select(i, std::nullopt);
        }
        if (!rung.comment.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kComment, "%s", rung.comment.c_str());
        }
        if (networkSelected) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Up"))
                moveRequest = std::pair{i, -1};
            ImGui::SameLine();
            if (ImGui::SmallButton("Down"))
                moveRequest = std::pair{i, +1};
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete network"))
                deleteRequest = i;
        }

        const std::optional<ItemPath> selected = networkSelected ? selection_->item : std::nullopt;
        const auto layout = layOut(rung); // cheap: recomputed every frame, so it's never stale
        origins_[i] = ImGui::GetCursorScreenPos();
        const auto shown = view::drawRung(rung, layout, static_cast<int>(i), selected, zoom_);
        if (shown.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            select(i, *shown.hovered);
        }
        if (shown.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            select(i, *shown.hovered, true);
        }
        if (shown.overRung && !shown.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            select(i, std::nullopt); // a click between the elements selects the network
        }
        for (const auto& problem : check(rung)) {
            ImGui::TextColored(kProblem, "  %s", problem.message.c_str());
        }
        ImGui::PopID();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::SetWindowFontScale(1.0F);
    // Changes to the list itself wait until it's no longer being drawn.
    if (deleteRequest) {
        deleteNetwork(*deleteRequest);
    }
    if (moveRequest) {
        moveNetwork(moveRequest->first, moveRequest->second);
    }
}

void EditorWindow::drawDetails() {
    commentField_.reset();
    if (!selection_ || selection_->rung >= rungs_.size()) {
        ImGui::TextColored(kDim, "Click an element or a network title to select it. Hover the buttons for shortcuts.");
        return;
    }
    auto& rung = rungs_[selection_->rung];
    ImGui::Text("Network %zu", selection_->rung + 1);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "   %s", formatRung(rung).c_str());

    // The comment is edited in place; the whole edit becomes one step when
    // the field is left.
    ImGui::SetNextItemWidth(-1.0F);
    const auto before = rung;
    ImGui::InputTextWithHint("##comment", "comment: why this network exists", &rung.comment);
    commentField_ = ImVec2{(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2.0F,
                           (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2.0F};
    if (ImGui::IsItemActivated()) {
        commentEdit_ = PendingEdit{selection_->rung, before, selection_, "edit comment", false};
    }
    if (ImGui::IsItemDeactivated() && commentEdit_) {
        if (rungs_[commentEdit_->rung] != commentEdit_->before) {
            auto command = std::make_unique<ReplaceRung>(commentEdit_->rung, commentEdit_->before,
                                                         rungs_[commentEdit_->rung], commentEdit_->what);
            command->selectionBefore = commentEdit_->selection;
            command->selectionAfter = commentEdit_->selection;
            history_.record(std::move(command));
        }
        commentEdit_.reset();
    }

    if (!selection_->item) {
        return;
    }
    if (const auto* coilPath = std::get_if<CoilPath>(&*selection_->item)) {
        const auto& coil = rung.coils[coilPath->index];
        ImGui::Text("Coil %s", coil.operand.empty() ? "(no operand)" : coil.operand.c_str());
    } else if (const auto* element = elementAt(rung, std::get<ElementPath>(*selection_->item))) {
        ImGui::Text("Selected: %s", formatElement(*element).c_str());
    }
}

void EditorWindow::drawEditPopup() {
    if (openEditor_) {
        ImGui::OpenPopup(kEditPopup);
        openEditor_ = false;
    }
    if (!ImGui::BeginPopup(kEditPopup)) {
        return;
    }
    if (!selection_ || !selection_->item || selection_->rung >= rungs_.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& rung = rungs_[selection_->rung];
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing) {
        // The popup's edits are made in place; remember the rung as it was,
        // to record one step when the popup closes.
        pending_ = PendingEdit{selection_->rung, rung, selection_, "edit", mergeNextEdit_};
        mergeNextEdit_ = false;
    }
    const auto operandField = [&](std::string& operand) {
        if (appearing) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputTextWithHint("Operand", "a variable, t1.Q or %IX0.0", &operand,
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            ImGui::CloseCurrentPopup(); // Enter: done
        }
    };

    if (const auto* coilPath = std::get_if<CoilPath>(&*selection_->item)) {
        auto& coil = rung.coils[coilPath->index];
        if (appearing)
            pending_->what = "edit coil";
        int kind = static_cast<int>(coil.kind);
        ImGui::TextUnformatted("Coil");
        bool radio = ImGui::RadioButton("( )", &kind, 0);
        ImGui::SameLine();
        radio |= ImGui::RadioButton("(/)", &kind, 1);
        ImGui::SameLine();
        radio |= ImGui::RadioButton("(S)", &kind, 2);
        ImGui::SameLine();
        radio |= ImGui::RadioButton("(R)", &kind, 3);
        if (radio) {
            coil.kind = static_cast<CoilKind>(kind);
        }
        operandField(coil.operand);
    } else if (auto* element = elementAt(rung, std::get<ElementPath>(*selection_->item))) {
        if (appearing)
            pending_->what = "edit " + kindOf(*element);
        if (auto* contact = std::get_if<Contact>(element)) {
            int kind = static_cast<int>(contact->kind);
            ImGui::TextUnformatted("Contact");
            bool radio = ImGui::RadioButton("-| |-  normally open", &kind, 0);
            ImGui::SameLine();
            radio |= ImGui::RadioButton("-|/|-  normally closed", &kind, 1);
            if (radio) {
                contact->kind = static_cast<ContactKind>(kind);
            }
            operandField(contact->operand);
        } else if (auto* box = std::get_if<Box>(element)) {
            ImGui::SetNextItemWidth(160.0F);
            if (ImGui::BeginCombo("Type", box->type.c_str())) {
                for (const auto& type : standardBoxTypes()) {
                    if (ImGui::Selectable(std::string(type.name).c_str(), type.name == box->type)) {
                        box->type = type.name;
                    }
                }
                ImGui::EndCombo();
            }
            if (appearing) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(160.0F);
            ImGui::InputText("Instance", &box->instance);
            if (const auto* type = findBoxType(box->type)) {
                for (const auto pin : type->inputs) {
                    pinField(fmt::format("{} :=", pin).c_str(), pin, box->inputs);
                }
                for (const auto pin : type->outputs) {
                    pinField(fmt::format("{} =>", pin).c_str(), pin, box->outputs);
                }
            }
        }
    }
    if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorWindow::finishPendingEdit() {
    auto edit = std::move(*pending_);
    pending_.reset();
    if (edit.rung >= rungs_.size() || rungs_[edit.rung] == edit.before) {
        return; // nothing changed
    }
    auto command = std::make_unique<ReplaceRung>(edit.rung, edit.before, rungs_[edit.rung], edit.what);
    command->selectionBefore = edit.selection;
    command->selectionAfter = edit.selection;
    history_.record(std::move(command), edit.mergeWithLast);
}

void EditorWindow::drawBoxPopup() {
    if (openBoxes_) {
        ImGui::OpenPopup("Insert box");
        openBoxes_ = false;
        boxChoice_ = 0;
    }
    if (!ImGui::BeginPopup("Insert box")) {
        return;
    }
    // Arrow keys and Enter work as well as the mouse: B, Down, Enter.
    const auto& types = standardBoxTypes();
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        boxChoice_ = (boxChoice_ + 1) % types.size();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        boxChoice_ = (boxChoice_ + types.size() - 1) % types.size();
    }
    std::optional<std::size_t> chosen;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        chosen = boxChoice_;
    }
    ImGui::TextColored(kDim, "Insert a box");
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (ImGui::Selectable(std::string(types[i].name).c_str(), i == boxChoice_)) {
            chosen = i;
        }
    }
    if (chosen) {
        ImGui::CloseCurrentPopup();
        insertElement(Box{std::string(types[*chosen].name), newInstanceName(types[*chosen].name), {}, {}});
    }
    ImGui::EndPopup();
}

void EditorWindow::handleShortcuts() {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return; // keys belong to the text field or popup being used
    }
    const bool ctrl = io.KeyCtrl;
    if (ctrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z))
            io.KeyShift ? redo() : undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y))
            redo();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            moveSelected(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            moveSelected(+1);
        if (ImGui::IsKeyPressed(ImGuiKey_N))
            addNetwork();
        if (ImGui::IsKeyPressed(ImGuiKey_S))
            save();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C))
        insertElement(Contact{});
    if (ImGui::IsKeyPressed(ImGuiKey_N))
        insertElement(Contact{ContactKind::NormallyClosed, ""});
    if (ImGui::IsKeyPressed(ImGuiKey_B))
        openBoxes_ = true;
    if (ImGui::IsKeyPressed(ImGuiKey_P))
        io.KeyShift ? addSide() : addBranch();
    if (ImGui::IsKeyPressed(ImGuiKey_O))
        addCoil();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        deleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selection_ && selection_->item)
        openEditor_ = true;
}

// ---------------------------------------------------------------- Actions

void EditorWindow::select(std::size_t rung, std::optional<ItemPath> item, bool edit) {
    selection_ = Selection{rung, std::move(item)};
    openEditor_ = edit && selection_->item.has_value();
    mergeNextEdit_ = false;
    status_.clear();
}

template <class Change> void EditorWindow::editRung(std::size_t index, std::string what, Change change, bool edit) {
    Rung after = rungs_[index];
    EditResult result = change(after);
    if (!result) {
        report(result.error());
        return;
    }
    auto command = std::make_unique<ReplaceRung>(index, rungs_[index], std::move(after), std::move(what));
    command->selectionBefore = selection_;
    command->selectionAfter = Selection{index, *result};
    history_.perform(std::move(command), rungs_);
    select(index, *result, edit);
    mergeNextEdit_ = edit; // the operand typed next belongs to this step
}

// Turns an edit operation's result (an element or coil path, or an error)
// into what editRung expects.
namespace {

using ItemResult = tl::expected<std::optional<ItemPath>, std::string>;

template <class Path> ItemResult toItem(const tl::expected<Path, std::string>& result) {
    if (!result) {
        return tl::unexpected(result.error());
    }
    return std::optional<ItemPath>(*result);
}

template <class Path> ItemResult toItem(const tl::expected<std::optional<Path>, std::string>& result) {
    if (!result) {
        return tl::unexpected(result.error());
    }
    return *result ? std::optional<ItemPath>(**result) : std::nullopt;
}

} // namespace

void EditorWindow::insertElement(Element element) {
    if (!selection_) {
        report("Select an element or a network first: new elements go after the selection.");
        return;
    }
    const auto& rung = rungs_[selection_->rung];
    // After the selected element; at the end of the logic for a coil or a whole network.
    InsertPoint point{{}, rung.logic.elements.size()};
    if (selection_->item) {
        if (const auto* path = std::get_if<ElementPath>(&*selection_->item)) {
            point = after(*path);
        }
    }
    const auto what = "insert " + kindOf(element);
    editRung(
        selection_->rung, what, [&](Rung& r) { return toItem(ladder::insertElement(r, point, std::move(element))); },
        true);
}

void EditorWindow::addBranch() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select a contact or box to put a branch around.");
        return;
    }
    const auto target = *path;
    editRung(selection_->rung, "add branch", [&](Rung& r) { return toItem(addParallel(r, target, Contact{})); }, true);
}

void EditorWindow::addSide() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element on a branch to add a side to that branch.");
        return;
    }
    const auto target = *path;
    editRung(
        selection_->rung, "add side", [&](Rung& r) { return toItem(ladder::addSide(r, target, Contact{})); }, true);
}

void EditorWindow::addCoil() {
    if (!selection_) {
        report("Select a network first.");
        return;
    }
    std::size_t index = rungs_[selection_->rung].coils.size();
    if (selection_->item) {
        if (const auto* coil = std::get_if<CoilPath>(&*selection_->item)) {
            index = coil->index + 1;
        }
    }
    editRung(selection_->rung, "add coil", [&](Rung& r) { return toItem(insertCoil(r, index, Coil{})); }, true);
}

void EditorWindow::deleteSelected() {
    if (!selection_) {
        return;
    }
    if (!selection_->item) {
        deleteNetwork(selection_->rung);
        return;
    }
    const auto item = *selection_->item;
    if (const auto* coil = std::get_if<CoilPath>(&item)) {
        editRung(selection_->rung, "delete coil", [&](Rung& r) { return toItem(removeCoil(r, *coil)); });
        return;
    }
    const auto& path = std::get<ElementPath>(item);
    const auto* element = elementAt(rungs_[selection_->rung], path);
    const auto what = "delete " + (element != nullptr ? kindOf(*element) : std::string("element"));
    editRung(selection_->rung, what, [&](Rung& r) { return toItem(removeElement(r, path)); });
}

void EditorWindow::moveSelected(int direction) {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element to move it.");
        return;
    }
    const auto target = *path;
    editRung(selection_->rung, "move element", [&](Rung& r) { return toItem(moveElement(r, target, direction)); });
}

void EditorWindow::addNetwork() {
    const auto index = selection_ ? selection_->rung + 1 : rungs_.size();
    Rung rung;
    rung.coils.push_back(Coil{});
    auto command = std::make_unique<InsertNetwork>(index, std::move(rung));
    command->selectionBefore = selection_;
    command->selectionAfter = Selection{index, CoilPath{0}};
    history_.perform(std::move(command), rungs_);
    select(index, CoilPath{0}, true);
    mergeNextEdit_ = true; // the coil's operand, typed next, is part of adding the network
}

void EditorWindow::deleteNetwork(std::size_t index) {
    if (index >= rungs_.size()) {
        return;
    }
    auto command = std::make_unique<RemoveNetwork>(index, rungs_[index]);
    command->selectionBefore = Selection{index, std::nullopt};
    if (rungs_.size() > 1) {
        command->selectionAfter = Selection{std::min(index, rungs_.size() - 2), std::nullopt};
    }
    const auto after = command->selectionAfter;
    history_.perform(std::move(command), rungs_);
    selection_ = after;
    status_.clear();
}

void EditorWindow::moveNetwork(std::size_t index, int direction) {
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= rungs_.size())) {
        return;
    }
    const auto other = direction < 0 ? index - 1 : index + 1;
    auto command = std::make_unique<SwapNetworks>(index, other);
    command->selectionBefore = Selection{index, std::nullopt};
    command->selectionAfter = Selection{other, std::nullopt};
    history_.perform(std::move(command), rungs_);
    select(other, std::nullopt);
}

void EditorWindow::undo() {
    if (!history_.canUndo()) {
        return;
    }
    selection_ = history_.undo(rungs_);
    if (selection_ && selection_->rung >= rungs_.size()) {
        selection_.reset();
    }
    status_.clear();
}

void EditorWindow::redo() {
    if (!history_.canRedo()) {
        return;
    }
    selection_ = history_.redo(rungs_);
    if (selection_ && selection_->rung >= rungs_.size()) {
        selection_.reset();
    }
    status_.clear();
}

void EditorWindow::save() {
    if (path_.empty()) {
        report("This is the built-in example: start ladder_editor with a file name to save.");
        return;
    }
    std::ofstream file(path_);
    file << formatRungFile(rungs_);
    if (!file) {
        report(fmt::format("Couldn't write {}", path_));
        return;
    }
    history_.markSaved();
    status_.clear();
}

std::optional<ImVec2> EditorWindow::rungOrigin(std::size_t index) const {
    if (index >= origins_.size() || origins_[index].x < 0.0F) {
        return std::nullopt;
    }
    return origins_[index];
}

std::string EditorWindow::newInstanceName(std::string_view type) const {
    std::vector<std::string> used;
    for (const auto& rung : rungs_) {
        collectInstances(rung.logic, used);
    }
    const auto base = lower(type);
    for (int n = 1;; ++n) {
        auto name = fmt::format("{}{}", base, n);
        if (std::find(used.begin(), used.end(), name) == used.end()) {
            return name;
        }
    }
}

} // namespace app
