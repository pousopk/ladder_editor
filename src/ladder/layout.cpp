#include "ladder/layout.hpp"
#include "ladder/boxes.hpp"
#include <algorithm>

namespace ladder {

namespace {

struct Size {
    std::size_t width = 0;
    std::size_t height = 1;
};

Size measure(const Series& series, const BoxCatalog& boxes);

Size measure(const Element& element, const BoxCatalog& boxes) {
    if (std::holds_alternative<Contact>(element)) {
        return {1, 1};
    }
    if (const auto* box = std::get_if<Box>(&element)) {
        return {kBoxWidth, boxHeight(*box, boxes)};
    }
    Size size{0, 0};
    for (const auto& branch : std::get<Parallel>(element).branches) {
        const auto side = measure(branch, boxes);
        size.width = std::max(size.width, side.width);
        size.height += side.height;
    }
    size.height = std::max<std::size_t>(size.height, 1);
    return size;
}

// A series is its elements side by side: their widths add up, and it's as
// tall as its tallest element.
Size measure(const Series& series, const BoxCatalog& boxes) {
    Size size{0, 1};
    for (const auto& element : series.elements) {
        const auto part = measure(element, boxes);
        size.width += part.width;
        size.height = std::max(size.height, part.height);
    }
    return size;
}

// Places everything; the sizes are measured first, so a parallel branch
// knows how wide its widest side is before placing any of them.
class Placer {
public:
    Placer(RungLayout& layout, const BoxCatalog& boxes) : layout_(layout), boxes_(boxes) {}

    // Returns the column where the series ends.
    std::size_t series(const Series& s, std::size_t column, std::size_t row, std::vector<std::size_t>& path) {
        for (std::size_t i = 0; i < s.elements.size(); ++i) {
            path.push_back(i);
            column = element(s.elements[i], column, row, path);
            path.pop_back();
        }
        return column;
    }

private:
    std::size_t element(const Element& e, std::size_t column, std::size_t row, std::vector<std::size_t>& path) {
        const auto size = measure(e, boxes_);
        if (!std::holds_alternative<Parallel>(e)) {
            layout_.items.push_back(PlacedItem{ElementPath{path}, column, row, size.width, size.height});
            return column + size.width;
        }
        const auto& parallel = std::get<Parallel>(e);
        const auto right = column + size.width;
        std::size_t sideRow = row;
        std::size_t lastSideRow = row;
        for (std::size_t side = 0; side < parallel.branches.size(); ++side) {
            const auto& branch = parallel.branches[side];
            path.push_back(side);
            const auto end = series(branch, column, sideRow, path);
            path.pop_back();
            if (end < right) {
                layout_.wires.push_back(Wire{sideRow, end, right}); // a short side: wire to the branch's end
            }
            lastSideRow = sideRow;
            sideRow += measure(branch, boxes_).height;
        }
        if (lastSideRow > row) {
            layout_.links.push_back(Link{column, row, lastSideRow}); // where the branch splits...
            layout_.links.push_back(Link{right, row, lastSideRow});  // ...and where it joins again
        }
        return right;
    }

    RungLayout& layout_;
    const BoxCatalog& boxes_;
};

} // namespace

std::size_t boxHeight(const Box& box, const BoxCatalog& boxes) {
    std::size_t pins = std::max(box.inputs.size(), box.outputs.size());
    if (const auto* type = boxes.find(box.type)) {
        pins = std::max({pins, type->inputs.size(), type->outputs.size()});
    }
    return 1 + pins;
}

RungLayout layOut(const Rung& rung, const BoxCatalog& boxes) {
    RungLayout layout;
    std::vector<std::size_t> path;
    const auto logicEnd = Placer(layout, boxes).series(rung.logic, 0, 0, path);
    const auto logicSize = measure(rung.logic, boxes);

    layout.coilColumn = std::max(logicEnd, kMinimumCoilColumn);
    if (logicEnd < layout.coilColumn) {
        layout.wires.push_back(Wire{0, logicEnd, layout.coilColumn}); // from the logic to the coils
    }
    for (std::size_t i = 0; i < rung.coils.size(); ++i) {
        layout.items.push_back(PlacedItem{CoilPath{i}, layout.coilColumn, i, 1, 1});
    }
    if (rung.coils.size() > 1) {
        layout.links.push_back(Link{layout.coilColumn, 0, rung.coils.size() - 1}); // coils side by side
    }
    layout.width = layout.coilColumn + 1;
    layout.height = std::max({logicSize.height, rung.coils.size(), std::size_t{1}});
    return layout;
}

const Element* elementAt(const Rung& rung, const ElementPath& path) {
    const Series* series = &rung.logic;
    const Element* element = nullptr;
    for (std::size_t step = 0; step < path.steps.size(); ++step) {
        if (step % 2 == 0) { // an element of the current series
            if (path.steps[step] >= series->elements.size()) {
                return nullptr;
            }
            element = &series->elements[path.steps[step]];
        } else { // a side of the current parallel branch
            const auto* parallel = std::get_if<Parallel>(element);
            if (parallel == nullptr || path.steps[step] >= parallel->branches.size()) {
                return nullptr;
            }
            series = &parallel->branches[path.steps[step]];
            element = nullptr;
        }
    }
    return element;
}

} // namespace ladder
