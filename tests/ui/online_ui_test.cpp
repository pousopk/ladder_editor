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

TEST_CASE("Download loads a change into the running PLC and keeps its values; Restart starts over") {
    // A counter that counts every other cycle.
    Project project;
    project.name = "counting";
    auto main = newBlock("Main", BlockKind::Program, Language::Kop);
    main.variables = {var("pulse", "BOOL"), var("c", "CTU"), var("count", "INT"), var("full", "BOOL")};
    main.networks = {*parseRung("/pulse => pulse"), *parseRung("pulse {CTU c, PV := 30000, CV => count} => full")};
    project.blocks.push_back(main);
    Ui ui(project, "");
    REQUIRE(ui.window().simulate());
    auto* session = ui.window().session();
    session->watch({"count"});
    REQUIRE(ui.waitFor([&] { return session->value("count") && std::stoi(*session->value("count")) > 20; }));
    CHECK_FALSE(ui.window().widgets().center("download").has_value()); // nothing changed yet

    ui.clickWidget("+ variable"); // a change: the PLC now runs an older version
    REQUIRE(ui.window().widgets().center("download").has_value());
    ui.clickWidget("download");
    REQUIRE(ui.waitFor([&] { return !ui.window().downloadPending(); }));
    CHECK(ui.window().note().find("these start at their initial value: var1") != std::string::npos);
    CHECK_FALSE(ui.window().widgets().center("download").has_value());
    const auto counted = [&] { return std::stoi(session->value("count").value_or("0")); };
    CHECK(ui.waitFor([&] { return counted() > 20; })); // still counting from where it was
    CHECK(session == ui.window().session());           // the same PLC: no restart

    ui.clickWidget("restart"); // a cold restart: a new simulator, every value from the start
    auto* restarted = ui.window().session();
    REQUIRE(restarted != nullptr);
    restarted->watch({"count"});
    REQUIRE(ui.waitFor([&] { return restarted->value("count").has_value(); }));
    CHECK(std::stoi(*restarted->value("count")) < 20);
}

TEST_CASE("Download refuses a project with errors") {
    Ui ui(startStop(), "");
    REQUIRE(ui.window().simulate());
    ui.clickWidget("block 0");
    ui.key(ImGuiKey_N, ImGuiKey_LeftCtrl);
    ui.type("missing"); // the new network's coil: an undeclared variable
    ui.key(ImGuiKey_Enter);
    ui.frame();
    REQUIRE(ui.window().codeError().has_value());
    CHECK_FALSE(ui.window().download());
    CHECK(ui.window().status().find("fix the errors first") != std::string::npos);
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

TEST_CASE("Text too long for its place is cut, never in the middle of a character") {
    Ui ui(startStop(), ""); // an ImGui context, for the font
    CHECK(view::fitText("short", 1000.0F) == "short");
    const auto cut = view::fitText("elapsedTravelTime", 60.0F);
    CHECK(cut.size() >= 3);
    CHECK(cut.substr(cut.size() - 2) == "..");
    CHECK(ImGui::CalcTextSize(cut.c_str()).x <= 60.0F);
    const auto umlauts = view::fitText("öööööööööööö", 40.0F);
    CHECK(umlauts.substr(umlauts.size() - 2) == "..");
    CHECK((umlauts.size() - 2) % 2 == 0); // whole two-byte characters only
}
