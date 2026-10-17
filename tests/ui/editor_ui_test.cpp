#include "app/editor_window.hpp"
#include "ladder/notation.hpp"
#include "ladder/rung_file.hpp"
#include "view/rung_view.hpp"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <sstream>

// The editor's UI, driven like a user would: mouse moves, clicks and keys,
// one ImGui frame at a time, without a window or a GPU. ImGui doesn't care
// where its input comes from, so the tests feed it events directly.

using namespace ladder;

namespace {

// One ImGui context with a fixed screen, and helpers to send input.
class Ui {
public:
    explicit Ui(std::vector<Rung> rungs, std::string path = "")
        : context_(ImGui::CreateContext()), editor_(std::move(path), std::move(rungs)) {
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
        editor_.draw();
        ImGui::Render(); // makes the draw data; nothing shows it, which is fine
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
        const auto origin = *editor_.rungOrigin(rung);
        const view::Metrics m;
        auto& io = ImGui::GetIO();
        pause();
        io.AddMousePosEvent(origin.x + (m.rail + 2.5F * m.column) * editor_.zoom(),
                            origin.y + m.power * editor_.zoom());
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

    std::string rung(std::size_t index) const { return formatRung(editor_.rungs().at(index)); }
    const app::EditorWindow& editor() const { return editor_; }

private:
    ImVec2 centerOf(std::size_t rung, const ItemPath& item) const {
        const auto layout = layOut(editor_.rungs().at(rung));
        for (const auto& placed : layout.items) {
            if (placed.path == item) {
                return view::itemCenter(placed, *editor_.rungOrigin(rung), editor_.zoom());
            }
        }
        FAIL("no such item in network " << rung);
        return {};
    }

    ImGuiContext* context_;
    app::EditorWindow editor_;
};

std::vector<Rung> rungs(std::initializer_list<const char*> texts) {
    std::vector<Rung> result;
    for (const char* text : texts) {
        result.push_back(parseRung(text).value());
    }
    return result;
}

ElementPath at(std::vector<std::size_t> steps) {
    return ElementPath{std::move(steps)};
}

} // namespace

TEST_CASE("Clicking an element selects it; Delete removes it") {
    Ui ui(rungs({"start stop => motor", "a => b"}));
    ui.click(0, at({1}));
    REQUIRE(ui.editor().selection().has_value());
    CHECK(ui.editor().selection()->rung == 0);
    CHECK(ui.editor().selection()->item == ItemPath{at({1})});
    ui.key(ImGuiKey_Delete);
    CHECK(ui.rung(0) == "start => motor");
    CHECK(ui.editor().modified());
}

TEST_CASE("C inserts a contact after the selection and goes straight to its operand") {
    Ui ui(rungs({"start => motor"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_C);
    ui.type("guard");
    ui.key(ImGuiKey_Enter); // closes the popup
    CHECK(ui.rung(0) == "start guard => motor");
    ui.key(ImGuiKey_N); // a normally closed one after it
    ui.type("fault");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "start guard /fault => motor");
}

TEST_CASE("Building the self-holding starter with the keyboard") {
    Ui ui(rungs({"start => motor"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_P); // a branch around start
    ui.type("motor");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "(start | motor) => motor");
    ui.click(0, at({0, 0, 0}));             // start again
    ui.key(ImGuiKey_P, ImGuiKey_LeftShift); // another side on the same branch
    ui.type("remote");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "(start | motor | remote) => motor");
}

TEST_CASE("Moving elements and editing a coil by double-clicking it") {
    Ui ui(rungs({"a b => x"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_RightArrow, ImGuiKey_LeftCtrl);
    CHECK(ui.rung(0) == "b a => x");
    ui.click(0, CoilPath{0}, 2); // double-click: edit
    ui.type("y");                // the old operand is selected, so typing replaces it
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "b a => y");
    ui.key(ImGuiKey_O); // another coil after the selected one
    ui.type("z");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "b a => y, z");
}

TEST_CASE("Inserting a box from the palette, with a new instance name") {
    Ui ui(rungs({"start => lamp"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_B); // opens the palette, on its first entry: TON
    ui.key(ImGuiKey_Enter);
    ui.key(ImGuiKey_Escape); // close the box's edit popup
    CHECK(ui.rung(0) == "start {TON ton1} => lamp");
    ui.key(ImGuiKey_B);
    ui.key(ImGuiKey_DownArrow); // TOF
    ui.key(ImGuiKey_DownArrow); // TP
    ui.key(ImGuiKey_Enter);
    ui.key(ImGuiKey_Escape);
    CHECK(ui.rung(0) == "start {TON ton1} {TP tp1} => lamp");
}

TEST_CASE("Networks: Ctrl+N adds one after the selection, Delete with only the network selected removes it") {
    Ui ui(rungs({"a => x", "b => y"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_N, ImGuiKey_LeftCtrl);
    ui.type("ready");
    ui.key(ImGuiKey_Enter);
    REQUIRE(ui.editor().rungs().size() == 3);
    CHECK(ui.rung(1) == "=> ready");
    CHECK(ui.rung(2) == "b => y");

    ui.clickNetworkBackground(2);
    REQUIRE(ui.editor().selection().has_value());
    CHECK_FALSE(ui.editor().selection()->item.has_value()); // the network, not an element
    ui.key(ImGuiKey_Delete);
    REQUIRE(ui.editor().rungs().size() == 2);
    CHECK(ui.rung(1) == "=> ready");
}

TEST_CASE("The last coil can't be deleted; the editor says why") {
    Ui ui(rungs({"a => x"}));
    ui.click(0, CoilPath{0});
    ui.key(ImGuiKey_Delete);
    CHECK(ui.rung(0) == "a => x");
    CHECK_FALSE(ui.editor().modified());
}

TEST_CASE("Ctrl+S writes the rung file") {
    const auto path = std::filesystem::temp_directory_path() / "ladder_ui_test_save.lad";
    std::filesystem::remove(path);
    {
        Ui ui(rungs({"a b => x"}), path.string());
        ui.click(0, at({1}));
        ui.key(ImGuiKey_Delete);
        CHECK(ui.editor().modified());
        ui.key(ImGuiKey_S, ImGuiKey_LeftCtrl);
        CHECK_FALSE(ui.editor().modified());
    }
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    CHECK(text.str() == "a => x\n");
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------- Undo and redo

TEST_CASE("Inserting a contact and typing its operand is one step to undo") {
    Ui ui(rungs({"start => motor"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_C);
    ui.type("guard");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.rung(0) == "start guard => motor");
    CHECK(ui.editor().history().size() == 1);
    CHECK(ui.editor().history().undoDescription() == "insert contact");

    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.rung(0) == "start => motor");
    CHECK_FALSE(ui.editor().modified()); // back as it was opened
    ui.key(ImGuiKey_Y, ImGuiKey_LeftCtrl);
    CHECK(ui.rung(0) == "start guard => motor"); // with its operand
    CHECK(ui.editor().selection()->item == ItemPath{at({1})});
}

TEST_CASE("Undo brings a deleted element back, and selects it again") {
    Ui ui(rungs({"(a | b) c => x"}));
    ui.click(0, at({0, 1, 0})); // b
    ui.key(ImGuiKey_Delete);
    CHECK(ui.rung(0) == "a c => x");
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.rung(0) == "(a | b) c => x");
    CHECK(ui.editor().selection()->item == ItemPath{at({0, 1, 0})});
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl, true); // Ctrl+Shift+Z: redo
    CHECK(ui.rung(0) == "a c => x");
}

TEST_CASE("Editing an existing operand is its own step") {
    Ui ui(rungs({"a => x"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_C);
    ui.type("b");
    ui.key(ImGuiKey_Enter);  // step 1: insert b
    ui.click(0, at({0}), 2); // double-click a
    ui.type("first");
    ui.key(ImGuiKey_Enter); // step 2: edit a
    CHECK(ui.rung(0) == "first b => x");
    CHECK(ui.editor().history().size() == 2);
    CHECK(ui.editor().history().undoDescription() == "edit contact");
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.rung(0) == "a b => x");
}

TEST_CASE("A new network and its first coil's operand are one step") {
    Ui ui(rungs({"a => x"}));
    ui.click(0, at({0}));
    ui.key(ImGuiKey_N, ImGuiKey_LeftCtrl);
    ui.type("ready");
    ui.key(ImGuiKey_Enter);
    REQUIRE(ui.editor().rungs().size() == 2);
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.editor().rungs().size() == 1);
    ui.key(ImGuiKey_Y, ImGuiKey_LeftCtrl);
    REQUIRE(ui.editor().rungs().size() == 2);
    CHECK(ui.rung(1) == "=> ready");
}

TEST_CASE("Deleting a network can be undone") {
    Ui ui(rungs({"a => x", "b => y"}));
    ui.clickNetworkBackground(0);
    ui.key(ImGuiKey_Delete);
    REQUIRE(ui.editor().rungs().size() == 1);
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    REQUIRE(ui.editor().rungs().size() == 2);
    CHECK(ui.rung(0) == "a => x");
}

TEST_CASE("The unsaved mark follows the history") {
    const auto path = std::filesystem::temp_directory_path() / "ladder_ui_test_modified.lad";
    std::filesystem::remove(path);
    Ui ui(rungs({"a b => x"}), path.string());
    ui.click(0, at({1}));
    ui.key(ImGuiKey_Delete);
    ui.key(ImGuiKey_S, ImGuiKey_LeftCtrl);
    CHECK_FALSE(ui.editor().modified());
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.editor().modified()); // differs from what's saved
    ui.key(ImGuiKey_Y, ImGuiKey_LeftCtrl);
    CHECK_FALSE(ui.editor().modified()); // the saved state again
    std::filesystem::remove(path);
}

TEST_CASE("A comment typed into the field is one step to undo") {
    Ui ui(rungs({"a => x"}));
    ui.clickNetworkBackground(0);
    const auto field = ui.editor().commentFieldCenter();
    REQUIRE(field.has_value());
    ui.clickAt(*field);
    ui.type("Lamp test");
    ui.clickNetworkBackground(0); // leave the field
    CHECK(ui.editor().rungs()[0].comment == "Lamp test");
    CHECK(ui.editor().history().size() == 1);
    CHECK(ui.editor().history().undoDescription() == "edit comment");
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.editor().rungs()[0].comment.empty());
}
