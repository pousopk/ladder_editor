#include "ladder/notation.hpp"
#include "ladder/power.hpp"
#include <doctest/doctest.h>
#include <map>

using namespace ladder;

namespace {

RungPower powerOf(const char* rung, std::map<std::string, bool> values) {
    return evaluatePower(parseRung(rung).value(), [&](const std::string& operand) -> std::optional<bool> {
        const auto it = values.find(operand);
        return it == values.end() ? std::nullopt : std::optional(it->second);
    });
}

ItemPower at(const RungPower& power, ItemPath path) {
    const auto* item = power.find(path);
    REQUIRE(item != nullptr);
    return *item;
}

} // namespace

TEST_CASE("Power goes through closed contacts and stops at open ones") {
    const auto p = powerOf("a /b c => x", {{"a", true}, {"b", false}, {"c", false}});
    CHECK(at(p, ElementPath{{0}}).in);
    CHECK(at(p, ElementPath{{0}}).out);
    CHECK(at(p, ElementPath{{1}}).out); // normally closed, b is FALSE: closed
    CHECK(at(p, ElementPath{{1}}).conducting == true);
    CHECK(at(p, ElementPath{{2}}).in);
    CHECK_FALSE(at(p, ElementPath{{2}}).out);
    CHECK_FALSE(at(p, CoilPath{0}).in);
}

TEST_CASE("A contact without power still shows whether it's closed") {
    const auto p = powerOf("a b => x", {{"a", false}, {"b", true}});
    CHECK_FALSE(at(p, ElementPath{{1}}).in);
    CHECK(at(p, ElementPath{{1}}).conducting == true);
    CHECK_FALSE(at(p, ElementPath{{1}}).out);
}

TEST_CASE("Parallel sides: any one is enough, and every side is evaluated") {
    const auto p = powerOf("(start | motor) stop => motor", {{"start", false}, {"motor", true}, {"stop", true}});
    CHECK_FALSE(at(p, ElementPath{{0, 0, 0}}).out);
    CHECK(at(p, ElementPath{{0, 1, 0}}).out);
    CHECK(at(p, ElementPath{{1}}).in);
    CHECK(at(p, CoilPath{0}).in);
}

TEST_CASE("A box gives out its power output, whatever came in") {
    // A TOF: Q stays TRUE for a while after IN goes FALSE.
    const auto p = powerOf("a {TOF t1, PT := T#5s} => lamp", {{"a", false}, {"t1.Q", true}});
    CHECK_FALSE(at(p, ElementPath{{1}}).in);
    CHECK(at(p, ElementPath{{1}}).out);
    CHECK(at(p, CoilPath{0}).in);
}

TEST_CASE("Unknown values carry no power") {
    const auto p = powerOf("a => x", {});
    CHECK(at(p, ElementPath{{0}}).in);
    CHECK_FALSE(at(p, ElementPath{{0}}).conducting.has_value());
    CHECK_FALSE(at(p, CoilPath{0}).in);
}
