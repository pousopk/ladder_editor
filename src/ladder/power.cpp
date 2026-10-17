#include "ladder/power.hpp"

namespace ladder {

namespace {

class Evaluator {
public:
    Evaluator(const BoolLookup& lookup, const BoxCatalog& boxes) : lookup_(lookup), boxes_(boxes) {}

    bool series(const Series& s, bool power, std::vector<std::size_t>& path) {
        for (std::size_t i = 0; i < s.elements.size(); ++i) {
            path.push_back(i);
            power = std::visit([&](const auto& e) { return element(e, power, path); }, s.elements[i]);
            path.pop_back();
        }
        return power;
    }

    RungPower result;

private:
    bool element(const Contact& contact, bool power, const std::vector<std::size_t>& path) {
        auto value = lookup_(contact.operand);
        std::optional<bool> conducting;
        if (value) {
            conducting = contact.kind == ContactKind::NormallyOpen ? *value : !*value;
        }
        const bool out = power && conducting.value_or(false);
        result.items.emplace_back(ElementPath{path}, ItemPower{power, out, conducting});
        return out;
    }

    bool element(const Box& box, bool power, const std::vector<std::size_t>& path) {
        bool out = false;
        if (const auto* type = boxes_.find(box.type)) {
            out = type->powerOutput.empty() ? power : lookup_(box.instance + "." + type->powerOutput).value_or(false);
        }
        result.items.emplace_back(ElementPath{path}, ItemPower{power, out, std::nullopt});
        return out;
    }

    bool element(const Parallel& parallel, bool power, std::vector<std::size_t>& path) {
        bool any = false;
        for (std::size_t side = 0; side < parallel.branches.size(); ++side) {
            path.push_back(side);
            any = series(parallel.branches[side], power, path) || any; // every side is evaluated
            path.pop_back();
        }
        return any;
    }

    const BoolLookup& lookup_;
    const BoxCatalog& boxes_;
};

} // namespace

const ItemPower* RungPower::find(const ItemPath& path) const {
    for (const auto& [p, power] : items) {
        if (p == path) {
            return &power;
        }
    }
    return nullptr;
}

RungPower evaluatePower(const Rung& rung, const BoolLookup& lookup, const BoxCatalog& boxes) {
    Evaluator evaluator(lookup, boxes);
    std::vector<std::size_t> path;
    const bool power = evaluator.series(rung.logic, true, path);
    for (std::size_t i = 0; i < rung.coils.size(); ++i) {
        evaluator.result.items.emplace_back(CoilPath{i}, ItemPower{power, power, std::nullopt});
    }
    return std::move(evaluator.result);
}

} // namespace ladder
