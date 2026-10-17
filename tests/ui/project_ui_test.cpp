#include "harness.hpp"
#include "ladder/project_file.hpp"
#include <filesystem>

// The project window: blocks, the interface table, ST blocks, saving and quitting.

using namespace ladder;
using test::Ui;
namespace fs = std::filesystem;

namespace {

Project twoBlocks() {
    Project project;
    project.name = "test";
    project.blocks.push_back(newBlock("Main", BlockKind::Program, Language::Kop));
    project.blocks.push_back(newBlock("Mixer", BlockKind::FunctionBlock, Language::St));
    return project;
}

fs::path freshFolder(const char* name) {
    const auto folder = fs::temp_directory_path() / "ladder_project_ui_test" / name;
    fs::remove_all(folder);
    return folder;
}

} // namespace

TEST_CASE("Adding a variable in the interface table, editing its name, and undoing") {
    Ui ui(twoBlocks(), "");
    ui.clickWidget("+ variable");
    auto& main = ui.window().project().blocks[0];
    REQUIRE(main.variables.size() == 1);
    CHECK(main.variables[0].name == "var1");
    CHECK(main.variables[0].type == "BOOL");
    CHECK(main.variables[0].section == Section::Local); // a PROGRAM only has VAR

    ui.clickWidget("name 0");
    ui.key(ImGuiKey_A, ImGuiKey_LeftCtrl); // select the old name
    ui.type("start");
    ui.clickWidget("+ variable"); // leaving the cell ends the edit (and adds another variable)
    REQUIRE(main.variables.size() == 2);
    CHECK(main.variables[0].name == "start");

    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl); // the second variable
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl); // the rename, as one step
    REQUIRE(main.variables.size() == 1);
    CHECK(main.variables[0].name == "var1");
}

TEST_CASE("Creating a block with the New block popup") {
    Ui ui(twoBlocks(), "");
    ui.clickWidget("new block");
    ui.type("Conveyor");
    ui.key(ImGuiKey_Enter);
    const auto& project = ui.window().project();
    REQUIRE(project.blocks.size() == 3);
    CHECK(project.blocks[2].name == "Conveyor");
    CHECK(project.blocks[2].language == Language::Kop);
    CHECK(ui.window().currentBlock() == 2); // and it opens
    CHECK(ui.window().modified());
    // The popup's default kind is PROGRAM, so now there are two: the project says so.
    CHECK(checkProject(project).at(0).message == "the project can only have one PROGRAM");
}

TEST_CASE("A block name that's taken or invalid can't be created") {
    Ui ui(twoBlocks(), "");
    ui.clickWidget("new block");
    ui.type("mixer"); // like Mixer
    ui.key(ImGuiKey_Enter);
    CHECK(ui.window().project().blocks.size() == 2);
    ui.key(ImGuiKey_Escape);
}

TEST_CASE("Renaming and deleting a block") {
    Ui ui(twoBlocks(), "");
    ui.clickWidget("block 1");
    CHECK(ui.window().currentBlock() == 1);
    ui.clickWidget("rename");
    ui.key(ImGuiKey_A, ImGuiKey_LeftCtrl);
    ui.type("Blender");
    ui.key(ImGuiKey_Enter);
    CHECK(ui.window().project().blocks[1].name == "Blender");

    ui.clickWidget("delete");
    ui.key(ImGuiKey_Enter); // confirm
    REQUIRE(ui.window().project().blocks.size() == 1);
    CHECK(ui.window().currentBlock() == 0);
}

TEST_CASE("Editing an ST block's text, and saving the whole project") {
    const auto folder = freshFolder("st");
    Ui ui(twoBlocks(), folder.string());
    ui.clickWidget("block 1");
    ui.clickWidget("source");
    ui.key(ImGuiKey_End, ImGuiKey_LeftCtrl); // to the end of the text
    ui.type("// mixer logic comes here");
    CHECK(ui.window().modified());
    ui.key(ImGuiKey_S, ImGuiKey_LeftCtrl); // saves even while typing in the text
    CHECK_FALSE(ui.window().modified());

    const auto loaded = loadProject(folder);
    REQUIRE(loaded.has_value());
    CHECK(loaded->blocks[1].source.find("// mixer logic comes here") != std::string::npos);
    CHECK(loaded->blocks[0] == ui.window().project().blocks[0]);
}

TEST_CASE("Quitting with unsaved changes asks first") {
    Ui ui(twoBlocks(), "");
    ui.clickWidget("+ variable");
    REQUIRE(ui.window().modified());
    ui.window().askToClose();
    ui.frame();
    ui.frame();
    CHECK_FALSE(ui.window().closeConfirmed());
    ui.clickWidget("quit without saving");
    CHECK(ui.window().closeConfirmed());
}

// ---------------------------------------------------------------- The ST code editor

namespace {

// A project with a KOP PROGRAM and one ST function block with this text.
Project withSt(std::string source) {
    auto project = twoBlocks();
    project.blocks[1].source = std::move(source);
    return project;
}

const ladder::Block& mixer(Ui& ui) {
    return ui.window().project().blocks[1];
}

} // namespace

TEST_CASE("Typing in the code editor: Enter keeps the indentation") {
    Ui ui(withSt("FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nVAR_OUTPUT on : BOOL; END_VAR\n    on := run;\n"
                 "END_FUNCTION_BLOCK"),
          "");
    ui.clickWidget("block 1");
    ui.clickWidget("source");
    ui.key(ImGuiKey_Home, ImGuiKey_LeftCtrl); // to the start of the text
    for (int i = 0; i < 3; ++i) {
        ui.key(ImGuiKey_DownArrow);
    }
    ui.key(ImGuiKey_End); // the end of "    on := run;"
    ui.key(ImGuiKey_Enter);
    ui.type("IF NOT run THEN on := FALSE; END_IF;");
    CHECK(mixer(ui).source ==
          "FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nVAR_OUTPUT on : BOOL; END_VAR\n    on := run;\n"
          "    IF NOT run THEN on := FALSE; END_IF;\nEND_FUNCTION_BLOCK");
    CHECK_FALSE(ui.window().stDiagnostic().has_value());
}

TEST_CASE("An error shows up while typing, with soft_plc's message, and goes away with undo") {
    Ui ui(withSt("FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nEND_FUNCTION_BLOCK"), "");
    ui.clickWidget("block 1");
    ui.clickWidget("source");
    ui.key(ImGuiKey_Home, ImGuiKey_LeftCtrl);
    ui.key(ImGuiKey_DownArrow);
    ui.key(ImGuiKey_End);
    ui.key(ImGuiKey_Enter);
    ui.type("run := 5;");
    const auto& diagnostic = ui.window().stDiagnostic();
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->block == 1);
    CHECK(diagnostic->line == 2);
    CHECK(diagnostic->column == 7);
    CHECK(diagnostic->message == "can't assign integer literal to 'run', which is BOOL");

    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl); // the typing, as one step
    ui.key(ImGuiKey_Z, ImGuiKey_LeftCtrl); // the new line
    CHECK(mixer(ui).source == "FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nEND_FUNCTION_BLOCK");
    CHECK_FALSE(ui.window().stDiagnostic().has_value());
}

TEST_CASE("Select all, copy and paste in the code editor") {
    Ui ui(withSt("FUNCTION_BLOCK Mixer\nEND_FUNCTION_BLOCK"), "");
    ui.clickWidget("block 1");
    ui.clickWidget("source");
    ui.key(ImGuiKey_A, ImGuiKey_LeftCtrl);
    ui.key(ImGuiKey_C, ImGuiKey_LeftCtrl);
    ui.key(ImGuiKey_End, ImGuiKey_LeftCtrl);
    ui.key(ImGuiKey_Enter);
    ui.key(ImGuiKey_V, ImGuiKey_LeftCtrl);
    CHECK(mixer(ui).source == "FUNCTION_BLOCK Mixer\nEND_FUNCTION_BLOCK\nFUNCTION_BLOCK Mixer\nEND_FUNCTION_BLOCK");
    // Two function blocks in one block: the editor says so.
    REQUIRE(ui.window().stDiagnostic().has_value());
    CHECK(ui.window().stDiagnostic()->message ==
          "a block holds just its own FUNCTION_BLOCK: put this FUNCTION_BLOCK in a block of its own");
}

TEST_CASE("Clicking the ST problem opens its block") {
    Ui ui(withSt("FUNCTION_BLOCK Blender\nEND_FUNCTION_BLOCK"), ""); // the block is called Mixer
    REQUIRE(ui.window().stDiagnostic().has_value());
    CHECK(ui.window().currentBlock() == 0);
    ui.clickWidget("st problem");
    CHECK(ui.window().currentBlock() == 1);
}
