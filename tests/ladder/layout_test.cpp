#include "ladder/layout.hpp"
#include "ladder/notation.hpp"
#include <algorithm>
#include <doctest/doctest.h>

using namespace ladder;

namespace {

RungLayout layOut(const char* text) {
    return ladder::layOut(parseRung(text).value());
}

// The placed item for a path, which must exist.
const PlacedItem& itemAt(const RungLayout& layout, const ItemPath& path) {
    const auto it = std::find_if(layout.items.begin(), layout.items.end(),
                                 [&](const PlacedItem& item) { return item.path == path; });
    REQUIRE(it != layout.items.end());
    return *it;
}

ElementPath element(std::vector<std::size_t> steps) {
    return ElementPath{std::move(steps)};
}

void checkPlace(const PlacedItem& item, std::size_t column, std::size_t row, std::size_t width = 1,
                std::size_t height = 1) {
    CHECK(item.column == column);
    CHECK(item.row == row);
    CHECK(item.width == width);
    CHECK(item.height == height);
}

} // namespace

TEST_CASE("A single contact and a coil, with a wire between them") {
    const auto layout = layOut("a => x");
    REQUIRE(layout.items.size() == 2);
    checkPlace(itemAt(layout, element({0})), 0, 0);
    checkPlace(itemAt(layout, CoilPath{0}), 4, 0); // coils line up at column 4 at least
    CHECK(layout.wires == std::vector<Wire>{{0, 1, 4}});
    CHECK(layout.links.empty());
    CHECK(layout.width == 5);
    CHECK(layout.height == 1);
}

TEST_CASE("A parallel branch puts its sides on rows below each other, linked at both ends") {
    //   start   stop       motor
    // ─┬─┤ ├─┬──┤ ├── ─ ─ ─( )─
    //  └─┤ ├─┘
    //   motor
    const auto layout = layOut("(start | motor) stop => motor");
    checkPlace(itemAt(layout, element({0, 0, 0})), 0, 0); // start: element 0, side 0, element 0
    checkPlace(itemAt(layout, element({0, 1, 0})), 0, 1); // motor: element 0, side 1, element 0
    checkPlace(itemAt(layout, element({1})), 1, 0);       // stop
    checkPlace(itemAt(layout, CoilPath{0}), 4, 0);
    CHECK(layout.links == std::vector<Link>{{0, 0, 1}, {1, 0, 1}});
    CHECK(layout.wires == std::vector<Wire>{{0, 2, 4}});
    CHECK(layout.height == 2);
}

TEST_CASE("A shorter side gets a wire to the end of the branch") {
    const auto layout = layOut("a (b c | d) => x");
    checkPlace(itemAt(layout, element({1, 0, 0})), 1, 0);
    checkPlace(itemAt(layout, element({1, 0, 1})), 2, 0);
    checkPlace(itemAt(layout, element({1, 1, 0})), 1, 1);
    CHECK(layout.wires == std::vector<Wire>{{1, 2, 3}, {0, 3, 4}}); // d's side, then to the coil
    CHECK(layout.links == std::vector<Link>{{1, 0, 1}, {3, 0, 1}});
}

TEST_CASE("Branches inside branches") {
    const auto layout = layOut("(a | (b | c) d) => x");
    checkPlace(itemAt(layout, element({0, 0, 0})), 0, 0);       // a
    checkPlace(itemAt(layout, element({0, 1, 0, 0, 0})), 0, 1); // b
    checkPlace(itemAt(layout, element({0, 1, 0, 1, 0})), 0, 2); // c
    checkPlace(itemAt(layout, element({0, 1, 1})), 1, 1);       // d
    CHECK(layout.wires == std::vector<Wire>{{0, 1, 2}, {0, 2, 4}});
    // The inner branch's links come first (it's placed while placing the outer one).
    CHECK(layout.links == std::vector<Link>{{0, 1, 2}, {1, 1, 2}, {0, 0, 1}, {2, 0, 1}});
    CHECK(layout.height == 3);
}

TEST_CASE("A box is two columns wide and one row taller than its pins") {
    const auto layout = layOut("start {TON t1, PT := T#5s} => lamp");
    checkPlace(itemAt(layout, element({1})), 1, 0, kBoxWidth, 2); // PT and ET: one pin row
    CHECK(boxHeight(Box{"CTU", "c", {}, {}}) == 3);               // R and PV: every pin of the type is shown
    CHECK(boxHeight(Box{"R_TRIG", "e", {}, {}}) == 1);
    CHECK(layout.height == 2);
}

TEST_CASE("Several coils stack up, linked on their left") {
    const auto layout = layOut("a => x, S:y, R:z");
    checkPlace(itemAt(layout, CoilPath{0}), 4, 0);
    checkPlace(itemAt(layout, CoilPath{2}), 4, 2);
    CHECK(layout.links == std::vector<Link>{{4, 0, 2}});
    CHECK(layout.height == 3);
}

TEST_CASE("A long rung pushes the coils right; an empty one is just a wire") {
    const auto longRung = layOut("a b c d e => x");
    checkPlace(itemAt(longRung, CoilPath{0}), 5, 0);
    CHECK(longRung.wires.empty());
    CHECK(longRung.width == 6);

    const auto empty = layOut("=> x");
    CHECK(empty.wires == std::vector<Wire>{{0, 0, 4}});
}

TEST_CASE("Paths lead back to the elements") {
    const auto rung = parseRung("a (b | c {TON t, PT := T#1s}) => x").value();
    REQUIRE(elementAt(rung, element({0})) != nullptr);
    CHECK(std::get<Contact>(*elementAt(rung, element({0}))).operand == "a");
    CHECK(std::get<Contact>(*elementAt(rung, element({1, 1, 0}))).operand == "c");
    CHECK(std::get<Box>(*elementAt(rung, element({1, 1, 1}))).instance == "t");
    CHECK(std::holds_alternative<Parallel>(*elementAt(rung, element({1}))));
    CHECK(elementAt(rung, element({2})) == nullptr);       // no third element
    CHECK(elementAt(rung, element({0, 0, 0})) == nullptr); // a isn't a branch
    CHECK(elementAt(rung, element({1, 2, 0})) == nullptr); // no third side
    // Every placed element can be found again by its path.
    for (const auto& item : ladder::layOut(rung).items) {
        if (const auto* path = std::get_if<ElementPath>(&item.path)) {
            CHECK(elementAt(rung, *path) != nullptr);
        }
    }
}
