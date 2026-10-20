#include "harness.hpp"
#include "ladder/project_file.hpp"

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

TEST_CASE("The conveyor example compiles, and runs in the simulator") {
    auto conveyor = loadProject(LADDER_EDITOR_SOURCE_DIR "/examples/conveyor");
    REQUIRE_MESSAGE(conveyor.has_value(), (conveyor ? "" : conveyor.error()));
    Ui ui(std::move(*conveyor), "");
    CHECK(checkProject(ui.window().project()).empty());
    CHECK_FALSE(ui.window().codeError().has_value());
    REQUIRE(ui.window().simulate());
    auto* session = ui.window().session();
    // stop and eStop are wired normally closed: their inputs are TRUE while
    // nobody presses them. The simulator's inputs start at 0, as if pressed.
    session->send("WRITE stop TRUE");
    session->send("WRITE eStop TRUE");
    session->send("WRITE start TRUE");
    CHECK(ui.waitFor([&] { return session->boolean("running") == true; }));
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

// ---------------------------------------------------------------- Upload

namespace {

const char* kPlant = "FUNCTION_BLOCK Fb\nVAR_INPUT x : BOOL; END_VAR\nEND_FUNCTION_BLOCK\n\n"
                     "PROGRAM Plant\nVAR\n  f : Fb;\n  count : INT;\nEND_VAR\n  count := count + 1;\nEND_PROGRAM\n";

// A soft_plc running kPlant, as if started on its own: the window connects
// to it like to any other.
std::unique_ptr<online::Simulator> plant() {
    auto started = online::Simulator::start(kPlant);
    REQUIRE_MESSAGE(started.has_value(), (started ? "" : started.error()));
    return std::move(*started);
}

bool showing(Ui& ui, const char* widget) { return ui.window().widgets().center(widget).has_value(); }

} // namespace

TEST_CASE("Upload takes the PLC's program: one ST block per declaration, online at once") {
    const auto plc = plant();
    Ui ui(newProject("untitled"), "");
    REQUIRE(ui.window().connect(plc->port()));
    ui.frame();
    ui.clickWidget("upload");
    REQUIRE(ui.waitFor([&] { return ui.window().project().name == "Plant"; }));
    const auto& blocks = ui.window().project().blocks;
    REQUIRE(blocks.size() == 2);
    CHECK(blocks[0].name == "Fb");
    CHECK(blocks[0].kind == BlockKind::FunctionBlock);
    CHECK(blocks[0].language == Language::St);
    CHECK(blocks[1].name == "Plant");
    CHECK(blocks[1].kind == BlockKind::Program);
    CHECK(blocks[1].language == Language::St);
    CHECK(ui.window().currentBlock() == 1);
    CHECK(ui.window().modified()); // never saved
    CHECK(ui.window().folder().empty());
    CHECK(ui.window().note() == "Uploaded 2 blocks from the PLC.");
    ui.frame();
    CHECK_FALSE(showing(ui, "download")); // the PLC runs exactly this
    auto* session = ui.window().session();
    REQUIRE(session != nullptr);
    CHECK(ui.waitFor([&] { return session->value("count").has_value(); }));
}

TEST_CASE("Upload over unsaved changes asks first") {
    const auto plc = plant();
    Ui ui(newProject("untitled"), "");
    ui.clickWidget("+ variable");
    REQUIRE(ui.window().modified());
    REQUIRE(ui.window().connect(plc->port()));
    ui.frame();
    ui.clickWidget("upload");
    ui.frame();
    REQUIRE(showing(ui, "upload cancel"));
    ui.clickWidget("upload cancel");
    CHECK_FALSE(ui.window().uploadPending());
    CHECK(ui.window().project().name == "untitled");

    ui.clickWidget("upload");
    ui.frame();
    ui.clickWidget("upload anyway");
    CHECK(ui.waitFor([&] { return ui.window().project().name == "Plant"; }));
}

TEST_CASE("Upload refuses while a download is waiting, and the other way round") {
    const auto plc = plant();
    Ui ui(newProject("untitled"), "");
    REQUIRE(ui.window().connect(plc->port()));
    ui.frame();
    CHECK(ui.window().upload());
    CHECK(ui.window().uploadPending());
    CHECK_FALSE(ui.window().download());
    CHECK_FALSE(ui.window().upload());
    REQUIRE(ui.waitFor([&] { return !ui.window().uploadPending(); }));
    REQUIRE(ui.window().project().name == "Plant");

    ui.clickWidget("source"); // Plant is open: change it, so there's something to download
    ui.key(ImGuiKey_End, ImGuiKey_LeftCtrl);
    ui.type("\n// changed");
    REQUIRE(ui.window().download());
    CHECK(ui.window().downloadPending());
    CHECK_FALSE(ui.window().upload());
}

TEST_CASE("Upload isn't offered while simulating") {
    Ui ui(newProject("untitled"), "");
    REQUIRE(ui.window().simulate());
    ui.frame();
    CHECK_FALSE(showing(ui, "upload"));
}

TEST_CASE("Saving an uploaded project doesn't make the PLC look out of date") {
    const auto folder = std::filesystem::temp_directory_path() / "ladder_online_ui_test" / "saved";
    std::filesystem::remove_all(folder);
    const auto plc = plant();
    Ui ui(newProject("untitled"), "");
    REQUIRE(ui.window().connect(plc->port()));
    ui.frame();
    ui.clickWidget("upload");
    REQUIRE(ui.waitFor([&] { return ui.window().project().name == "Plant"; }));
    ui.clickWidget("save");
    ui.key(ImGuiKey_A, ImGuiKey_LeftCtrl);
    ui.type(folder.string().c_str());
    ui.clickWidget("save here");
    REQUIRE(ui.window().folder() == folder);
    ui.frame();
    ui.frame();
    CHECK_FALSE(showing(ui, "download"));
}
