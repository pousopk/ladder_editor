#include "app/editor_window.hpp"
#include "ladder/check.hpp"
#include "ladder/notation.hpp"
#include "view/rung_view.hpp"
#include <fmt/format.h>
#include <imgui.h>

namespace app {

namespace {

const ImVec4 kComment{0.55F, 0.75F, 0.55F, 1.0F};
const ImVec4 kProblem{0.95F, 0.45F, 0.40F, 1.0F};
const ImVec4 kDim{0.6F, 0.6F, 0.65F, 1.0F};

const char* contactName(ladder::ContactKind kind) {
    return kind == ladder::ContactKind::NormallyOpen ? "normally open contact" : "normally closed contact";
}

const char* coilName(ladder::CoilKind kind) {
    switch (kind) {
    case ladder::CoilKind::Normal:
        return "coil";
    case ladder::CoilKind::Negated:
        return "negated coil";
    case ladder::CoilKind::Set:
        return "set coil";
    case ladder::CoilKind::Reset:
        return "reset coil";
    }
    return "coil";
}

} // namespace

EditorWindow::EditorWindow(std::string title, std::vector<ladder::Rung> rungs)
    : title_(std::move(title)), rungs_(std::move(rungs)) {
    for (const auto& rung : rungs_) {
        layouts_.push_back(ladder::layOut(rung));
    }
}

void EditorWindow::draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ladder_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    drawToolbar();
    ImGui::Separator();
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 6.0F;
    ImGui::BeginChild("networks", ImVec2(0, -detailsHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawNetworks();
    ImGui::EndChild();
    ImGui::Separator();
    drawDetails();
    ImGui::End();
}

void EditorWindow::drawToolbar() {
    ImGui::TextUnformatted(title_.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kDim, "  %zu networks", rungs_.size());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0F);
    ImGui::SetNextItemWidth(160.0F);
    ImGui::SliderFloat("Zoom", &zoom_, 0.6F, 2.0F, "%.1fx");
}

void EditorWindow::drawNetworks() {
    // Text scales with the drawing, so labels stay where the symbols are.
    ImGui::SetWindowFontScale(zoom_);
    for (std::size_t i = 0; i < rungs_.size(); ++i) {
        const auto& rung = rungs_[i];
        ImGui::Text("Network %zu", i + 1);
        if (!rung.comment.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kComment, "%s", rung.comment.c_str());
        }
        const std::optional<ladder::ItemPath> selected =
            selection_ && selection_->rung == i ? std::optional(selection_->item) : std::nullopt;
        const auto shown = view::drawRung(rung, layouts_[i], static_cast<int>(i), selected, zoom_);
        if (shown.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selection_ = Selection{i, *shown.hovered};
        }
        for (const auto& problem : ladder::check(rung)) {
            ImGui::TextColored(kProblem, "  %s", problem.message.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::SetWindowFontScale(1.0F);
}

void EditorWindow::drawDetails() {
    if (!selection_ || selection_->rung >= rungs_.size()) {
        ImGui::TextColored(kDim, "Click an element to select it.");
        return;
    }
    const auto& rung = rungs_[selection_->rung];
    ImGui::Text("Network %zu", selection_->rung + 1);
    ImGui::SameLine();
    ImGui::TextColored(kDim, "   %s", ladder::formatRung(rung).c_str());

    if (const auto* coil = std::get_if<ladder::CoilPath>(&selection_->item)) {
        const auto& c = rung.coils[coil->index];
        ImGui::Text("%s  %s", coilName(c.kind), c.operand.c_str());
        return;
    }
    const auto* element = ladder::elementAt(rung, std::get<ladder::ElementPath>(selection_->item));
    if (const auto* contact = std::get_if<ladder::Contact>(element)) {
        ImGui::Text("%s  %s", contactName(contact->kind), contact->operand.c_str());
    } else if (const auto* box = std::get_if<ladder::Box>(element)) {
        ImGui::Text("%s box  %s", box->type.c_str(), box->instance.c_str());
        for (const auto& pin : box->inputs) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "  %s := %s", pin.name.c_str(), pin.operand.c_str());
        }
        for (const auto& pin : box->outputs) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "  %s => %s", pin.name.c_str(), pin.operand.c_str());
        }
    }
}

} // namespace app
