#pragma once
#include "ladder/rung.hpp"
#include <cstddef>
#include <variant>
#include <vector>

// Where everything in a rung goes, on a grid, worked out from the rung's
// tree. The drawing code turns grid positions into pixels; nothing about
// positions is stored in the rung itself.
//
// Columns are counted at element *boundaries*: an element of width 2 at
// column 3 covers columns 3 to 5. Power runs along the top of each row. Row
// 0 is the rung's main line.
//
//   col 0     1     2     3     4
//   │   start    stop             motor
//   ├────┤ ├──┬───┤ ├───┬──────────( )──┤   row 0
//   │         │  motor │
//   │         └───┤ ├──┘                    row 1
//
// A parallel branch is as wide as its widest side; shorter sides get a wire
// to its right end. Coils are lined up at the right, one per row.
namespace ladder {

// Which element of a rung something is: the index in the rung's logic, then
// for each parallel branch on the way, the side and the index inside it.
// Coils are numbered separately.
struct ElementPath {
    std::vector<std::size_t> steps; // element, side, element, side, element ...
    bool operator==(const ElementPath&) const = default;
};

struct CoilPath {
    std::size_t index = 0;
    bool operator==(const CoilPath&) const = default;
};

using ItemPath = std::variant<ElementPath, CoilPath>;

// One contact, box or coil, placed on the grid.
struct PlacedItem {
    ItemPath path;
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t width = 1;
    std::size_t height = 1;
};

// A horizontal wire along the top of `row`, from one column boundary to another.
struct Wire {
    std::size_t row = 0;
    std::size_t fromColumn = 0;
    std::size_t toColumn = 0;
    bool operator==(const Wire&) const = default;
};

// A vertical connection at a column boundary, joining the rows of a parallel branch.
struct Link {
    std::size_t column = 0;
    std::size_t fromRow = 0;
    std::size_t toRow = 0;
    bool operator==(const Link&) const = default;
};

struct RungLayout {
    std::vector<PlacedItem> items;
    std::vector<Wire> wires;
    std::vector<Link> links;
    std::size_t width = 0;      // up to the right rail
    std::size_t height = 1;     // rows
    std::size_t coilColumn = 0; // where the coils start
};

// The grid sizes of the elements.
constexpr std::size_t kBoxWidth = 3;          // room for an input and an output on the same row
constexpr std::size_t kMinimumCoilColumn = 4; // coils line up at least this far right

// How many rows a box needs: one for the power line, then one per pin of the
// side with the most pins (every pin of the block type is shown, connected or not).
std::size_t boxHeight(const Box& box);

RungLayout layOut(const Rung& rung);

// The element at a path, or null if the path doesn't lead to one.
const Element* elementAt(const Rung& rung, const ElementPath& path);

} // namespace ladder
