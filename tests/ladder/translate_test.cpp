#include "ladder/notation.hpp"
#include "ladder/translate.hpp"
#include <doctest/doctest.h>
#include <string>

using namespace ladder;

// The text the translator writes for each kind of rung. That the text does
// what the rung does is checked by running it in soft_plc (tests/st).

namespace {

Block program(std::vector<std::string> rungs, std::vector<Variable> variables = {}) {
    Block block;
    block.name = "Main";
    block.variables = std::move(variables);
    for (const auto& text : rungs) {
        auto rung = parseRung(text);
        REQUIRE_MESSAGE(rung.has_value(), text);
        block.networks.push_back(std::move(*rung));
    }
    return block;
}

// Just the statements of a one-network block, without the comment line.
std::string body(const std::string& rung) {
    const auto text = translateBlock(program({rung})).text;
    const auto start = text.find("// Network 1");
    REQUIRE(start != std::string::npos);
    const auto from = text.find('\n', start) + 1;
    const auto to = text.find("\nEND_PROGRAM");
    return text.substr(from, to - from - 1); // without the empty line before END_PROGRAM
}

} // namespace

TEST_CASE("Series is AND, parallel is OR, normally closed is NOT") {
    CHECK(body("a b => x") == "    x := a AND b;");
    CHECK(body("a /b => x") == "    x := a AND NOT b;");
    CHECK(body("(start | motor) stop => motor") == "    motor := (start OR motor) AND stop;");
    CHECK(body("a (b c | d) => x") == "    x := a AND (b AND c OR d);");
    CHECK(body("(a | /b) => x") == "    x := a OR NOT b;");
}

TEST_CASE("Coils: negated, set, reset; an empty rung is always powered") {
    CHECK(body("a => /x") == "    x := NOT a;");
    CHECK(body("a b => S:x") == "    IF a AND b THEN x := TRUE; END_IF;");
    CHECK(body("a => R:x") == "    IF a THEN x := FALSE; END_IF;");
    CHECK(body("=> x") == "    x := TRUE;");
}

TEST_CASE("Several coils share the power through a generated variable") {
    CHECK(body("a b => x, /y") == "    _kop_power1 := a AND b;\n"
                                  "    x := _kop_power1;\n"
                                  "    y := NOT _kop_power1;");
    CHECK(body("a => x, y") == "    x := a;\n    y := a;"); // a name is used as it is
}

TEST_CASE("A box is a call; the power goes on from its power output") {
    CHECK(body("a {TON t1, PT := T#5s, ET => elapsed} => lamp") == "    t1(IN := a, PT := T#5s);\n"
                                                                   "    elapsed := t1.ET;\n"
                                                                   "    lamp := t1.Q;");
    CHECK(body("{R_TRIG edge} => S:latched") == "    edge(CLK := TRUE);\n"
                                                "    IF edge.Q THEN latched := TRUE; END_IF;");
}

TEST_CASE("Boxes on a branch get the power that enters the branch") {
    CHECK(body("a b ({TON t1, PT := T#1s} | c) => x") == "    _kop_power1 := a AND b;\n"
                                                         "    t1(IN := _kop_power1, PT := T#1s);\n"
                                                         "    x := t1.Q OR _kop_power1 AND c;");
}

TEST_CASE("Addresses get generated AT variables") {
    const auto text = translateBlock(program({"%IX0.0 /%I0.1 => %QX0.0", "%IX0.0 => x"})).text;
    CHECK(text.find("    _kop_IX0_0 AT %IX0.0 : BOOL;") != std::string::npos);
    CHECK(text.find("    _kop_I0_1 AT %I0.1 : BOOL;") != std::string::npos);
    CHECK(text.find("    _kop_QX0_0 := _kop_IX0_0 AND NOT _kop_I0_1;") != std::string::npos);
    CHECK(text.find("    x := _kop_IX0_0;") != std::string::npos);
}

TEST_CASE("The whole block: interface, networks with their comments, line origins") {
    auto block = program({"(start | running) stop => running", "running => %QX0.0"},
                         {Variable{"start", "BOOL", Section::Local, "", "%IX0.0", "start button"},
                          Variable{"stop", "BOOL", Section::Local, "", "%IX0.1", "wired *) closed"},
                          Variable{"running", "BOOL", Section::Local, "FALSE", "", ""}});
    block.networks[0].comment = "Start/stop\nstop wins";
    const auto translated = translateBlock(block);
    CHECK(translated.text == "PROGRAM Main\n"
                             "VAR\n"
                             "    start AT %IX0.0 : BOOL; (* start button *)\n"
                             "    stop AT %IX0.1 : BOOL; (* wired * ) closed *)\n"
                             "    running : BOOL := FALSE;\n"
                             "    _kop_QX0_0 AT %QX0.0 : BOOL; (* generated: the operand %QX0.0 *)\n"
                             "END_VAR\n"
                             "\n"
                             "    // Network 1: Start/stop\n"
                             "    // stop wins\n"
                             "    running := (start OR running) AND stop;\n"
                             "\n"
                             "    // Network 2\n"
                             "    _kop_QX0_0 := running;\n"
                             "\n"
                             "END_PROGRAM\n");
    CHECK(translated.problems.empty());
    CHECK(translated.networkAt(0) == std::nullopt);
    CHECK(translated.networkAt(8) == 0u);
    CHECK(translated.networkAt(10) == 0u);
    CHECK(translated.networkAt(12) == 1u);
    CHECK(translated.networkAt(15) == std::nullopt);
}

TEST_CASE("Networks that can't be translated are left out, with the reason") {
    auto block = program({"a => x", "? => y", "a => z"});
    block.kind = BlockKind::FunctionBlock;
    block.name = "Fb";
    block.networks.push_back(*parseRung("%IX0.0 => x"));
    const auto translated = translateBlock(block);
    REQUIRE(translated.problems.size() == 2);
    CHECK(translated.problems[0].network == 1);
    CHECK(translated.problems[1].network == 3);
    CHECK(translated.problems[1].message.find("only be used in the PROGRAM") != std::string::npos);
    CHECK(translated.text.find("    // (not translated: ") != std::string::npos);
    CHECK(translated.text.find("    z := a;") != std::string::npos);
    CHECK(translated.text.rfind("FUNCTION_BLOCK Fb\n", 0) == 0);
}

TEST_CASE("A FUNCTION has its return type; the project puts the blocks one after the other") {
    Project project;
    project.name = "p";
    Block f;
    f.name = "Both";
    f.kind = BlockKind::Function;
    f.returnType = "BOOL";
    f.variables = {Variable{"a", "BOOL", Section::Input, "", "", ""},
                   Variable{"b", "BOOL", Section::Input, "", "", ""}};
    f.networks.push_back(*parseRung("a b => Both"));
    project.blocks.push_back(f);
    Block st;
    st.name = "Main";
    st.language = Language::St;
    st.source = "PROGRAM Main\nEND_PROGRAM";
    project.blocks.push_back(st);

    const auto translated = translateProject(project);
    CHECK(translated.text.rfind("FUNCTION Both : BOOL\nVAR_INPUT\n    a : BOOL;\n", 0) == 0);
    REQUIRE(translated.parts.size() == 2);
    const auto stStart = translated.parts[1].firstLine;
    CHECK(translated.blockAt(stStart + 1) == std::pair<std::size_t, std::size_t>{1, 1});
    CHECK(translated.blockAt(2) == std::pair<std::size_t, std::size_t>{0, 2});
    CHECK(translated.text.substr(translated.text.size() - 25) == "PROGRAM Main\nEND_PROGRAM\n");
}
