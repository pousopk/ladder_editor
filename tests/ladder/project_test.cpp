#include "ladder/block.hpp"
#include "ladder/check.hpp"
#include "ladder/notation.hpp"
#include "ladder/project_file.hpp"
#include <doctest/doctest.h>
#include <fstream>

using namespace ladder;
namespace fs = std::filesystem;

namespace {

// A fresh, empty folder under the system's temporary directory.
fs::path freshFolder(const char* name) {
    const auto folder = fs::temp_directory_path() / "ladder_project_test" / name;
    fs::remove_all(folder);
    return folder;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

std::string read(const fs::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

Project sample() {
    Project project;
    project.name = "conveyor";
    auto main = newBlock("Main", BlockKind::Program, Language::Kop);
    main.variables = {Variable{"start", "BOOL", Section::Local, "", "%IX0.0", "start button"},
                      Variable{"motor", "BOOL", Section::Local, "", "%QX0.0", ""},
                      Variable{"starter", "Starter", Section::Local, "", "", ""}};
    main.networks = {parseRung("(start | motor) stop => motor").value()};
    main.networks[0].comment = "Motor starter";
    project.blocks.push_back(main);
    auto starter = newBlock("Starter", BlockKind::FunctionBlock, Language::St);
    project.blocks.push_back(starter);
    auto scale = newBlock("Scale", BlockKind::Function, Language::Kop);
    scale.returnType = "REAL";
    scale.variables = {Variable{"raw", "INT", Section::Input, "", "", ""}};
    project.blocks.push_back(scale);
    return project;
}

std::vector<std::string> messages(const Project& project) {
    std::vector<std::string> result;
    for (const auto& problem : checkProject(project)) {
        result.push_back((problem.block.empty() ? "" : problem.block + ": ") + problem.message);
    }
    return result;
}

} // namespace

TEST_CASE("New blocks start with something to work on") {
    const auto kop = newBlock("Main", BlockKind::Program, Language::Kop);
    REQUIRE(kop.networks.size() == 1);
    CHECK(formatRung(kop.networks[0]) == "=> ?"); // one coil, its operand still to be filled in
    const auto st = newBlock("Mixer", BlockKind::FunctionBlock, Language::St);
    CHECK(st.source.rfind("FUNCTION_BLOCK Mixer\n", 0) == 0);
    CHECK(newBlock("Scale", BlockKind::Function, Language::Kop).returnType == "BOOL");
}

TEST_CASE("A project saves to a folder and loads back the same") {
    const auto folder = freshFolder("round_trip");
    const auto project = sample();
    REQUIRE(saveProject(project, folder).has_value());
    CHECK(fs::exists(folder / "project.json"));
    CHECK(fs::exists(folder / "Main.kop.json"));
    CHECK(fs::exists(folder / "Starter.st"));
    CHECK_FALSE(fs::exists(folder / "Main.kop.json.tmp")); // the temporary files were renamed away

    const auto loaded = loadProject(folder);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? "" : loaded.error()));
    CHECK(*loaded == project);

    // The KOP file keeps each network as readable notation.
    const auto kop = read(folder / "Main.kop.json");
    CHECK(kop.find("\"rung\": \"(start | motor) stop => motor\"") != std::string::npos);
    CHECK(kop.find("\"address\": \"%IX0.0\"") != std::string::npos);
    CHECK(kop.find("\"initial\"") == std::string::npos); // empty fields aren't written
    // And the ST block is plain text.
    CHECK(read(folder / "Starter.st") == project.blocks[1].source);
}

TEST_CASE("Saving removes the files of blocks that were deleted or renamed") {
    const auto folder = freshFolder("stale");
    auto project = sample();
    REQUIRE(saveProject(project, folder).has_value());
    write(folder / "notes.txt", "mine"); // not a block file: must survive

    project.blocks[1].name = "Starter2"; // renamed
    project.blocks.pop_back();           // Scale deleted
    REQUIRE(saveProject(project, folder).has_value());
    CHECK(fs::exists(folder / "Starter2.st"));
    CHECK_FALSE(fs::exists(folder / "Starter.st"));
    CHECK_FALSE(fs::exists(folder / "Scale.kop.json"));
    CHECK(fs::exists(folder / "notes.txt"));
}

TEST_CASE("Load errors say which file and what") {
    CHECK(loadProject(freshFolder("missing")).error() == "can't read project.json");

    const auto broken = freshFolder("broken");
    write(broken / "project.json", "{ \"format\": 1, \"blocks\": [ }");
    CHECK(loadProject(broken).error().rfind("project.json isn't valid JSON:", 0) == 0);

    const auto future = freshFolder("future");
    write(future / "project.json", "{ \"format\": 9 }");
    CHECK(loadProject(future).error() == "project.json: unknown format 9 (this editor reads format 1)");

    const auto badRung = freshFolder("bad_rung");
    write(badRung / "project.json", R"({ "format": 1, "blocks": [
        { "name": "Main", "kind": "PROGRAM", "language": "KOP", "file": "Main.kop.json" } ] })");
    write(badRung / "Main.kop.json", R"({ "networks": [ { "rung": "a => x" }, { "rung": "(a | b => x" } ] })");
    CHECK(loadProject(badRung).error() ==
          "Main.kop.json, network 2: column 8: expected ')' to close the parallel branch, found '=>'");

    const auto badSection = freshFolder("bad_section");
    write(badSection / "project.json", R"({ "format": 1, "blocks": [
        { "name": "Main", "kind": "PROGRAM", "language": "KOP", "file": "Main.kop.json" } ] })");
    write(badSection / "Main.kop.json",
          R"({ "interface": [ { "name": "x", "type": "INT", "section": "VAR_TEMP" } ] })");
    CHECK(loadProject(badSection).error() == "Main.kop.json: 'x' has an unknown section 'VAR_TEMP'");

    const auto noKind = freshFolder("no_kind");
    write(noKind / "project.json",
          R"({ "format": 1, "blocks": [ { "name": "Main", "language": "KOP", "file": "x" } ] })");
    CHECK(loadProject(noKind).error() == "project.json: 'kind' is missing");
}

TEST_CASE("Project rules") {
    CHECK(messages(sample()).empty());

    auto twoPrograms = sample();
    twoPrograms.blocks.push_back(newBlock("Second", BlockKind::Program, Language::St));
    CHECK(messages(twoPrograms) == std::vector<std::string>{"the project can only have one PROGRAM"});

    auto noProgram = sample();
    noProgram.blocks.erase(noProgram.blocks.begin());
    CHECK(messages(noProgram) ==
          std::vector<std::string>{"the project needs a PROGRAM: the block the PLC runs every cycle"});

    auto names = sample();
    names.blocks[2].name = "starter"; // the same as Starter, ignoring case
    names.blocks[1].variables.clear();
    CHECK(messages(names) == std::vector<std::string>{"starter: there's already a block called 'starter'"});

    auto bad = sample();
    bad.blocks[0].variables.push_back(Variable{"2fast", "BOOL", Section::Local, "", "", ""});
    bad.blocks[0].variables.push_back(Variable{"START", "INT", Section::Input, "", "", ""});
    bad.blocks[0].variables.push_back(Variable{"_KOP_power1", "BOOL", Section::Local, "", "", ""});
    bad.blocks[2].variables.push_back(Variable{"lamp", "", Section::Output, "", "%QX0.1", ""});
    bad.blocks[2].variables.push_back(Variable{"Scale", "REAL", Section::Local, "", "", ""});
    bad.blocks.push_back(newBlock("Types", BlockKind::Types, Language::Kop));
    CHECK(messages(bad) ==
          std::vector<std::string>{
              "Main: '2fast' isn't a valid variable name",
              "Main: 'START' is declared twice",
              "Main: a PROGRAM has no VAR_INPUT ('START')",
              "Main: '_KOP_power1': names starting with _kop_ are kept for the code generated from KOP",
              "Scale: 'lamp' has no type",
              "Scale: a FUNCTION has no VAR_OUTPUT ('lamp')",
              "Scale: 'lamp': AT addresses are only allowed in the PROGRAM",
              "Scale: 'Scale' is the function's own name, which holds its result",
              "Types: a TYPE block can only be written in ST",
          });
}

TEST_CASE("The example project loads, has no problems, and saves back unchanged") {
    const fs::path example = fs::path(LADDER_EDITOR_SOURCE_DIR) / "examples" / "conveyor";
    const auto project = loadProject(example);
    REQUIRE_MESSAGE(project.has_value(), (project ? "" : project.error()));
    CHECK(checkProject(*project).empty());
    // BoxRate is the project's ST block: its box type comes from its text
    // (st_support::projectBoxes, tested in tests/st); here it's written out.
    BoxCatalog boxes;
    boxes.add(BoxType{"BoxRate", "arrived", "tooFast", {"limit"}, {"perMinute"}});
    for (const auto& rung : project->blocks[0].networks) {
        CHECK(check(rung, boxes).empty());
    }
    // Saving it again writes exactly the same files: the example is in the editor's own format.
    const auto copy = freshFolder("example_copy");
    REQUIRE(saveProject(*project, copy).has_value());
    for (const char* file : {"project.json", "Main.kop.json", "BoxRate.st"}) {
        CAPTURE(file);
        CHECK(read(copy / file) == read(example / file));
    }
}
