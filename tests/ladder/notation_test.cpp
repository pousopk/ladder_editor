#include "ladder/notation.hpp"
#include <doctest/doctest.h>

using namespace ladder;

TEST_CASE("The notation reads every kind of element") {
    const auto rung =
        parseRung("start (motor | /stop) {TON t1, PT := T#5s, ET => elapsed} => lamp, /off, S:alarm, R:ack");
    REQUIRE(rung.has_value());
    const auto& logic = rung->logic.elements;
    REQUIRE(logic.size() == 3);
    CHECK(std::get<Contact>(logic[0]) == Contact{ContactKind::NormallyOpen, "start"});
    const auto& branch = std::get<Parallel>(logic[1]);
    REQUIRE(branch.branches.size() == 2);
    CHECK(std::get<Contact>(branch.branches[1].elements[0]) == Contact{ContactKind::NormallyClosed, "stop"});
    CHECK(std::get<Box>(logic[2]) == Box{"TON", "t1", {{"PT", "T#5s"}}, {{"ET", "elapsed"}}});
    CHECK(rung->coils == std::vector<Coil>{{CoilKind::Normal, "lamp"},
                                           {CoilKind::Negated, "off"},
                                           {CoilKind::Set, "alarm"},
                                           {CoilKind::Reset, "ack"}});
}

TEST_CASE("Formatting and parsing are each other's reverse") {
    const char* rungs[] = {
        "=> lamp",
        "a => b",
        "a /b c => d",
        "(a | b) => c",
        "(a b | c (d | /e) | f) => g, S:h",
        "{R_TRIG edge} {CTU count, R := reset, PV := 10, CV => total} => done",
        "%IX0.0 t1.Q sensors[2] => %QX0.1",
        "{TON t1, PT := T#1m30s} (x | {TP pulse, PT := T#200ms, ET => left}) => R:latched",
    };
    for (const char* text : rungs) {
        CAPTURE(text);
        const auto rung = parseRung(text);
        REQUIRE_MESSAGE(rung.has_value(), (rung ? "" : rung.error()));
        CHECK(formatRung(*rung) == text);
        CHECK(parseRung(formatRung(*rung)).value() == *rung);
    }
}

TEST_CASE("Spacing doesn't matter when reading") {
    CHECK(formatRung(parseRung("  a(b|/c){TON t,PT:=T#1s,ET=>e}=>S:x,y  ").value()) ==
          "a (b | /c) {TON t, PT := T#1s, ET => e} => S:x, y");
}

TEST_CASE("Notation errors say what's wrong and where") {
    struct Case {
        const char* text;
        const char* message;
    };
    const Case cases[] = {
        {"", "column 1: expected '=>' before the coils, found the end"},
        {"a b", "column 4: expected '=>' before the coils, found the end"},
        {"a =>", "column 5: expected a coil after '=>', found the end"},
        {"(a | b => c", "column 8: expected ')' to close the parallel branch, found '=>'"},
        {"a ) => c", "column 3: expected '=>' before the coils, found ')'"},
        {"{TON} => x", "column 5: expected the instance name after the box type, as in {TON t1}, found '}'"},
        {"{TON t1, PT T#1s} => x", "column 13: expected ':=' (an input) or '=>' (an output) after 'PT', found 'T#1s'"},
        {"{TON t1, PT := T#1s => x", "column 21: expected '}' to close the box, found '=>'"},
        {"/ => x", "column 1: expected an operand after '/'"},
        {"a => x y", "column 8: expected ',' or the end of the rung after a coil, found 'y'"},
        {"a => S:", "column 6: expected an operand for the coil"},
        {"a => /", "column 6: expected an operand for the coil"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto rung = parseRung(c.text);
        REQUIRE_FALSE(rung.has_value());
        CHECK(rung.error() == c.message);
    }
}
