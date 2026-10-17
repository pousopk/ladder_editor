#include "ladder/check.hpp"
#include "ladder/edit.hpp"
#include "ladder/notation.hpp"
#include <doctest/doctest.h>

using namespace ladder;

namespace {

Rung rung(const char* text) {
    return parseRung(text).value();
}
Contact no(const char* operand) {
    return Contact{ContactKind::NormallyOpen, operand};
}
Contact nc(const char* operand) {
    return Contact{ContactKind::NormallyClosed, operand};
}
ElementPath at(std::vector<std::size_t> steps) {
    return ElementPath{std::move(steps)};
}

} // namespace

TEST_CASE("Building the motor starter from an empty rung, one operation at a time") {
    Rung r = rung("=> motor");
    const auto start = insertElement(r, InsertPoint{{}, 0}, no("start")).value();
    CHECK(formatRung(r) == "start => motor");

    const auto holding = addParallel(r, start, no("motor")).value(); // a branch around start, motor below it
    CHECK(formatRung(r) == "(start | motor) => motor");
    CHECK(holding == at({0, 1, 0}));

    const auto stop = insertElement(r, after(at({0})), no("stop")).value(); // after the branch
    CHECK(formatRung(r) == "(start | motor) stop => motor");
    CHECK(stop == at({1}));
    CHECK(check(r).empty());
}

TEST_CASE("Inserting before and after, inside a branch too") {
    Rung r = rung("(a | b) => x");
    insertElement(r, after(at({0, 0, 0})), no("c")).value();  // after a, on the same side
    insertElement(r, before(at({0, 1, 0})), nc("d")).value(); // before b
    CHECK(formatRung(r) == "(a c | /d b) => x");
    CHECK_FALSE(insertElement(r, InsertPoint{{}, 5}, no("e")).has_value());                 // past the end
    CHECK_FALSE(insertElement(r, InsertPoint{SeriesPath{{0, 7}}, 0}, no("e")).has_value()); // no side 7
}

TEST_CASE("Deleting an element selects its neighbour") {
    Rung r = rung("a b c => x");
    CHECK(removeElement(r, at({1})).value() == at({1})); // c moved into b's place
    CHECK(formatRung(r) == "a c => x");
    CHECK(removeElement(r, at({1})).value() == at({0})); // the last one: select the one before
    CHECK(removeElement(r, at({0})).value() == std::nullopt);
    CHECK(formatRung(r) == "=> x");
    CHECK_FALSE(removeElement(r, at({0})).has_value()); // nothing left to delete
}

TEST_CASE("Deleting the last element of a side removes the side, and a branch with one side disappears") {
    Rung r = rung("(a | b | c) d => x");
    removeElement(r, at({0, 1, 0})).value(); // b: its side goes away, two sides are left
    CHECK(formatRung(r) == "(a | c) d => x");
    CHECK(removeElement(r, at({0, 1, 0})).value() == std::nullopt); // c: one side left, the branch goes
    CHECK(formatRung(r) == "a d => x");

    Rung longer = rung("(a b | c) d => x");
    removeElement(longer, at({0, 1, 0})).value(); // the side's elements take the branch's place
    CHECK(formatRung(longer) == "a b d => x");
}

TEST_CASE("Taking apart nested branches keeps the rest") {
    Rung r = rung("(a | (b | c) e) => x");
    removeElement(r, at({0, 1, 0, 1, 0})).value(); // c: the inner branch becomes just b
    CHECK(formatRung(r) == "(a | b e) => x");
    CHECK(check(r).empty());
}

TEST_CASE("Adding sides and branches around branches") {
    Rung r = rung("(a | b) => x");
    CHECK(addSide(r, at({0, 1, 0}), no("c")).value() == at({0, 2, 0}));
    CHECK(formatRung(r) == "(a | b | c) => x");
    CHECK_FALSE(addSide(r, at({0}), no("d")).has_value()); // the branch itself isn't on a branch

    Rung nested = rung("(a | b) => x");
    addParallel(nested, at({0, 0, 0}), no("c")).value(); // a branch around a, inside the first side
    CHECK(formatRung(nested) == "((a | c) | b) => x");
}

TEST_CASE("Moving elements left and right in their series") {
    Rung r = rung("a b (c | d) => x");
    CHECK(moveElement(r, at({0}), +1).value() == at({1}));
    CHECK(formatRung(r) == "b a (c | d) => x");
    CHECK(moveElement(r, at({2}), -1).value() == at({1}));
    CHECK(formatRung(r) == "b (c | d) a => x");
    CHECK_FALSE(moveElement(r, at({0}), -1).has_value());
    CHECK(moveElement(r, at({0}), -1).error() == "it's already first");
    CHECK_FALSE(moveElement(r, at({1, 0, 0}), +1).has_value()); // c is alone on its side
}

TEST_CASE("Coils: insert, remove, but never the last one") {
    Rung r = rung("a => x");
    CHECK(insertCoil(r, 1, Coil{CoilKind::Set, "y"}).value() == CoilPath{1});
    CHECK(formatRung(r) == "a => x, S:y");
    CHECK(removeCoil(r, CoilPath{0}).value() == CoilPath{0});
    CHECK(formatRung(r) == "a => S:y");
    CHECK(removeCoil(r, CoilPath{0}).error() == "a rung needs at least one coil");
    CHECK_FALSE(insertCoil(r, 5, Coil{}).has_value());
}

TEST_CASE("Series paths and element paths fit together") {
    CHECK(parentSeries(at({2})).steps.empty());
    CHECK(parentSeries(at({1, 0, 3})) == SeriesPath{{1, 0}});
    CHECK(indexInSeries(at({1, 0, 3})) == 3);
    CHECK(pathAt(after(at({1, 0, 3}))) == at({1, 0, 4}));
    Rung r = rung("a (b | c) => x");
    CHECK(seriesAt(r, SeriesPath{{1, 1}})->elements.size() == 1);
    CHECK(seriesAt(r, SeriesPath{{0, 0}}) == nullptr); // a isn't a branch
    CHECK(seriesAt(r, SeriesPath{{1}}) == nullptr);    // a path to a series ends with a side
}
