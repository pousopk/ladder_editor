#pragma once
#include "ladder/layout.hpp"
#include "ladder/rung.hpp"
#include <optional>
#include <string>
#include <tl/expected.hpp>

// Structured editing: every change to a rung goes through one of these
// operations, and each one leaves a valid rung behind. There is no way to
// "draw a wire": branches are made by putting a parallel around an element,
// and taken apart by deleting what's on their sides.
//
// The operations return the path of the element to select afterwards,
// because paths change when the tree does (an insertion shifts the elements
// after it; putting a branch around an element moves it one level down).
namespace ladder {

// A series inside a rung: empty for the rung's logic; otherwise the path to
// a parallel branch, then the side: element, side, element, side, ...
struct SeriesPath {
    std::vector<std::size_t> steps;
    bool operator==(const SeriesPath&) const = default;
};

// A place in a series where an element can go: before the element at `index`
// (or at the end, when index is the series' size).
struct InsertPoint {
    SeriesPath series;
    std::size_t index = 0;
};

// The series an element is in, and where it is in it.
SeriesPath parentSeries(const ElementPath& path);
std::size_t indexInSeries(const ElementPath& path);
InsertPoint before(const ElementPath& path);
InsertPoint after(const ElementPath& path);
ElementPath pathAt(const InsertPoint& point); // the path an element gets when inserted there

Series* seriesAt(Rung& rung, const SeriesPath& path);
const Series* seriesAt(const Rung& rung, const SeriesPath& path);
Element* elementAt(Rung& rung, const ElementPath& path);

// Puts `element` at `point` and returns its path. Fails if the point doesn't exist.
tl::expected<ElementPath, std::string> insertElement(Rung& rung, const InsertPoint& point, Element element);

// Takes an element out. A side of a branch left empty disappears, and a
// branch left with one side becomes plain series again. Returns what to
// select next: the element that took its place, or the one before it, if any.
tl::expected<std::optional<ElementPath>, std::string> removeElement(Rung& rung, const ElementPath& path);

// Puts a parallel branch around the element at `path`, with `other` on a new
// side below it:   a  ->  (a | other)
// Returns the path of `other`.
tl::expected<ElementPath, std::string> addParallel(Rung& rung, const ElementPath& path, Element other);

// Adds a new side, holding `element`, to the branch that the element at
// `path` is on (below the other sides):  (a | b)  ->  (a | b | element)
// Returns the path of `element`.
tl::expected<ElementPath, std::string> addSide(Rung& rung, const ElementPath& path, Element element);

// Swaps an element with its neighbour on the left (-1) or right (+1) in its
// series. Returns its new path.
tl::expected<ElementPath, std::string> moveElement(Rung& rung, const ElementPath& path, int direction);

// Coils: insert at an index (0 = top), or remove. A rung keeps at least one coil.
tl::expected<CoilPath, std::string> insertCoil(Rung& rung, std::size_t index, Coil coil);
tl::expected<std::optional<CoilPath>, std::string> removeCoil(Rung& rung, const CoilPath& path);

} // namespace ladder
