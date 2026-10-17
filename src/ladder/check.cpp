#include "ladder/check.hpp"
#include "ladder/boxes.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <set>
#include <string_view>

namespace ladder {

namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

// Collects problems while walking the rung.
class Checker {
public:
    explicit Checker(const BoxCatalog& boxes) : boxes_(boxes) {}

    std::vector<Problem> run(const Rung& rung) {
        series(rung.logic);
        if (rung.coils.empty()) {
            add("a rung needs at least one coil at its end");
        }
        for (const auto& coil : rung.coils) {
            if (coil.operand.empty()) {
                add("a coil has no operand");
            }
        }
        return std::move(problems_);
    }

private:
    void add(std::string message) { problems_.push_back(Problem{std::move(message)}); }

    void series(const Series& s) {
        for (const auto& element : s.elements) {
            std::visit([&](const auto& e) { check(e); }, element);
        }
    }

    void check(const Contact& contact) {
        if (contact.operand.empty()) {
            add("a contact has no operand");
        }
    }

    void check(const Parallel& parallel) {
        if (parallel.branches.size() < 2) {
            add("a parallel branch needs at least two sides");
        }
        for (const auto& branch : parallel.branches) {
            if (branch.elements.empty()) {
                add("a side of a parallel branch is empty (it would always pass power)");
            }
            series(branch);
        }
    }

    void check(const Box& box) {
        const auto* type = boxes_.find(box.type);
        if (type == nullptr) {
            add(fmt::format("unknown box type '{}'", box.type));
            return;
        }
        if (box.instance.empty()) {
            add(fmt::format("the {} box has no instance", type->name));
        }
        const auto pins = [&](const std::vector<Pin>& given, const std::vector<std::string>& allowed,
                              const std::string& power, const char* kind) {
            std::set<std::string> seen;
            for (const auto& pin : given) {
                const auto name = upper(pin.name);
                if (name == upper(power)) {
                    add(fmt::format("{} of {} {} is connected to the rung's power, not to an operand", name, type->name,
                                    box.instance));
                } else if (std::none_of(allowed.begin(), allowed.end(),
                                        [&](const std::string& a) { return upper(a) == name; })) {
                    add(fmt::format("{} has no {} called '{}'", type->name, kind, pin.name));
                } else if (!seen.insert(name).second) {
                    add(fmt::format("{} of {} {} is connected twice", name, type->name, box.instance));
                }
                if (pin.operand.empty()) {
                    add(fmt::format("{} of {} {} has no operand", name, type->name, box.instance));
                }
            }
        };
        pins(box.inputs, type->inputs, type->powerInput, "input");
        pins(box.outputs, type->outputs, type->powerOutput, "output");
    }

    const BoxCatalog& boxes_;
    std::vector<Problem> problems_;
};

} // namespace

std::vector<Problem> check(const Rung& rung, const BoxCatalog& boxes) {
    return Checker(boxes).run(rung);
}

} // namespace ladder
