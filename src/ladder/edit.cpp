#include "ladder/edit.hpp"
#include <algorithm>
#include <utility>

namespace ladder {

namespace {

tl::unexpected<std::string> noSuchElement() {
    return tl::unexpected(std::string("there's no element there"));
}

// Removes empty sides and turns branches with a single side back into plain
// series, everywhere below `series`. Run after every removal.
void tidy(Series& series) {
    for (std::size_t i = 0; i < series.elements.size();) {
        auto* parallel = std::get_if<Parallel>(&series.elements[i]);
        if (parallel == nullptr) {
            ++i;
            continue;
        }
        for (auto& side : parallel->branches) {
            tidy(side);
        }
        auto& sides = parallel->branches;
        sides.erase(std::remove_if(sides.begin(), sides.end(), [](const Series& s) { return s.elements.empty(); }),
                    sides.end());
        if (sides.size() >= 2) {
            ++i;
            continue;
        }
        // One side left (or none): its elements take the branch's place.
        std::vector<Element> lifted;
        if (!sides.empty()) {
            lifted = std::move(sides.front().elements);
        }
        series.elements.erase(series.elements.begin() + static_cast<std::ptrdiff_t>(i));
        series.elements.insert(series.elements.begin() + static_cast<std::ptrdiff_t>(i),
                               std::make_move_iterator(lifted.begin()), std::make_move_iterator(lifted.end()));
        i += lifted.size();
    }
}

} // namespace

SeriesPath parentSeries(const ElementPath& path) {
    SeriesPath series;
    series.steps.assign(path.steps.begin(), path.steps.end() - 1);
    return series;
}

std::size_t indexInSeries(const ElementPath& path) {
    return path.steps.back();
}

InsertPoint before(const ElementPath& path) {
    return InsertPoint{parentSeries(path), indexInSeries(path)};
}

InsertPoint after(const ElementPath& path) {
    return InsertPoint{parentSeries(path), indexInSeries(path) + 1};
}

ElementPath pathAt(const InsertPoint& point) {
    ElementPath path{point.series.steps};
    path.steps.push_back(point.index);
    return path;
}

const Series* seriesAt(const Rung& rung, const SeriesPath& path) {
    if (path.steps.size() % 2 != 0) {
        return nullptr; // a series path ends with a side
    }
    const Series* series = &rung.logic;
    for (std::size_t i = 0; i < path.steps.size(); i += 2) {
        if (path.steps[i] >= series->elements.size()) {
            return nullptr;
        }
        const auto* parallel = std::get_if<Parallel>(&series->elements[path.steps[i]]);
        if (parallel == nullptr || path.steps[i + 1] >= parallel->branches.size()) {
            return nullptr;
        }
        series = &parallel->branches[path.steps[i + 1]];
    }
    return series;
}

Series* seriesAt(Rung& rung, const SeriesPath& path) {
    return const_cast<Series*>(seriesAt(std::as_const(rung), path)); // safe: `rung` isn't const here
}

Element* elementAt(Rung& rung, const ElementPath& path) {
    return const_cast<Element*>(elementAt(std::as_const(rung), path));
}

tl::expected<ElementPath, std::string> insertElement(Rung& rung, const InsertPoint& point, Element element) {
    auto* series = seriesAt(rung, point.series);
    if (series == nullptr || point.index > series->elements.size()) {
        return tl::unexpected(std::string("there's no place there to insert"));
    }
    series->elements.insert(series->elements.begin() + static_cast<std::ptrdiff_t>(point.index), std::move(element));
    return pathAt(point);
}

tl::expected<std::optional<ElementPath>, std::string> removeElement(Rung& rung, const ElementPath& path) {
    if (elementAt(rung, path) == nullptr) {
        return noSuchElement();
    }
    auto* series = seriesAt(rung, parentSeries(path));
    const auto index = indexInSeries(path);
    series->elements.erase(series->elements.begin() + static_cast<std::ptrdiff_t>(index));
    const bool sideStillThere = !series->elements.empty();
    tidy(rung.logic);

    // Select the element that moved into its place, or the one before it.
    // If tidying took the branch apart, the old path means nothing any more.
    if (sideStillThere || path.steps.size() == 1) {
        series = seriesAt(rung, parentSeries(path));
        if (series != nullptr && !series->elements.empty()) {
            ElementPath next = path;
            next.steps.back() = std::min(index, series->elements.size() - 1);
            if (elementAt(rung, next) != nullptr) {
                return std::optional(next);
            }
        }
    }
    return std::optional<ElementPath>();
}

tl::expected<ElementPath, std::string> addParallel(Rung& rung, const ElementPath& path, Element other) {
    auto* element = elementAt(rung, path);
    if (element == nullptr) {
        return noSuchElement();
    }
    Parallel parallel;
    parallel.branches.resize(2);
    parallel.branches[0].elements.push_back(std::move(*element));
    parallel.branches[1].elements.push_back(std::move(other));
    *element = std::move(parallel);
    ElementPath result = path;
    result.steps.push_back(1); // the new side
    result.steps.push_back(0); // its only element
    return result;
}

tl::expected<ElementPath, std::string> addSide(Rung& rung, const ElementPath& path, Element element) {
    if (elementAt(rung, path) == nullptr) {
        return noSuchElement();
    }
    if (path.steps.size() < 3) {
        return tl::unexpected(std::string("this element isn't on a branch"));
    }
    // The branch is two steps up: drop the element's index and its side's.
    ElementPath branchPath{std::vector<std::size_t>(path.steps.begin(), path.steps.end() - 2)};
    auto& parallel = std::get<Parallel>(*elementAt(rung, branchPath));
    parallel.branches.push_back(Series{});
    parallel.branches.back().elements.push_back(std::move(element));
    ElementPath result = branchPath;
    result.steps.push_back(parallel.branches.size() - 1);
    result.steps.push_back(0);
    return result;
}

tl::expected<ElementPath, std::string> moveElement(Rung& rung, const ElementPath& path, int direction) {
    if (elementAt(rung, path) == nullptr) {
        return noSuchElement();
    }
    auto* series = seriesAt(rung, parentSeries(path));
    const auto index = indexInSeries(path);
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= series->elements.size())) {
        return tl::unexpected(std::string(direction < 0 ? "it's already first" : "it's already last"));
    }
    const auto other = direction < 0 ? index - 1 : index + 1;
    std::swap(series->elements[index], series->elements[other]);
    ElementPath result = path;
    result.steps.back() = other;
    return result;
}

tl::expected<CoilPath, std::string> insertCoil(Rung& rung, std::size_t index, Coil coil) {
    if (index > rung.coils.size()) {
        return tl::unexpected(std::string("there's no place there for a coil"));
    }
    rung.coils.insert(rung.coils.begin() + static_cast<std::ptrdiff_t>(index), std::move(coil));
    return CoilPath{index};
}

tl::expected<std::optional<CoilPath>, std::string> removeCoil(Rung& rung, const CoilPath& path) {
    if (path.index >= rung.coils.size()) {
        return tl::unexpected(std::string("there's no coil there"));
    }
    if (rung.coils.size() == 1) {
        return tl::unexpected(std::string("a rung needs at least one coil"));
    }
    rung.coils.erase(rung.coils.begin() + static_cast<std::ptrdiff_t>(path.index));
    return std::optional(CoilPath{std::min(path.index, rung.coils.size() - 1)});
}

} // namespace ladder
