#include "app/watch_table.hpp"
#include "online/paths.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace app {

namespace {

const ImVec4 kDim{0.6F, 0.6F, 0.65F, 1.0F};
const ImVec4 kProblem{0.95F, 0.45F, 0.40F, 1.0F};
const ImVec4 kOnline{0.45F, 0.85F, 0.50F, 1.0F};
const ImVec4 kForced{0.95F, 0.70F, 0.30F, 1.0F};

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return text;
}

bool isForced(const std::map<std::string, std::string>& forces, const std::string& path) {
    return std::any_of(forces.begin(), forces.end(),
                       [&](const auto& force) { return upper(force.first) == upper(path); });
}

} // namespace

std::optional<std::string> WatchTable::pathOf(const ladder::WatchEntry& entry, const online::Session* session) const {
    auto path = online::monitorPath(entry.path, ""); // an address reads its generated variable
    if (path && session != nullptr) {
        if (const auto variable = session->describe(*path)) {
            return variable->path; // spelled as declared
        }
    }
    return path;
}

bool WatchTable::draw(std::vector<ladder::WatchEntry>& entries, online::Session* session) {
    widgets_.clear();
    watched_.clear();
    bool changed = false;
    const bool online = session != nullptr;
    const auto forces = online ? session->forces() : std::map<std::string, std::string>{};

    ImGui::TextColored(kDim, online ? "Set writes the value once (the program may change it again); Force holds it "
                                      "every cycle, until Unforce."
                                    : "Add the variables to watch; go online to see their values.");
    std::optional<std::size_t> remove;
    const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("watch", 6, flags)) {
        ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.8F);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Modify value", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 240.0F);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            const auto path = pathOf(entry, session);
            if (path && std::find(watched_.begin(), watched_.end(), *path) == watched_.end()) {
                watched_.push_back(*path);
            }
            const auto variable = online && path ? session->describe(*path) : std::nullopt;
            const bool unknown = online && session->variablesKnown() && !variable;

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0F);
            if (unknown) {
                ImGui::PushStyleColor(ImGuiCol_Text, kProblem);
            }
            changed |= ImGui::InputTextWithHint("##path", "start, t1.ET, p1.demand", &entry.path);
            if (unknown) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("soft_plc has no variable called %s", entry.path.c_str());
            }
            widgets_.remember(fmt::format("path {}", i));

            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", variable ? variable->type.c_str() : "");

            ImGui::TableNextColumn();
            const bool forced = path && isForced(forces, *path);
            if (const auto value = online && path ? session->value(*path) : std::nullopt) {
                ImGui::TextColored(forced ? kForced : kOnline, "%s%s", value->c_str(), forced ? "  F" : "");
                if (forced) {
                    ImGui::SetItemTooltip("Forced: held at this value every cycle");
                }
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0F);
            changed |= ImGui::InputTextWithHint("##modify", "TRUE, 42, T#5s", &entry.modify);
            widgets_.remember(fmt::format("modify {}", i));

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0F);
            changed |= ImGui::InputText("##comment", &entry.comment);

            ImGui::TableNextColumn();
            const bool canWrite = online && path && !entry.modify.empty();
            ImGui::BeginDisabled(!canWrite);
            if (ImGui::SmallButton("Set")) {
                session->send(fmt::format("WRITE {} {}", *path, entry.modify));
            }
            ImGui::SetItemTooltip("Write the modify value once");
            widgets_.remember(fmt::format("set {}", i));
            ImGui::SameLine();
            if (ImGui::SmallButton("Force")) {
                session->send(fmt::format("FORCE {} {}", *path, entry.modify));
            }
            ImGui::SetItemTooltip("Hold the variable at the modify value, every cycle");
            widgets_.remember(fmt::format("force {}", i));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!forced);
            if (ImGui::SmallButton("Unforce")) {
                session->send("UNFORCE " + *path);
            }
            widgets_.remember(fmt::format("unforce {}", i));
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                remove = i;
            }
            ImGui::SetItemTooltip("Remove this line");
            widgets_.remember(fmt::format("delete {}", i));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove) {
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(*remove));
        changed = true;
    }

    if (ImGui::SmallButton("+ Line")) {
        entries.push_back(ladder::WatchEntry{});
        changed = true;
    }
    widgets_.remember("+ line");
    ImGui::SameLine();
    ImGui::BeginDisabled(!online);
    if (ImGui::SmallButton("Set all")) {
        for (const auto& entry : entries) { // every line with a modify value, in order, in one go
            const auto path = pathOf(entry, session);
            if (path && !entry.modify.empty()) {
                session->send(fmt::format("WRITE {} {}", *path, entry.modify));
            }
        }
    }
    ImGui::SetItemTooltip("Write every line's modify value, in order");
    widgets_.remember("set all");
    ImGui::SameLine();
    ImGui::BeginDisabled(forces.empty());
    if (ImGui::SmallButton("Unforce all")) {
        for (const auto& [forcedPath, value] : forces) {
            session->send("UNFORCE " + forcedPath);
        }
    }
    ImGui::SetItemTooltip("Remove every force in the PLC, including ones set elsewhere");
    widgets_.remember("unforce all");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (online) {
        const auto error = session->status().lastError;
        if (!error.empty()) {
            ImGui::TextColored(kProblem, "soft_plc: %s", error.c_str());
        }
    }
    return changed;
}

} // namespace app
