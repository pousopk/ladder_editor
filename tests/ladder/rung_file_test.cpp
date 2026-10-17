#include "ladder/notation.hpp"
#include "ladder/rung_file.hpp"
#include <doctest/doctest.h>

using namespace ladder;

TEST_CASE("A rung file: one rung per line, // lines are the next rung's comment") {
    const auto rungs = parseRungFile("// Motor starter\n"
                                     "// stop always wins\n"
                                     "(start | motor) stop => motor\n"
                                     "\n"
                                     "  motor {TON t1, PT := T#2s} => lamp  \n"
                                     "// Alarm\n"
                                     "fault => S:alarm\n");
    REQUIRE(rungs.has_value());
    REQUIRE(rungs->size() == 3);
    CHECK((*rungs)[0].comment == "Motor starter\nstop always wins");
    CHECK(formatRung((*rungs)[0]) == "(start | motor) stop => motor");
    CHECK((*rungs)[1].comment.empty());
    CHECK((*rungs)[2].comment == "Alarm");
}

TEST_CASE("Only the comment lines right above a rung are its comment") {
    const auto rungs = parseRungFile("// A header for the whole file\n"
                                     "// that belongs to no rung\n"
                                     "\n"
                                     "// The first rung\n"
                                     "a => b\n");
    REQUIRE(rungs.has_value());
    REQUIRE(rungs->size() == 1);
    CHECK((*rungs)[0].comment == "The first rung");
}

TEST_CASE("A rung file writes back the same way") {
    const std::string text = "// Motor starter\n"
                             "// stop always wins\n"
                             "(start | motor) stop => motor\n"
                             "\n"
                             "motor {TON t1, PT := T#2s} => lamp\n";
    CHECK(formatRungFile(parseRungFile(text).value()) == text);
}

TEST_CASE("Errors in a rung file give the line") {
    const auto rungs = parseRungFile("a => b\n\n(c | d => e\n");
    REQUIRE_FALSE(rungs.has_value());
    CHECK(rungs.error() == "line 3, column 8: expected ')' to close the parallel branch, found '=>'");
}
