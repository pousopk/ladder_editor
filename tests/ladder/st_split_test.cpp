#include "ladder/st_split.hpp"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ladder;

namespace {

const char* kProgram = "(* types *)\n"
                       "TYPE\n"
                       "  Mode : (A, B);\n"
                       "END_TYPE\n"
                       "\n"
                       "// counts\n"
                       "FUNCTION_BLOCK Counter\n"
                       "END_FUNCTION_BLOCK\n"
                       "FUNCTION Twice : INT\n"
                       "VAR_INPUT x : INT; END_VAR\n"
                       "Twice := 2 * x;\n"
                       "END_FUNCTION\n"
                       "PROGRAM Main\n"
                       "END_PROGRAM\n";

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("Each declaration becomes a block, in order, with its leading comment") {
    const auto blocks = splitStSource(kProgram);
    REQUIRE_MESSAGE(blocks.has_value(), (blocks ? "" : blocks.error()));
    REQUIRE(blocks->size() == 4);
    const char* names[] = {"Types", "Counter", "Twice", "Main"};
    const BlockKind kinds[] = {BlockKind::Types, BlockKind::FunctionBlock, BlockKind::Function, BlockKind::Program};
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK((*blocks)[i].name == names[i]);
        CHECK((*blocks)[i].kind == kinds[i]);
        CHECK((*blocks)[i].language == Language::St);
        CHECK((*blocks)[i].returnType.empty());
    }
    CHECK((*blocks)[0].source == "(* types *)\nTYPE\n  Mode : (A, B);\nEND_TYPE\n");
    CHECK((*blocks)[1].source == "// counts\nFUNCTION_BLOCK Counter\nEND_FUNCTION_BLOCK\n");
}

TEST_CASE("Joining the blocks gives the program back") {
    const auto blocks = splitStSource(kProgram);
    REQUIRE(blocks.has_value());
    std::string joined;
    for (const auto& block : *blocks) {
        joined += block.source;
    }
    std::string expected = kProgram;
    expected.erase(expected.find("END_TYPE\n\n") + 9, 1); // the blank line between blocks
    CHECK(joined == expected);
}

TEST_CASE("Keywords inside comments and strings don't count") {
    const char* source = "PROGRAM Main\nVAR s : STRING := 'END_PROGRAM'; t : STRING := \"$\"END_PROGRAM\"; END_VAR\n"
                         "(* END_PROGRAM *) // END_PROGRAM\nEND_PROGRAM\n";
    const auto blocks = splitStSource(source);
    REQUIRE_MESSAGE(blocks.has_value(), (blocks ? "" : blocks.error()));
    REQUIRE(blocks->size() == 1);
    CHECK((*blocks)[0].source == source);
}

TEST_CASE("Keywords in any case") {
    const auto blocks = splitStSource("program Main\nend_program\nFunction_Block Fb\nEnd_Function_Block\n");
    REQUIRE_MESSAGE(blocks.has_value(), (blocks ? "" : blocks.error()));
    REQUIRE(blocks->size() == 2);
    CHECK((*blocks)[0].name == "Main");
    CHECK((*blocks)[0].kind == BlockKind::Program);
    CHECK((*blocks)[1].name == "Fb");
    CHECK((*blocks)[1].kind == BlockKind::FunctionBlock);
}

TEST_CASE("Windows line ends") {
    const auto blocks = splitStSource("PROGRAM Main\r\nEND_PROGRAM\r\n");
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 1);
    CHECK((*blocks)[0].source == "PROGRAM Main\nEND_PROGRAM\n");
}

TEST_CASE("A comment after the last block joins it") {
    const char* source = "PROGRAM Main\nEND_PROGRAM\n(* the end *)\n";
    const auto blocks = splitStSource(source);
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 1);
    CHECK((*blocks)[0].source == source);
}

TEST_CASE("Several TYPE blocks get numbered names") {
    const auto blocks = splitStSource("TYPE A : INT; END_TYPE\nTYPE B : INT; END_TYPE\nPROGRAM Main\nEND_PROGRAM\n");
    REQUIRE(blocks.has_value());
    REQUIRE(blocks->size() == 3);
    CHECK((*blocks)[0].name == "Types");
    CHECK((*blocks)[1].name == "Types2");
}

TEST_CASE("A TYPE block's name never takes a name the program already uses") {
    const auto blocks = splitStSource("TYPE A : INT; END_TYPE\nFUNCTION_BLOCK Types\nEND_FUNCTION_BLOCK\n"
                                      "PROGRAM Main\nEND_PROGRAM\n");
    REQUIRE_MESSAGE(blocks.has_value(), (blocks ? "" : blocks.error()));
    REQUIRE(blocks->size() == 3);
    CHECK((*blocks)[0].name == "Types2");
    CHECK((*blocks)[1].name == "Types");
}

TEST_CASE("Errors") {
    auto failed = splitStSource("x := 1;\n");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().find("line 1") != std::string::npos);
    CHECK(failed.error().find("outside") != std::string::npos);

    failed = splitStSource("PROGRAM Main\n");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().find("has no END_PROGRAM") != std::string::npos);

    failed = splitStSource("FUNCTION_BLOCK Fb\nEND_FUNCTION_BLOCK\nFUNCTION_BLOCK fb\nEND_FUNCTION_BLOCK\n");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().find("two blocks are named fb") != std::string::npos);

    failed = splitStSource("");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().find("no PROGRAM") != std::string::npos);
}

TEST_CASE("Every soft_plc example program splits, except the one with no PROGRAM") {
    const std::filesystem::path examples = LADDER_EDITOR_SOURCE_DIR "/../soft_plc/examples";
    REQUIRE(std::filesystem::exists(examples));
    int checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(examples)) {
        if (entry.path().extension() != ".st") {
            continue;
        }
        CAPTURE(entry.path().filename().string());
        const auto blocks = splitStSource(readFile(entry.path()));
        if (entry.path().filename() == "expressions.st") {
            CHECK_FALSE(blocks.has_value());
        } else {
            CHECK_MESSAGE(blocks.has_value(), (blocks ? "" : blocks.error()));
        }
        ++checked;
    }
    CHECK(checked > 3);
}

TEST_CASE("projectFromSt names the project after its PROGRAM") {
    const auto project = projectFromSt(kProgram);
    REQUIRE(project.has_value());
    CHECK(project->name == "Main");
    CHECK(project->blocks.size() == 4);
    CHECK(project->watch.empty());
}
