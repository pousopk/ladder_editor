#include "ladder/check.hpp"
#include "ladder/notation.hpp"
#include <doctest/doctest.h>

using namespace ladder;

namespace {

std::vector<std::string> problemsOf(const Rung& rung) {
    std::vector<std::string> messages;
    for (const auto& problem : check(rung)) {
        messages.push_back(problem.message);
    }
    return messages;
}

std::vector<std::string> problemsOf(const char* text) {
    return problemsOf(parseRung(text).value());
}

} // namespace

TEST_CASE("Correct rungs have no problems") {
    for (const char* text : {"=> lamp", "start (motor | /stop) => motor", "{TON t1, PT := T#5s, ET => elapsed} => lamp",
                             "{ctu c, r := reset, pv := 3} => done", "(a | b | c) {R_TRIG e} => S:x, R:y"}) {
        CAPTURE(text);
        CHECK(problemsOf(text).empty());
    }
}

TEST_CASE("Problems with boxes") {
    CHECK(problemsOf("{MOTOR m} => x") == std::vector<std::string>{"unknown box type 'MOTOR'"});
    CHECK(problemsOf("{TON t1, START := a} => x") == std::vector<std::string>{"TON has no input called 'START'"});
    CHECK(problemsOf("{TON t1, IN := a} => x") ==
          std::vector<std::string>{"IN of TON t1 is connected to the rung's power, not to an operand"});
    CHECK(problemsOf("{TON t1, Q => a} => x") ==
          std::vector<std::string>{"Q of TON t1 is connected to the rung's power, not to an operand"});
    CHECK(problemsOf("{TON t1, PT := a, pt := b} => x") == std::vector<std::string>{"PT of TON t1 is connected twice"});
    CHECK(problemsOf("{CTU c, CV := a} => x") == std::vector<std::string>{"CTU has no input called 'CV'"});
}

TEST_CASE("Problems the notation can't write, built by hand") {
    Rung noCoil;
    noCoil.logic.elements.emplace_back(Contact{ContactKind::NormallyOpen, "a"});
    CHECK(problemsOf(noCoil) == std::vector<std::string>{"a rung needs at least one coil at its end"});

    Rung oneSided;
    oneSided.logic.elements.emplace_back(Parallel{{Series{{Contact{ContactKind::NormallyOpen, "a"}}}}});
    oneSided.coils.push_back(Coil{CoilKind::Normal, "x"});
    CHECK(problemsOf(oneSided) == std::vector<std::string>{"a parallel branch needs at least two sides"});

    Rung emptySide;
    emptySide.logic.elements.emplace_back(Parallel{{Series{{Contact{ContactKind::NormallyOpen, "a"}}}, Series{}}});
    emptySide.coils.push_back(Coil{CoilKind::Normal, "x"});
    CHECK(problemsOf(emptySide) ==
          std::vector<std::string>{"a side of a parallel branch is empty (it would always pass power)"});

    Rung blanks;
    blanks.logic.elements.emplace_back(Contact{ContactKind::NormallyOpen, ""});
    blanks.logic.elements.emplace_back(Box{"TON", "", {{"PT", ""}}, {}});
    blanks.coils.push_back(Coil{CoilKind::Normal, ""});
    CHECK(problemsOf(blanks) == std::vector<std::string>{"a contact has no operand", "the TON box has no instance",
                                                         "PT of TON  has no operand", "a coil has no operand"});
}
