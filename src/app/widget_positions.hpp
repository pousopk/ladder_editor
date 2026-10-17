#pragma once
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace app {

// Where some widgets were drawn in the last frame, by name. The UI tests use
// it to click a button or a field, the way a person would, without knowing
// the window's layout.
class WidgetPositions {
public:
    void clear() { centers_.clear(); }

    // Remembers the widget ImGui just drew (the "last item") under `name`.
    void remember(std::string name) {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        centers_[std::move(name)] = ImVec2{(min.x + max.x) / 2.0F, (min.y + max.y) / 2.0F};
    }

    std::optional<ImVec2> center(std::string_view name) const {
        const auto it = centers_.find(name);
        return it == centers_.end() ? std::nullopt : std::optional(it->second);
    }

private:
    std::map<std::string, ImVec2, std::less<>> centers_; // std::less<>: look up by string_view
};

} // namespace app
