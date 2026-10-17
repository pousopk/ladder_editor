#include "ladder/boxes.hpp"
#include "ladder/rung.hpp"
#include <doctest/doctest.h>

using namespace ladder;

TEST_CASE("A rung is built from series, parallel branches, contacts, boxes and coils") {
    // start (motor | /stop) => motor   -- a self-holding motor starter, with stop in the branch for the example
    Rung rung;
    rung.logic.elements.emplace_back(Contact{ContactKind::NormallyOpen, "start"});
    Parallel branch;
    branch.branches.push_back(Series{{Contact{ContactKind::NormallyOpen, "motor"}}});
    branch.branches.push_back(Series{{Contact{ContactKind::NormallyClosed, "stop"}}});
    rung.logic.elements.emplace_back(std::move(branch));
    rung.coils.push_back(Coil{CoilKind::Normal, "motor"});

    REQUIRE(rung.logic.elements.size() == 2);
    const auto& parallel = std::get<Parallel>(rung.logic.elements[1]);
    CHECK(parallel.branches.size() == 2);
    CHECK(std::get<Contact>(parallel.branches[1].elements[0]).kind == ContactKind::NormallyClosed);
    CHECK(countElements(rung.logic) == 4); // start, the branch, motor, stop
}

TEST_CASE("Rungs compare by value, all the way down") {
    Rung a;
    a.logic.elements.emplace_back(Parallel{
        {Series{{Contact{ContactKind::NormallyOpen, "x"}}}, Series{{Box{"TON", "t1", {{"PT", "T#1s"}}, {}}}}}});
    a.coils.push_back(Coil{CoilKind::Set, "y"});
    Rung b = a; // a deep copy: rungs own all their parts
    CHECK(a == b);
    std::get<Box>(std::get<Parallel>(b.logic.elements[0]).branches[1].elements[0]).inputs[0].operand = "T#2s";
    CHECK(a != b);
}

TEST_CASE("The standard boxes and how power goes through them") {
    const auto* ton = findBoxType("ton");
    REQUIRE(ton != nullptr);
    CHECK(ton->name == "TON");
    CHECK(ton->powerInput == "IN");
    CHECK(ton->powerOutput == "Q");
    CHECK(ton->inputs == std::vector<std::string_view>{"PT"});
    CHECK(ton->outputs == std::vector<std::string_view>{"ET"});
    CHECK(findBoxType("CTU")->powerInput == "CU");
    CHECK(findBoxType("RS")->powerOutput == "Q1");
    CHECK(findBoxType("MOTOR") == nullptr);
    CHECK(standardBoxTypes().size() == 9);
}
