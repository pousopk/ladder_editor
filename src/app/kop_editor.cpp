#include "app/kop_editor.hpp"
#include "app/st_display.hpp"
#include "ladder/boxes.hpp"
#include "ladder/check.hpp"
#include "ladder/notation.hpp"
#include "ladder/power.hpp"
#include "online/paths.hpp"
#include "view/rung_view.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace app {

namespace {

using namespace ladder;

const ImVec4 kComment{0.55F, 0.75F, 0.55F, 1.0F};
const ImVec4 kProblem{0.95F, 0.45F, 0.40F, 1.0F};
const ImVec4 kDim{0.6F, 0.6F, 0.65F, 1.0F};
const ImVec4 kOnline{0.45F, 0.85F, 0.50F, 1.0F};

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

using ItemResult = tl::expected<std::optional<ItemPath>, std::string>;

// Turns an edit operation's result (an element or coil path, or an error)
// into what editRung expects.
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

void KopEditor::draw(Block& block, const KopContext& context) {
    block_ = &block;
    context_ = &context;
    watched_.clear();
    widgets_.clear();
    // A popup closed by a click elsewhere is already closed when the frame
    // starts: record its edit before that click does anything else.
    if (pending_ && !ImGui::IsPopupOpen(kEditPopup)) {
        finishPendingEdit();
    }
    drawToolbar();
    ImGui::Separator();
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 7.0F;
    const float stWidth = showSt_ ? std::max(ImGui::GetContentRegionAvail().x * 0.4F, 300.0F) : 0.0F;
    ImGui::BeginChild("networks", ImVec2(showSt_ ? -stWidth : 0.0F, -detailsHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawInterface();
    drawNetworks();
    ImGui::EndChild();
    if (showSt_) {
        ImGui::SameLine();
        ImGui::BeginChild("st", ImVec2(0.0F, -detailsHeight));
        drawSt();
        ImGui::EndChild();
    }
    ImGui::Separator();
    drawDetails();
    handleShortcuts(); // before the popups: the Enter that closes one mustn't open it again
    drawEditPopup();
    drawBoxPopup();
}

void KopEditor::drawToolbar() {
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
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 280.0F);
    ImGui::Checkbox("ST", &showSt_);
    ImGui::SetItemTooltip("Show the block as Structured Text: the code soft_plc runs");
    widgets_.remember("show st");
    ImGui::SameLine();
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

// ---------------------------------------------------------------- Interface

void KopEditor::drawInterface() {
    auto& block = *block_;
    if (!ImGui::CollapsingHeader(fmt::format("Interface ({} variables)###interface", block.variables.size()).c_str(),
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    // Cells are edited in place; a cell's whole edit is one step, recorded
    // when the cell is left.
    const auto track = [&](const Interface& before) {
        if (ImGui::IsItemActivated()) {
            cellEdit_ = before;
        }
        if (ImGui::IsItemDeactivated() && cellEdit_) {
            if (interfaceOf(block) != *cellEdit_) {
                auto command = std::make_unique<ReplaceInterface>(*cellEdit_, interfaceOf(block), "edit interface");
                command->selectionBefore = selection_;
                command->selectionAfter = selection_;
                history_.record(std::move(command));
            }
            cellEdit_.reset();
        }
    };

    if (block.kind == BlockKind::Function) {
        const auto before = interfaceOf(block);
        ImGui::SetNextItemWidth(200.0F);
        ImGui::InputTextWithHint("Returns", "the type the function gives back", &block.returnType);
        track(before);
    }
    const auto sections = sectionsFor(block.kind);
    std::optional<std::size_t> deleteRequest;
    std::optional<std::pair<std::size_t, Section>> sectionRequest;
    const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!block.variables.empty() && ImGui::BeginTable("variables", 7, flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Section", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Initial", ImGuiTableColumnFlags_WidthStretch, 0.8F);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 0.8F);
        ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0F);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < block.variables.size(); ++i) {
            auto& variable = block.variables[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            const auto before = interfaceOf(block);
            const auto cell = [&](const char* id, std::string& text, const char* hint) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::InputTextWithHint(id, hint, &text);
                widgets_.remember(fmt::format("{} {}", id + 2, i)); // "name 0", "type 0", ...
                track(before);
            };
            cell("##name", variable.name, "name");
            cell("##type", variable.type, "BOOL, INT, TON, ...");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##section", sectionName(variable.section))) {
                for (const auto section : sections) {
                    if (ImGui::Selectable(sectionName(section), section == variable.section)) {
                        sectionRequest = std::pair{i, section};
                    }
                }
                ImGui::EndCombo();
            }
            cell("##initial", variable.initial, "default");
            if (block.kind == BlockKind::Program) {
                cell("##address", variable.address, "none");
            } else {
                ImGui::TableNextColumn();
                ImGui::TextColored(kDim, "-");
            }
            cell("##comment", variable.comment, "");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) {
                deleteRequest = i;
            }
            ImGui::SetItemTooltip("Delete this variable");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("+ Variable")) {
        addVariable();
    }
    widgets_.remember("+ variable");
    ImGui::Spacing();
    // Changes to the table itself wait until it's no longer being drawn.
    if (sectionRequest) {
        auto after = interfaceOf(block);
        after.variables[sectionRequest->first].section = sectionRequest->second;
        changeInterface(std::move(after), "change section");
    }
    if (deleteRequest) {
        deleteVariable(*deleteRequest);
    }
}

void KopEditor::changeInterface(Interface after, std::string what) {
    auto command = std::make_unique<ReplaceInterface>(interfaceOf(*block_), std::move(after), std::move(what));
    command->selectionBefore = selection_;
    command->selectionAfter = selection_;
    history_.perform(std::move(command), *block_);
}

void KopEditor::addVariable() {
    auto after = interfaceOf(*block_);
    std::string name;
    for (int n = 1;; ++n) {
        name = fmt::format("var{}", n);
        const bool used = std::any_of(after.variables.begin(), after.variables.end(),
                                      [&](const Variable& v) { return lower(v.name) == name; });
        if (!used) {
            break;
        }
    }
    const auto sections = sectionsFor(block_->kind);
    after.variables.push_back(Variable{name, "BOOL", sections.empty() ? Section::Local : sections.front(), "", "", ""});
    changeInterface(std::move(after), "add variable");
}

void KopEditor::deleteVariable(std::size_t index) {
    auto after = interfaceOf(*block_);
    if (index >= after.variables.size()) {
        return;
    }
    after.variables.erase(after.variables.begin() + static_cast<std::ptrdiff_t>(index));
    changeInterface(std::move(after), "delete variable");
}

// ---------------------------------------------------------------- Networks

void KopEditor::drawNetworks() {
    auto& networks = block_->networks;
    // Text scales with the drawing, so labels stay where the symbols are.
    ImGui::SetWindowFontScale(zoom_);
    origins_.assign(networks.size(), ImVec2{-1.0F, -1.0F});
    if (networks.empty()) {
        ImGui::TextColored(kDim, "No networks yet: press Ctrl+N or click +Network to add one.");
    }
    if (context_->error && !context_->error->network) {
        ImGui::TextColored(kProblem, "soft_plc, in the interface: %s", context_->error->message.c_str());
    }
    std::optional<std::size_t> deleteRequest;
    std::optional<std::pair<std::size_t, int>> moveRequest;
    for (std::size_t i = 0; i < networks.size(); ++i) {
        const auto& rung = networks[i];
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
        const auto layout = layOut(rung, boxes()); // cheap: recomputed every frame, so it's never stale
        origins_[i] = ImGui::GetCursorScreenPos();
        std::optional<view::RungOnline> live;
        if (online()) {
            live.emplace();
            live->power =
                evaluatePower(rung, [this](const std::string& operand) { return onlineBool(operand); }, boxes());
            live->value = [this](const std::string& operand) -> std::optional<std::string> {
                const auto path = watch(operand);
                return path ? context_->session->value(*path) : std::nullopt;
            };
        }
        const auto shown =
            view::drawRung(rung, layout, static_cast<int>(i), selected, zoom_, boxes(), live ? &*live : nullptr);
        if (shown.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            select(i, *shown.hovered);
        }
        if (shown.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // Online, a double-click on a contact or coil flips its operand
            // in the PLC (as "Modify" does in TIA Portal); offline, it edits.
            std::optional<std::string> operand;
            if (const auto* coil = std::get_if<CoilPath>(&*shown.hovered)) {
                operand = rung.coils[coil->index].operand;
            } else if (const auto* contact =
                           std::get_if<Contact>(elementAt(rung, std::get<ElementPath>(*shown.hovered)))) {
                operand = contact->operand;
            }
            if (online() && operand) {
                if (const auto value = onlineBool(*operand)) {
                    modify(*operand, "WRITE", !*value);
                }
            } else {
                select(i, *shown.hovered, true);
            }
        }
        if (shown.overRung && !shown.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            select(i, std::nullopt); // a click between the elements selects the network
        }
        drawProblems(i);
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

// What's wrong with a network: the rung's own rules first; if it keeps
// those, what stopped its translation, or soft_plc's error in its code.
void KopEditor::drawProblems(std::size_t network) {
    const auto problems = check(block_->networks[network], boxes());
    for (const auto& problem : problems) {
        ImGui::TextColored(kProblem, "  %s", problem.message.c_str());
    }
    if (!problems.empty() || context_->translation == nullptr) {
        return;
    }
    for (const auto& problem : context_->translation->problems) {
        if (problem.network == network) {
            ImGui::TextColored(kProblem, "  %s", problem.message.c_str());
        }
    }
    const auto& error = context_->error;
    if (error && error->network == network) {
        ImGui::TextColored(kProblem, "  soft_plc: %s", error->message.c_str());
        widgets_.remember(fmt::format("error {}", network));
    }
}

// The block as ST, read-only, coloured, with soft_plc's error marked.
void KopEditor::drawSt() {
    if (context_->translation == nullptr) {
        ImGui::TextColored(kDim, "Not translated yet.");
        return;
    }
    const auto& text = context_->translation->text;
    if (text != stShown_) {
        // A new editor for the new text (it's read-only: nothing is lost).
        stView_ = view::CodeEditor(text);
        stView_.setReadOnly(true);
        stView_.setColors(stColors(text));
        stShown_ = text;
    }
    const auto& error = context_->error;
    stView_.setError(error ? std::optional(view::ErrorMark{error->line, error->column, error->message}) : std::nullopt);
    stView_.setNotes(online() ? onlineNotes(text, *context_->session, context_->instance, watched_)
                              : std::vector<std::string>{});
    const float messageHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0F;
    stView_.draw("##st", ImVec2(-1.0F, -messageHeight));
    widgets_.remember("st view");
    if (error && !error->network) {
        ImGui::TextColored(kProblem, "line %zu: %s", error->line + 1, error->message.c_str());
    } else {
        ImGui::TextColored(kDim, "Generated from the networks: edit them, not this.");
    }
}

void KopEditor::drawDetails() {
    auto& networks = block_->networks;
    if (!selection_ || selection_->rung >= networks.size()) {
        ImGui::TextColored(kDim, "Click an element or a network title to select it. Hover the buttons for shortcuts.");
        return;
    }
    auto& rung = networks[selection_->rung];
    ImGui::Text("Network %zu", selection_->rung + 1);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "   %s", formatRung(rung).c_str());

    // The comment is edited in place; the whole edit becomes one step when
    // the field is left.
    ImGui::SetNextItemWidth(-1.0F);
    const auto before = rung;
    ImGui::InputTextWithHint("##comment", "comment: why this network exists", &rung.comment);
    widgets_.remember("comment");
    if (ImGui::IsItemActivated()) {
        commentEdit_ = PendingEdit{selection_->rung, before, selection_, "edit comment", false};
    }
    if (ImGui::IsItemDeactivated() && commentEdit_) {
        if (networks[commentEdit_->rung] != commentEdit_->before) {
            auto command = std::make_unique<ReplaceRung>(commentEdit_->rung, commentEdit_->before,
                                                         networks[commentEdit_->rung], commentEdit_->what);
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
        drawOnlineDetails(coil.operand);
    } else if (const auto* element = elementAt(rung, std::get<ElementPath>(*selection_->item))) {
        ImGui::Text("Selected: %s", formatElement(*element).c_str());
        if (const auto* contact = std::get_if<Contact>(element)) {
            drawOnlineDetails(contact->operand);
        }
    }
}

// Online: the selected operand's value, and buttons to change it in the PLC.
void KopEditor::drawOnlineDetails(const std::string& operand) {
    if (!online()) {
        return;
    }
    const auto path = watch(operand);
    if (!path) {
        return;
    }
    const auto value = context_->session->value(*path);
    const auto forces = context_->session->forces();
    const bool forced = std::any_of(forces.begin(), forces.end(),
                                    [&](const auto& force) { return lower(force.first) == lower(*path); });
    ImGui::SameLine();
    ImGui::TextColored(kOnline, "  = %s%s", value ? value->c_str() : "?", forced ? "  (forced)" : "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Set TRUE"))
        modify(operand, "WRITE", true);
    widgets_.remember("set true");
    ImGui::SameLine();
    if (ImGui::SmallButton("Set FALSE"))
        modify(operand, "WRITE", false);
    widgets_.remember("set false");
    ImGui::SameLine();
    if (ImGui::SmallButton("Force TRUE"))
        modify(operand, "FORCE", true);
    widgets_.remember("force true");
    ImGui::SameLine();
    if (ImGui::SmallButton("Force FALSE"))
        modify(operand, "FORCE", false);
    widgets_.remember("force false");
    if (forced) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Unforce"))
            context_->session->send("UNFORCE " + *path);
        widgets_.remember("unforce");
    }
    ImGui::TextColored(kDim, "Online: double-click a contact or coil to flip its operand. Set writes once (the "
                             "program may change it again); Force holds it every cycle.");
}

std::optional<std::string> KopEditor::watch(const std::string& operand) {
    auto path = online::monitorPath(operand, context_->instance);
    if (path && std::find(watched_.begin(), watched_.end(), *path) == watched_.end()) {
        watched_.push_back(*path);
    }
    return path;
}

std::optional<bool> KopEditor::onlineBool(const std::string& operand) {
    if (const auto literal = online::boolLiteral(operand)) {
        return literal;
    }
    const auto path = watch(operand);
    return path ? context_->session->boolean(*path) : std::nullopt;
}

void KopEditor::modify(const std::string& operand, const char* command, bool value) {
    if (const auto path = watch(operand)) {
        context_->session->send(fmt::format("{} {} {}", command, *path, value ? "TRUE" : "FALSE"));
    }
}

void KopEditor::drawEditPopup() {
    if (openEditor_) {
        ImGui::OpenPopup(kEditPopup);
        openEditor_ = false;
    }
    if (!ImGui::BeginPopup(kEditPopup)) {
        return;
    }
    auto& networks = block_->networks;
    if (!selection_ || !selection_->item || selection_->rung >= networks.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& rung = networks[selection_->rung];
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
                for (const auto& type : boxes().all()) {
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
            if (const auto* type = boxes().find(box->type)) {
                for (const auto& pin : type->inputs) {
                    pinField(fmt::format("{} :=", pin).c_str(), pin, box->inputs);
                }
                for (const auto& pin : type->outputs) {
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

void KopEditor::finishPendingEdit() {
    auto edit = std::move(*pending_);
    pending_.reset();
    auto& networks = block_->networks;
    if (edit.rung >= networks.size() || networks[edit.rung] == edit.before) {
        return; // nothing changed
    }
    auto command = std::make_unique<ReplaceRung>(edit.rung, edit.before, networks[edit.rung], edit.what);
    command->selectionBefore = edit.selection;
    command->selectionAfter = edit.selection;
    history_.record(std::move(command), edit.mergeWithLast);
}

void KopEditor::drawBoxPopup() {
    if (openBoxes_) {
        ImGui::OpenPopup("Insert box");
        openBoxes_ = false;
        boxChoice_ = 0;
    }
    if (!ImGui::BeginPopup("Insert box")) {
        return;
    }
    // Arrow keys and Enter work as well as the mouse: B, Down, Enter.
    const auto& types = boxes().all();
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

void KopEditor::handleShortcuts() {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return; // keys belong to the text field or popup being used
    }
    if (io.KeyCtrl) {
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
        return; // Ctrl+S belongs to the project
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

void KopEditor::select(std::size_t rung, std::optional<ItemPath> item, bool edit) {
    selection_ = Selection{rung, std::move(item)};
    openEditor_ = edit && selection_->item.has_value();
    mergeNextEdit_ = false;
    status_.clear();
}

template <class Change> void KopEditor::editRung(std::size_t index, std::string what, Change change, bool edit) {
    auto& networks = block_->networks;
    Rung after = networks[index];
    ItemResult result = change(after);
    if (!result) {
        report(result.error());
        return;
    }
    auto command = std::make_unique<ReplaceRung>(index, networks[index], std::move(after), std::move(what));
    command->selectionBefore = selection_;
    command->selectionAfter = Selection{index, *result};
    history_.perform(std::move(command), *block_);
    select(index, *result, edit);
    mergeNextEdit_ = edit; // the operand typed next belongs to this step
}

void KopEditor::insertElement(Element element) {
    if (!selection_ || selection_->rung >= block_->networks.size()) {
        report("Select an element or a network first: new elements go after the selection.");
        return;
    }
    const auto& rung = block_->networks[selection_->rung];
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

void KopEditor::addBranch() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select a contact or box to put a branch around.");
        return;
    }
    const auto target = *path;
    editRung(selection_->rung, "add branch", [&](Rung& r) { return toItem(addParallel(r, target, Contact{})); }, true);
}

void KopEditor::addSide() {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element on a branch to add a side to that branch.");
        return;
    }
    const auto target = *path;
    editRung(
        selection_->rung, "add side", [&](Rung& r) { return toItem(ladder::addSide(r, target, Contact{})); }, true);
}

void KopEditor::addCoil() {
    if (!selection_ || selection_->rung >= block_->networks.size()) {
        report("Select a network first.");
        return;
    }
    std::size_t index = block_->networks[selection_->rung].coils.size();
    if (selection_->item) {
        if (const auto* coil = std::get_if<CoilPath>(&*selection_->item)) {
            index = coil->index + 1;
        }
    }
    editRung(selection_->rung, "add coil", [&](Rung& r) { return toItem(insertCoil(r, index, Coil{})); }, true);
}

void KopEditor::deleteSelected() {
    if (!selection_ || selection_->rung >= block_->networks.size()) {
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
    const auto* element = elementAt(block_->networks[selection_->rung], path);
    const auto what = "delete " + (element != nullptr ? kindOf(*element) : std::string("element"));
    editRung(selection_->rung, what, [&](Rung& r) { return toItem(removeElement(r, path)); });
}

void KopEditor::moveSelected(int direction) {
    const auto* path = selection_ && selection_->item ? std::get_if<ElementPath>(&*selection_->item) : nullptr;
    if (path == nullptr) {
        report("Select an element to move it.");
        return;
    }
    const auto target = *path;
    editRung(selection_->rung, "move element", [&](Rung& r) { return toItem(moveElement(r, target, direction)); });
}

void KopEditor::addNetwork() {
    const auto index = selection_ ? std::min(selection_->rung + 1, block_->networks.size()) : block_->networks.size();
    Rung rung;
    rung.coils.push_back(Coil{});
    auto command = std::make_unique<InsertNetwork>(index, std::move(rung));
    command->selectionBefore = selection_;
    command->selectionAfter = Selection{index, CoilPath{0}};
    history_.perform(std::move(command), *block_);
    select(index, CoilPath{0}, true);
    mergeNextEdit_ = true; // the coil's operand, typed next, is part of adding the network
}

void KopEditor::deleteNetwork(std::size_t index) {
    auto& networks = block_->networks;
    if (index >= networks.size()) {
        return;
    }
    auto command = std::make_unique<RemoveNetwork>(index, networks[index]);
    command->selectionBefore = Selection{index, std::nullopt};
    if (networks.size() > 1) {
        command->selectionAfter = Selection{std::min(index, networks.size() - 2), std::nullopt};
    }
    const auto after = command->selectionAfter;
    history_.perform(std::move(command), *block_);
    selection_ = after;
    status_.clear();
}

void KopEditor::moveNetwork(std::size_t index, int direction) {
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= block_->networks.size())) {
        return;
    }
    const auto other = direction < 0 ? index - 1 : index + 1;
    auto command = std::make_unique<SwapNetworks>(index, other);
    command->selectionBefore = Selection{index, std::nullopt};
    command->selectionAfter = Selection{other, std::nullopt};
    history_.perform(std::move(command), *block_);
    select(other, std::nullopt);
}

void KopEditor::undo() {
    if (!history_.canUndo()) {
        return;
    }
    selection_ = history_.undo(*block_);
    if (selection_ && selection_->rung >= block_->networks.size()) {
        selection_.reset();
    }
    status_.clear();
}

void KopEditor::redo() {
    if (!history_.canRedo()) {
        return;
    }
    selection_ = history_.redo(*block_);
    if (selection_ && selection_->rung >= block_->networks.size()) {
        selection_.reset();
    }
    status_.clear();
}

std::optional<ImVec2> KopEditor::rungOrigin(std::size_t index) const {
    if (index >= origins_.size() || origins_[index].x < 0.0F) {
        return std::nullopt;
    }
    return origins_[index];
}

std::string KopEditor::newInstanceName(std::string_view type) const {
    std::vector<std::string> used;
    for (const auto& rung : block_->networks) {
        collectInstances(rung.logic, used);
    }
    for (const auto& variable : block_->variables) {
        used.push_back(lower(variable.name)); // and not the name of a declared variable either
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
