#pragma once
#include "app/project_window.hpp"
#include "ladder/block.hpp"
#include "ladder/layout.hpp"
#include "ladder/notation.hpp"
#include "view/rung_view.hpp"
#include <chrono>
#include <doctest/doctest.h>
#include <functional>
#include <imgui.h>
#include <string>
#include <thread>
#include <vector>

// Drives the editor's UI like a user would: mouse moves, clicks and keys,
// one ImGui frame at a time, without a window or a GPU. ImGui doesn't care
// where its input comes from, so the tests feed it events directly.
namespace test {

using namespace ladder;

// One ImGui context with a fixed screen, and helpers to send input.
class Ui {
public:
    // A project with one KOP PROGRAM, Main, holding `rungs`; saved to
    // `folder` if one is given.
    explicit Ui(std::vector<Rung> rungs, std::string folder = "")
        : Ui(projectWith(std::move(rungs)), std::move(folder)) {}

    Ui(Project project, std::string folder)
        : context_(ImGui::CreateContext()), window_(std::move(project), std::move(folder)) {
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0F, 860.0F);
        io.DeltaTime = 1.0F / 60.0F;
        io.IniFilename = nullptr;
        io.Fonts->Build(); // no renderer: build the font atlas ourselves
        frame();
        frame(); // the first frames only lay things out
    }
    ~Ui() { ImGui::DestroyContext(context_); }
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    void frame() {
        ImGui::NewFrame();
        window_.draw();
        ImGui::Render(); // makes the draw data; nothing shows it, which is fine
    }

    // Draws frames until `condition` holds (online mode: the PLC runs on its
    // own time). False after 3 seconds.
    bool waitFor(const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            frame();
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    // Clicks the centre of an element or coil of network `rung`.
    void click(std::size_t rung, const ItemPath& item, int clicks = 1) {
        const auto position = centerOf(rung, item);
        auto& io = ImGui::GetIO();
        pause(); // so two separate clicks never count as a double-click
        io.AddMousePosEvent(position.x, position.y);
        frame();
        for (int i = 0; i < clicks; ++i) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frame();
        }
        frame();
    }

    // Clicks inside network `rung` but on no element: on the wire halfway
    // between column 2 and 3, where a short rung has no element.
    void clickNetworkBackground(std::size_t rung) {
        const auto origin = *editor().rungOrigin(rung);
        const view::Metrics m;
        auto& io = ImGui::GetIO();
        pause();
        io.AddMousePosEvent(origin.x + (m.rail + 2.5F * m.column) * editor().zoom(),
                            origin.y + m.power * editor().zoom());
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    }

    // Presses and releases a key, optionally while holding Ctrl or Shift.
    // Like a real backend, it sends both the key (ImGuiKey_LeftCtrl) and the
    // modifier state (ImGuiMod_Ctrl), which is what io.KeyCtrl reads.
    void key(ImGuiKey key, ImGuiKey modifier = ImGuiKey_None, bool withShift = false) {
        auto& io = ImGui::GetIO();
        const ImGuiKey mod = modifier == ImGuiKey_LeftCtrl    ? ImGuiMod_Ctrl
                             : modifier == ImGuiKey_LeftShift ? ImGuiMod_Shift
                                                              : ImGuiKey_None;
        if (mod != ImGuiKey_None) {
            io.AddKeyEvent(modifier, true);
            io.AddKeyEvent(mod, true);
        }
        if (withShift) {
            io.AddKeyEvent(ImGuiKey_LeftShift, true);
            io.AddKeyEvent(ImGuiMod_Shift, true);
        }
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        if (withShift) {
            io.AddKeyEvent(ImGuiKey_LeftShift, false);
            io.AddKeyEvent(ImGuiMod_Shift, false);
        }
        if (mod != ImGuiKey_None) {
            io.AddKeyEvent(modifier, false);
            io.AddKeyEvent(mod, false);
        }
        frame();
        frame();
    }

    // Clicks a point on the screen.
    void clickAt(ImVec2 position) {
        auto& io = ImGui::GetIO();
        pause();
        io.AddMousePosEvent(position.x, position.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    }

    // Clicks a widget the window (or the open KOP editor) remembered by name.
    void clickWidget(std::string_view name) {
        auto center = window_.widgets().center(name);
        if (!center && window_.kopEditor(window_.currentBlock()) != nullptr) {
            center = window_.kopEditor(window_.currentBlock())->widgets().center(name);
        }
        REQUIRE_MESSAGE(center.has_value(), "no widget called " << std::string(name));
        clickAt(*center);
        frame();
    }

    // Half a second without input, as between two actions of a person.
    void pause() {
        auto& io = ImGui::GetIO();
        io.DeltaTime = 0.5F;
        frame();
        io.DeltaTime = 1.0F / 60.0F;
    }

    void type(const char* text) {
        ImGui::GetIO().AddInputCharactersUTF8(text);
        frame();
    }

    std::string rung(std::size_t index) const { return formatRung(window_.project().blocks[0].networks.at(index)); }
    // Main's editor: the block the window opens first.
    app::KopEditor& editor() { return *window_.kopEditor(0); }
    app::ProjectWindow& window() { return window_; }

private:
    ImVec2 centerOf(std::size_t rung, const ItemPath& item) {
        const auto layout = layOut(window_.project().blocks[0].networks.at(rung));
        for (const auto& placed : layout.items) {
            if (placed.path == item) {
                return view::itemCenter(placed, *editor().rungOrigin(rung), editor().zoom());
            }
        }
        FAIL("no such item in network " << rung);
        return {};
    }

    ImGuiContext* context_;
    static Project projectWith(std::vector<Rung> rungs) {
        Project project;
        project.name = "test";
        auto main = newBlock("Main", BlockKind::Program, Language::Kop);
        main.networks = std::move(rungs);
        project.blocks.push_back(std::move(main));
        return project;
    }

    app::ProjectWindow window_;
};

} // namespace test
