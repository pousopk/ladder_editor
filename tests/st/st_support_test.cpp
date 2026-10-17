#include "ladder/notation.hpp"
#include "st_support/diagnostics.hpp"
#include "st_support/highlight.hpp"
#include <algorithm>
#include <doctest/doctest.h>
#include <string>

using namespace st_support;
using ladder::BlockKind;
using ladder::Language;

namespace {

// The text of each span, with its style, for readable checks.
std::vector<std::pair<std::string, Style>> styled(std::string_view source) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const auto end = source.find('\n', start);
        lines.emplace_back(source.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    std::vector<std::pair<std::string, Style>> result;
    for (const auto& span : highlight(source)) {
        result.emplace_back(lines[span.line].substr(span.column, span.length), span.style);
    }
    return result;
}

ladder::Block st(const char* name, BlockKind kind, std::string source) {
    auto block = ladder::newBlock(name, kind, Language::St);
    block.source = std::move(source);
    return block;
}

} // namespace

TEST_CASE("Keywords, types, built-ins, literals, addresses and operators") {
    const auto spans = styled("motor AT %QX0.0 : BOOL := TRUE;\nIF t1.Q THEN x := MAX(16#FF, 2.5); END_IF;");
    using P = std::pair<std::string, Style>;
    CHECK(spans ==
          std::vector<P>{
              {"AT", Style::Keyword},  {"%QX0.0", Style::Address}, {":", Style::Operator},     {"BOOL", Style::Type},
              {":=", Style::Operator}, {"TRUE", Style::Literal},   {";", Style::Operator},     {"IF", Style::Keyword},
              {".", Style::Operator},  {"THEN", Style::Keyword},   {":=", Style::Operator},    {"MAX", Style::Builtin},
              {"(", Style::Operator},  {"16#FF", Style::Number},   {",", Style::Operator},     {"2.5", Style::Number},
              {")", Style::Operator},  {";", Style::Operator},     {"END_IF", Style::Keyword}, {";", Style::Operator},
          });
    // motor, t1, Q and x are plain names: no span.
}

TEST_CASE("Comments, on one line or over several") {
    const auto spans = styled("a := 1; // set a\n(* two\n   lines *) b := T#2s;");
    using P = std::pair<std::string, Style>;
    CHECK(spans == std::vector<P>{
                       {":=", Style::Operator},
                       {"1", Style::Number},
                       {";", Style::Operator},
                       {"// set a", Style::Comment},
                       {"(* two", Style::Comment},
                       {"lines *)", Style::Comment},
                       {":=", Style::Operator},
                       {"T#2s", Style::Time},
                       {";", Style::Operator},
                   });
}

TEST_CASE("Standard blocks and case don't matter") {
    const auto spans = styled("t : ton; c : Ctu; x := real_to_int(r);");
    CHECK(std::count(spans.begin(), spans.end(), std::pair<std::string, Style>{"ton", Style::Builtin}) == 1);
    CHECK(std::count(spans.begin(), spans.end(), std::pair<std::string, Style>{"Ctu", Style::Builtin}) == 1);
    CHECK(std::count(spans.begin(), spans.end(), std::pair<std::string, Style>{"real_to_int", Style::Builtin}) == 1);
}

TEST_CASE("Colours go on after a character the lexer can't read") {
    const auto spans = styled("a := $ IF;");
    CHECK(spans.back() == std::pair<std::string, Style>{";", Style::Operator});
    CHECK(std::count(spans.begin(), spans.end(), std::pair<std::string, Style>{"IF", Style::Keyword}) == 1);
}

TEST_CASE("An unclosed comment colours the rest as a comment") {
    const auto spans = styled("a := 1; (* not closed yet\nIF b THEN");
    using P = std::pair<std::string, Style>;
    CHECK(spans == std::vector<P>{
                       {":=", Style::Operator},
                       {"1", Style::Number},
                       {";", Style::Operator},
                       {"(* not closed yet", Style::Comment},
                       {"IF b THEN", Style::Comment},
                   });
}

// ---------------------------------------------------------------- Diagnostics

TEST_CASE("Correct blocks have no diagnostic, KOP and ST together") {
    ladder::Project project;
    project.blocks.push_back(ladder::newBlock("Main", BlockKind::Program, Language::Kop));
    project.blocks.push_back(st("Mixer", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nVAR_OUTPUT on : BOOL; END_VAR\n"
                                "on := run;\nEND_FUNCTION_BLOCK\n"));
    CHECK_FALSE(checkCode(project).error.has_value());
    project.blocks.push_back(ladder::newBlock("Scale", BlockKind::Function, Language::St)); // the skeleton compiles too
    CHECK_FALSE(checkCode(project).error.has_value());
}

TEST_CASE("An error is reported in its block, at its line and column, with soft_plc's message") {
    ladder::Project project;
    project.blocks.push_back(st("Mixer", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK Mixer\nVAR_INPUT run : BOOL; END_VAR\nEND_FUNCTION_BLOCK\n"));
    project.blocks.push_back(st("Main", BlockKind::Program,
                                "PROGRAM Main\nVAR m : Mixer; n : INT; END_VAR\n    m(run := TRUE);\n"
                                "    n := m.run + 1;\nEND_PROGRAM\n"));
    const auto diagnostic = checkCode(project).error;
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->block == 1);
    CHECK(diagnostic->line == 3);    // the 4th line of Main
    CHECK(diagnostic->column == 15); // at the '+'
    CHECK(diagnostic->message == "'+' needs numbers, but got BOOL and integer literal");
}

TEST_CASE("A block's text must declare the block the project says it is") {
    ladder::Project project;
    project.blocks.push_back(st("Mixer", BlockKind::FunctionBlock, "FUNCTION_BLOCK Blender\nEND_FUNCTION_BLOCK\n"));
    auto diagnostic = checkCode(project).error;
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->message ==
          "the text declares FUNCTION_BLOCK Blender, but the block is called Mixer: rename one of them");
    CHECK(diagnostic->column == 15);

    project.blocks[0].source = "PROGRAM Mixer\nEND_PROGRAM\n";
    CHECK(checkCode(project).error->message ==
          "block Mixer is a FUNCTION_BLOCK: its text should start with FUNCTION_BLOCK Mixer");

    project.blocks[0].source = "FUNCTION_BLOCK Mixer\nEND_FUNCTION_BLOCK\nFUNCTION_BLOCK Other\nEND_FUNCTION_BLOCK\n";
    diagnostic = checkCode(project).error;
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->message ==
          "a block holds just its own FUNCTION_BLOCK: put this FUNCTION_BLOCK in a block of its own");
    CHECK(diagnostic->line == 2);
}

TEST_CASE("Types from a TYPE block are known to the other ST blocks") {
    ladder::Project project;
    project.blocks.push_back(
        st("Shapes", BlockKind::Types, "TYPE\n    Point : STRUCT x : REAL; y : REAL; END_STRUCT;\nEND_TYPE\n"));
    project.blocks.push_back(
        st("Main", BlockKind::Program, "PROGRAM Main\nVAR p : Point; END_VAR\n    p.x := 1.0;\nEND_PROGRAM\n"));
    CHECK_FALSE(checkCode(project).error.has_value());
    project.blocks[1].source = "PROGRAM Main\nVAR p : Point; END_VAR\n    p.z := 1.0;\nEND_PROGRAM\n";
    const auto diagnostic = checkCode(project).error;
    REQUIRE(diagnostic.has_value());
    CHECK(diagnostic->block == 1);
    CHECK(diagnostic->message == "Point has no field 'z'");
}

TEST_CASE("The project's function blocks are boxes: KOP ones from their interface, ST ones from their text") {
    ladder::Project project;
    auto valve = ladder::newBlock("Valve", BlockKind::FunctionBlock, Language::Kop);
    valve.variables = {{"open", "BOOL", ladder::Section::Input, "", "", ""},
                       {"delay", "TIME", ladder::Section::Input, "", "", ""},
                       {"isOpen", "BOOL", ladder::Section::Output, "", "", ""},
                       {"timer", "TON", ladder::Section::Local, "", "", ""}};
    project.blocks.push_back(valve);
    project.blocks.push_back(st("Rate", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK Rate\nVAR_INPUT pulse : BOOL; END_VAR\n"
                                "VAR_OUTPUT perMinute : INT; END_VAR\nEND_FUNCTION_BLOCK\n"));
    project.blocks.push_back(st("NoPower", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK NoPower\nVAR_INPUT x : INT; END_VAR\nEND_FUNCTION_BLOCK\n"));

    const auto boxes = projectBoxes(project);
    const auto* kop = boxes.find("VALVE");
    REQUIRE(kop != nullptr);
    CHECK(kop->powerInput == "open");
    CHECK(kop->powerOutput == "isOpen");
    CHECK(kop->inputs == std::vector<std::string>{"delay"});
    const auto* rate = boxes.find("Rate");
    REQUIRE(rate != nullptr);
    CHECK(rate->powerInput == "pulse");
    CHECK(rate->powerOutput.empty()); // the power passes through
    CHECK(rate->outputs == std::vector<std::string>{"perMinute"});
    CHECK(boxes.find("NoPower") == nullptr);
    CHECK(boxes.find("TON") != nullptr);
}

TEST_CASE("Errors in KOP blocks are traced back to their network") {
    ladder::Project project;
    auto main = ladder::newBlock("Main", BlockKind::Program, Language::Kop);
    main.variables = {{"a", "BOOL", ladder::Section::Local, "", "", ""},
                      {"count", "INT", ladder::Section::Local, "", "", ""},
                      {"r", "Rate", ladder::Section::Local, "", "", ""}};
    main.networks = {*ladder::parseRung("a => a"), *ladder::parseRung("a {Rate r, perMinute => count} => a"),
                     *ladder::parseRung("a => missing")};
    project.blocks.push_back(main);
    project.blocks.push_back(st("Rate", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK Rate\nVAR_INPUT pulse : BOOL; END_VAR\n"
                                "VAR_OUTPUT perMinute : INT; END_VAR\nEND_FUNCTION_BLOCK\n"));
    auto check = checkCode(project);
    REQUIRE(check.error.has_value());
    CHECK(check.error->block == 0);
    CHECK(check.error->network == 2u);
    CHECK(check.error->message.find("missing") != std::string::npos);

    project.blocks[0].networks.pop_back();
    CHECK_FALSE(checkCode(project).error.has_value());

    project.blocks[0].variables[1].type = "Gadget"; // an error in the interface: no network
    check = checkCode(project);
    REQUIRE(check.error.has_value());
    CHECK(check.error->block == 0);
    CHECK_FALSE(check.error->network.has_value());

    project.blocks[0].variables[1].type = "INT";
    project.blocks[1].source = "FUNCTION_BLOCK Rate\nVAR_INPUT pulse : BOOL; END_VAR\n"
                               "VAR_OUTPUT perMinute : INT; END_VAR\n    perMinute := pulse;\nEND_FUNCTION_BLOCK\n";
    check = checkCode(project);
    REQUIRE(check.error.has_value());
    CHECK(check.error->block == 1);
    CHECK(check.error->line == 3);
    CHECK_FALSE(check.error->network.has_value());
}

TEST_CASE("Online helpers: the names on each line, and the instances of each function block") {
    const auto names = namesByLine("PROGRAM Main\n  x := t1.Q AND x;\n\n  p1.timer(IN := a);\nEND_PROGRAM");
    REQUIRE(names.size() == 4);
    CHECK(names[0] == std::vector<std::string>{"Main"});
    CHECK(names[1] == std::vector<std::string>{"x", "t1.Q"});
    CHECK(names[2].empty());
    CHECK(names[3] == std::vector<std::string>{"p1.timer", "IN", "a"});

    ladder::Project project;
    project.blocks.push_back(
        st("Main", BlockKind::Program,
           "PROGRAM Main\nVAR p1 : Pump; p2 : Pump; line : Line; t : TON; END_VAR\nEND_PROGRAM\n"));
    project.blocks.push_back(
        st("Line", BlockKind::FunctionBlock, "FUNCTION_BLOCK Line\nVAR p : Pump; END_VAR\nEND_FUNCTION_BLOCK\n"));
    project.blocks.push_back(st("Pump", BlockKind::FunctionBlock,
                                "FUNCTION_BLOCK Pump\nVAR_INPUT run : BOOL; END_VAR\nEND_FUNCTION_BLOCK\n"));
    const auto check = checkCode(project);
    REQUIRE_FALSE(check.error.has_value());
    CHECK(check.instances.at("PUMP") == std::vector<std::string>{"p1", "p2", "line.p"});
    CHECK(check.instances.at("LINE") == std::vector<std::string>{"line"});
    CHECK(check.instances.count("TON") == 0);
}
