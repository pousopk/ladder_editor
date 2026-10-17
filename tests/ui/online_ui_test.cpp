#include "harness.hpp"

// Online mode: the project runs in the simulator, and the editor shows and
// changes its values.

using namespace ladder;
using test::Ui;

namespace {

Variable var(const char* name, const char* type, const char* address = "", const char* initial = "") {
    return Variable{name, type, Section::Local, initial, address, ""};
}

Project startStop() {
    Project project;
    project.name = "online";
    auto main = newBlock("Main", BlockKind::Program, Language::Kop);
    main.variables = {var("start", "BOOL", "%IX0.0"), var("stop", "BOOL", "", "TRUE"), var("motor", "BOOL", "%QX0.0")};
    main.networks = {*parseRung("(start | motor) stop => motor")};
    project.blocks.push_back(main);
    return project;
}

} // namespace

TEST_CASE("Simulate: the rungs show the PLC's values; a double-click flips an input") {
    Ui ui(startStop(), "");
    ui.clickWidget("simulate");
    auto* session = ui.window().session();
    REQUIRE(session != nullptr);
    REQUIRE(ui.waitFor([&] { return session->boolean("motor").has_value(); }));
    const auto& watched = ui.editor().watched();
    for (const char* path : {"start", "motor", "stop"}) {
        CAPTURE(path);
        CHECK(std::find(watched.begin(), watched.end(), path) != watched.end());
    }
    CHECK(session->boolean("motor") == false);

    ui.click(0, ElementPath{{0, 0, 0}}, 2); // double-click start: online, that presses it
    CHECK(ui.waitFor([&] { return session->boolean("motor") == true; }));
    CHECK(ui.editor().rungs()[0] == *parseRung("(start | motor) stop => motor")); // not edited

    ui.click(0, ElementPath{{1}}); // select stop, then force it FALSE from the details
    ui.clickWidget("force false");
    CHECK(ui.waitFor([&] { return session->boolean("motor") == false; }));
    CHECK(ui.waitFor([&] { return session->forces().size() == 1; }));
    ui.clickWidget("unforce");
    CHECK(ui.waitFor([&] { return session->forces().empty(); }));

    ui.clickWidget("go offline");
    CHECK(ui.window().session() == nullptr);
}

TEST_CASE("After a change, Download restarts the simulator with it") {
    Ui ui(startStop(), "");
    REQUIRE(ui.window().simulate());
    ui.frame();
    CHECK_FALSE(ui.window().widgets().center("download").has_value());
    ui.key(ImGuiKey_N, ImGuiKey_LeftCtrl); // a new network: the PLC now runs an older version
    ui.key(ImGuiKey_Escape);               // (closes the new network's edit popup)
    REQUIRE(ui.window().widgets().center("download").has_value());
    ui.clickWidget("download");
    CHECK_FALSE(ui.window().widgets().center("download").has_value());
    CHECK(ui.window().session() != nullptr);
}

TEST_CASE("A project with errors can't be simulated") {
    auto project = startStop();
    project.blocks[0].networks.push_back(*parseRung("start => missing"));
    Ui ui(project, "");
    CHECK_FALSE(ui.window().simulate());
    CHECK(ui.window().session() == nullptr);
}

TEST_CASE("A function block is watched through an instance; ST lines show their values") {
    Project project;
    project.name = "online";
    auto pump = newBlock("Pump", BlockKind::FunctionBlock, Language::Kop);
    pump.variables = {Variable{"demand", "BOOL", Section::Input, "", "", ""},
                      Variable{"running", "BOOL", Section::Output, "", "", ""}};
    pump.networks = {*parseRung("demand => running")};
    auto main = newBlock("Main", BlockKind::Program, Language::St);
    main.source = "PROGRAM Main\nVAR p1 : Pump; p2 : Pump; n : INT; END_VAR\n"
                  "    p1(demand := TRUE);\n    p2(demand := FALSE);\n    n := 7;\nEND_PROGRAM\n";
    project.blocks = {pump, main};
    Ui ui(project, "");
    REQUIRE(ui.window().simulate());

    REQUIRE(ui.waitFor([&] { return ui.window().instance() == "p1"; })); // the first instance
    auto* session = ui.window().session();
    CHECK(ui.waitFor([&] { return session->boolean("p1.demand") == true; })); // p1's own input
    const auto& watched = ui.editor().watched();
    CHECK(std::find(watched.begin(), watched.end(), "p1.demand") != watched.end());

    ui.clickWidget("block 1"); // the ST PROGRAM: values at the end of its lines
    const auto* code = ui.window().codeEditor(1);
    REQUIRE(code != nullptr);
    CHECK(ui.waitFor([&] { return code->notes().size() > 4 && code->notes()[4] == "n = 7"; }));
}

#include "app/example.hpp"

TEST_CASE("The built-in example compiles, and runs in the simulator") {
    Ui ui(app::exampleProject(), "");
    CHECK(checkProject(ui.window().project()).empty());
    CHECK_FALSE(ui.window().codeError().has_value());
    REQUIRE(ui.window().simulate());
    auto* session = ui.window().session();
    // stop is wired normally closed: its input is TRUE while nobody presses
    // it. The simulator's inputs start at 0, as if it were pressed.
    session->send("WRITE stop TRUE");
    session->send("WRITE start TRUE");
    CHECK(ui.waitFor([&] { return session->boolean("motor") == true; }));
}

TEST_CASE("The watch table: lines are added and edited offline, and mark the project modified") {
    Ui ui(startStop(), "");
    ui.clickWidget("watch table");
    CHECK(ui.window().showingWatchTable());
    ui.clickWidget("+ line");
    ui.clickWidget("path 0");
    ui.type("motor");
    ui.clickWidget("+ line"); // leaves the field (and adds a second line)
    const auto& watch = ui.window().project().watch;
    REQUIRE(watch.size() == 2);
    CHECK(watch[0].path == "motor");
    CHECK(ui.window().modified());
    ui.clickWidget("delete 1");
    CHECK(ui.window().project().watch.size() == 1);
    ui.clickWidget("block 0"); // back to the block
    CHECK_FALSE(ui.window().showingWatchTable());
}

TEST_CASE("The watch table online: values, Set all, Force and Unforce") {
    auto project = startStop();
    project.watch = {WatchEntry{"stop", "TRUE", ""}, WatchEntry{"START", "TRUE", "press start"},
                     WatchEntry{"motor", "", ""}, WatchEntry{"nothing", "", ""}};
    Ui ui(project, "");
    REQUIRE(ui.window().simulate());
    ui.clickWidget("watch table");
    auto* session = ui.window().session();
    REQUIRE(ui.waitFor([&] { return session->boolean("motor").has_value() && session->variablesKnown(); }));
    const auto& watched = ui.window().watchTable().watched();
    CHECK(std::find(watched.begin(), watched.end(), "start") != watched.end()); // spelled as declared
    CHECK_FALSE(session->describe("nothing").has_value());                      // shown in red

    ui.clickWidget("set all"); // stop TRUE, then start TRUE: the belt runs
    CHECK(ui.waitFor([&] { return session->boolean("motor") == true; }));

    ui.clickWidget("modify 0"); // force stop FALSE: the belt stops, and stays stopped
    ui.key(ImGuiKey_A, ImGuiKey_LeftCtrl);
    ui.type("FALSE");
    ui.clickWidget("force 0");
    CHECK(ui.waitFor([&] { return session->boolean("motor") == false; }));
    REQUIRE(ui.waitFor([&] { return session->forces().size() == 1; }));
    ui.clickWidget("unforce 0");
    CHECK(ui.waitFor([&] { return session->forces().empty(); }));
}
