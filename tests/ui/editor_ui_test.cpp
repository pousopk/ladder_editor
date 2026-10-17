#include "harness.hpp"
#include "ladder/rung_file.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

// The KOP editor's UI: selecting, editing, networks, undo and saving.

using namespace ladder;
using test::Ui;

namespace {

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

TEST_CASE("Ctrl+S saves the project folder") {
    const auto folder = std::filesystem::temp_directory_path() / "ladder_ui_test_save";
    std::filesystem::remove_all(folder);
    {
        Ui ui(rungs({"a b => x"}), folder.string());
        ui.click(0, at({1}));
        ui.key(ImGuiKey_Delete);
        CHECK(ui.window().modified());
        ui.key(ImGuiKey_S, ImGuiKey_LeftCtrl);
        CHECK_FALSE(ui.window().modified());
    }
    std::ifstream file(folder / "Main.kop.json");
    std::stringstream text;
    text << file.rdbuf();
    CHECK(text.str().find("\"rung\": \"a => x\"") != std::string::npos);
    std::filesystem::remove_all(folder);
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
    const auto path = std::filesystem::temp_directory_path() / "ladder_ui_test_modified";
    std::filesystem::remove_all(path);
    Ui ui(rungs({"a b => x"}), path.string());
    ui.click(0, at({1}));
    ui.key(ImGuiKey_Delete);
    ui.key(ImGuiKey_S, ImGuiKey_LeftCtrl);
    CHECK_FALSE(ui.editor().modified());
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl);
    CHECK(ui.editor().modified()); // differs from what's saved
    ui.key(ImGuiKey_Y, ImGuiKey_LeftCtrl);
    CHECK_FALSE(ui.editor().modified()); // the saved state again
    std::filesystem::remove_all(path);
}

TEST_CASE("A comment typed into the field is one step to undo") {
    Ui ui(rungs({"a => x"}));
    ui.clickNetworkBackground(0);
    const auto field = ui.editor().widgets().center("comment");
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
