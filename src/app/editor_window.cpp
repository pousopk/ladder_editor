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

} // namespace

EditorWindow::EditorWindow(std::string path, std::vector<Rung> rungs)
    : path_(std::move(path)), rungs_(std::move(rungs)) {}

void EditorWindow::draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ladder_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
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
    if (modified_) {
        ImGui::SameLine(0, 2);
        ImGui::TextUnformatted("*");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(path_.empty());
    if (ImGui::Button("Save")) {
        save();
    }
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
    if (button("Delete", "Delete the selected element, or the network if only the network is selected (Del)"))
        deleteSelected();
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
        auto& rung = rungs_[i];
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
    if (!selection_ || selection_->rung >= rungs_.size()) {
        ImGui::TextColored(kDim, "Click an element or a network title to select it. Hover the buttons for shortcuts.");
        return;
    }
    auto& rung = rungs_[selection_->rung];
    ImGui::Text("Network %zu", selection_->rung + 1);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "   %s", formatRung(rung).c_str());
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##comment", "comment: why this network exists", &rung.comment)) {
        changed();
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
        ImGui::OpenPopup("Edit element");
        openEditor_ = false;
    }
    if (!ImGui::BeginPopup("Edit element")) {
        return;
    }
    if (!selection_ || !selection_->item || selection_->rung >= rungs_.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& rung = rungs_[selection_->rung];
    const bool appearing = ImGui::IsWindowAppearing();
    const auto operandField = [&](std::string& operand) {
        if (appearing) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::InputTextWithHint("Operand", "a variable, t1.Q or %IX0.0", &operand,
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            ImGui::CloseCurrentPopup(); // Enter: done
        }
        if (ImGui::IsItemEdited()) {
            changed();
        }
    };

    if (const auto* coilPath = std::get_if<CoilPath>(&*selection_->item)) {
        auto& coil = rung.coils[coilPath->index];
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
            changed();
        }
        operandField(coil.operand);
    } else if (auto* element = elementAt(rung, std::get<ElementPath>(*selection_->item))) {
        if (auto* contact = std::get_if<Contact>(element)) {
            int kind = static_cast<int>(contact->kind);
            ImGui::TextUnformatted("Contact");
            bool radio = ImGui::RadioButton("-| |-  normally open", &kind, 0);
            ImGui::SameLine();
            radio |= ImGui::RadioButton("-|/|-  normally closed", &kind, 1);
            if (radio) {
                contact->kind = static_cast<ContactKind>(kind);
                changed();
            }
            operandField(contact->operand);
        } else if (auto* box = std::get_if<Box>(element)) {
            ImGui::SetNextItemWidth(160.0F);
            if (ImGui::BeginCombo("Type", box->type.c_str())) {
                for (const auto& type : standardBoxTypes()) {
                    if (ImGui::Selectable(std::string(type.name).c_str(), type.name == box->type)) {
                        box->type = type.name;
                        changed();
                    }
                }
                ImGui::EndCombo();
            }
            if (appearing) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(160.0F);
            if (ImGui::InputText("Instance", &box->instance)) {
                changed();
            }
            if (const auto* type = findBoxType(box->type)) {
                const auto before = *box;
                for (const auto pin : type->inputs) {
                    pinField(fmt::format("{} :=", pin).c_str(), pin, box->inputs);
                }
                for (const auto pin : type->outputs) {
                    pinField(fmt::format("{} =>", pin).c_str(), pin, box->outputs);
                }
                if (!(*box == before)) {
                    changed();
                }
            }
        }
    }
    if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
    if (ImGui::IsKeyPressed(ImGuiKey_C) && !ctrl)
        insertElement(Contact{});
    if (ImGui::IsKeyPressed(ImGuiKey_N) && !ctrl)
        insertElement(Contact{ContactKind::NormallyClosed, ""});
    if (ImGui::IsKeyPressed(ImGuiKey_B))
        openBoxes_ = true;
    if (ImGui::IsKeyPressed(ImGuiKey_P)) {
        io.KeyShift ? addSide() : addBranch();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O))
        addCoil();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        deleteSelected();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        moveSelected(-1);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        moveSelected(+1);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && selection_ && selection_->item)
        openEditor_ = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N))
        addNetwork();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S))
        save();
}

// ---------------------------------------------------------------- Actions

void EditorWindow::select(std::size_t rung, std::optional<ItemPath> item, bool edit) {
    selection_ = Selection{rung, std::move(item)};
    openEditor_ = edit && selection_->item.has_value();
    status_.clear();
}

void EditorWindow::insertElement(Element element) {
    if (!selection_) {
        report("Select an element or a network first: new elements go after the selection.");
        return;
    }
    auto& rung = rungs_[selection_->rung];
    // After the selected element; at the end of the logic for a coil or a whole network.
    InsertPoint point{{}, rung.logic.elements.size()};
    if (selection_->item) {
        if (const auto* path = std::get_if<ElementPath>(&*selection_->item)) {
            point = after(*path);
        }
    }
    auto inserted = ladder::insertElement(rung, point, std::move(element));
    if (!inserted) {
        report(inserted.error());
        return;
    }
    changed();
    select(selection_->rung, *inserted, true); // straight into editing its operand
}

void EditorWindow::addBranch() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select a contact or box to put a branch around.");
        return;
    }
    auto added = addParallel(rungs_[selection_->rung], *path, Contact{});
    if (!added) {
        report(added.error());
        return;
    }
    changed();
    select(selection_->rung, *added, true);
}

void EditorWindow::addSide() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element on a branch to add a side to that branch.");
        return;
    }
    auto added = ladder::addSide(rungs_[selection_->rung], *path, Contact{});
    if (!added) {
        report(added.error());
        return;
    }
    changed();
    select(selection_->rung, *added, true);
}

void EditorWindow::addCoil() {
    if (!selection_) {
        report("Select a network first.");
        return;
    }
    auto& rung = rungs_[selection_->rung];
    std::size_t index = rung.coils.size();
    if (selection_->item) {
        if (const auto* coil = std::get_if<CoilPath>(&*selection_->item)) {
            index = coil->index + 1;
        }
    }
    auto added = insertCoil(rung, index, Coil{});
    if (!added) {
        report(added.error());
        return;
    }
    changed();
    select(selection_->rung, *added, true);
}

void EditorWindow::deleteSelected() {
    if (!selection_) {
        return;
    }
    auto& rung = rungs_[selection_->rung];
    if (!selection_->item) {
        deleteNetwork(selection_->rung);
        return;
    }
    if (const auto* coil = std::get_if<CoilPath>(&*selection_->item)) {
        auto removed = removeCoil(rung, *coil);
        if (!removed) {
            report(removed.error());
            return;
        }
        changed();
        select(selection_->rung, *removed ? std::optional<ItemPath>(**removed) : std::nullopt);
        return;
    }
    auto removed = removeElement(rung, std::get<ElementPath>(*selection_->item));
    if (!removed) {
        report(removed.error());
        return;
    }
    changed();
    select(selection_->rung, *removed ? std::optional<ItemPath>(**removed) : std::nullopt);
}

void EditorWindow::moveSelected(int direction) {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element to move it.");
        return;
    }
    auto moved = moveElement(rungs_[selection_->rung], *path, direction);
    if (!moved) {
        report(moved.error());
        return;
    }
    changed();
    select(selection_->rung, *moved);
}

void EditorWindow::addNetwork() {
    const auto index = selection_ ? selection_->rung + 1 : rungs_.size();
    Rung rung;
    rung.coils.push_back(Coil{});
    rungs_.insert(rungs_.begin() + static_cast<std::ptrdiff_t>(index), std::move(rung));
    changed();
    select(index, CoilPath{0}, true);
}

void EditorWindow::deleteNetwork(std::size_t index) {
    if (index >= rungs_.size()) {
        return;
    }
    rungs_.erase(rungs_.begin() + static_cast<std::ptrdiff_t>(index));
    changed();
    if (rungs_.empty()) {
        selection_.reset();
    } else {
        select(std::min(index, rungs_.size() - 1), std::nullopt);
    }
}

void EditorWindow::moveNetwork(std::size_t index, int direction) {
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= rungs_.size())) {
        return;
    }
    const auto other = direction < 0 ? index - 1 : index + 1;
    std::swap(rungs_[index], rungs_[other]);
    changed();
    select(other, std::nullopt);
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
    modified_ = false;
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
