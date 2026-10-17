#include "ladder/rung.hpp"

namespace ladder {

std::size_t countElements(const Series& series) {
    std::size_t count = 0;
    for (const auto& element : series.elements) {
        ++count;
        if (const auto* parallel = std::get_if<Parallel>(&element)) {
            for (const auto& branch : parallel->branches) {
                count += countElements(branch);
            }
        }
    }
    return count;
}

} // namespace ladder
