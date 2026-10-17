#include "ladder/translate.hpp"
#include "ladder/check.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <map>
#include <memory>

namespace ladder {

namespace {

// ---------------------------------------------------------------- Boolean expressions

// The power at a point of a rung, as a small expression tree. Subtrees are
// shared (the power entering a branch feeds each of its sides), so they're
// shared_ptr to const.
struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

struct Expr {
    enum class Kind { True, Name, Not, And, Or };
    Kind kind;
    std::string name;           // Name
    std::vector<ExprPtr> items; // Not (one), And, Or
};

ExprPtr makeTrue() {
    static const auto t = std::make_shared<const Expr>(Expr{Expr::Kind::True, "TRUE", {}});
    return t;
}

ExprPtr name(std::string text) {
    return std::make_shared<const Expr>(Expr{Expr::Kind::Name, std::move(text), {}});
}

ExprPtr notOf(ExprPtr e) {
    if (e->kind == Expr::Kind::Not) {
        return e->items.front(); // NOT NOT x is x
    }
    return std::make_shared<const Expr>(Expr{Expr::Kind::Not, "", {std::move(e)}});
}

// AND and OR flatten (a AND (b AND c) is a AND b AND c) and drop what changes
// nothing (TRUE AND x is x).
ExprPtr combine(Expr::Kind kind, const std::vector<ExprPtr>& parts) {
    std::vector<ExprPtr> items;
    for (const auto& part : parts) {
        if (part->kind == Expr::Kind::True) {
            if (kind == Expr::Kind::Or) {
                return makeTrue(); // x OR TRUE is TRUE
            }
            continue;
        }
        if (part->kind == kind) {
            items.insert(items.end(), part->items.begin(), part->items.end());
        } else {
            items.push_back(part);
        }
    }
    if (items.empty()) {
        return makeTrue();
    }
    if (items.size() == 1) {
        return items.front();
    }
    return std::make_shared<const Expr>(Expr{kind, "", std::move(items)});
}

ExprPtr andOf(ExprPtr a, ExprPtr b) {
    return combine(Expr::Kind::And, {std::move(a), std::move(b)});
}

// Precedence, loosest first, as in ST: OR, AND, NOT, then names.
int precedence(const Expr& e) {
    switch (e.kind) {
    case Expr::Kind::Or:
        return 1;
    case Expr::Kind::And:
        return 2;
    case Expr::Kind::Not:
        return 3;
    default:
        return 4;
    }
}

// Writes the expression with only the parentheses it needs.
std::string print(const Expr& e, int outer = 0) {
    std::string text;
    switch (e.kind) {
    case Expr::Kind::True:
    case Expr::Kind::Name:
        return e.name;
    case Expr::Kind::Not:
        text = "NOT " + print(*e.items.front(), precedence(e));
        break;
    case Expr::Kind::And:
    case Expr::Kind::Or:
        for (std::size_t i = 0; i < e.items.size(); ++i) {
            text += (i == 0 ? "" : e.kind == Expr::Kind::And ? " AND " : " OR ") + print(*e.items[i], precedence(e));
        }
        break;
    }
    return precedence(e) < outer ? "(" + text + ")" : text;
}

bool isSimple(const Expr& e) {
    return e.kind == Expr::Kind::True || e.kind == Expr::Kind::Name;
}

bool containsBox(const Series& series) {
    return std::any_of(series.elements.begin(), series.elements.end(), [](const Element& element) {
        if (std::holds_alternative<Box>(element)) {
            return true;
        }
        if (const auto* parallel = std::get_if<Parallel>(&element)) {
            return std::any_of(parallel->branches.begin(), parallel->branches.end(), containsBox);
        }
        return false;
    });
}

// ---------------------------------------------------------------- Translating a block

struct TranslationError {
    std::string message;
};

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

// A bit address an operand may be: %IX0.0 or %I0.0 (also Q and M), %DB1.DBX0.0.
bool isBitAddress(std::string_view text) {
    const auto upperText = upper(text);
    if (upperText.rfind("%DB", 0) == 0) {
        return upperText.find(".DBX") != std::string::npos;
    }
    return upperText.size() > 2 && (upperText[1] == 'I' || upperText[1] == 'Q' || upperText[1] == 'M') &&
           upperText.find('.') != std::string::npos &&
           (upperText[2] == 'X' || std::isdigit(static_cast<unsigned char>(upperText[2])) != 0);
}

class BlockTranslator {
public:
    BlockTranslator(const Block& block, const BoxCatalog& boxes) : block_(block), boxes_(boxes) {}

    TranslatedBlock run() {
        // The networks first: they may need generated variables in the interface.
        std::vector<std::vector<std::string>> networks;
        TranslatedBlock result;
        for (std::size_t i = 0; i < block_.networks.size(); ++i) {
            networks.push_back(network(i, result.problems));
        }

        std::vector<std::string> lines;
        std::vector<LineOrigin> origins{LineOrigin{0, std::nullopt}};
        lines.push_back(header());
        declarations(lines);
        for (std::size_t i = 0; i < networks.size(); ++i) {
            lines.emplace_back();
            origins.push_back(LineOrigin{lines.size(), i});
            lines.insert(lines.end(), networks[i].begin(), networks[i].end());
        }
        lines.emplace_back();
        origins.push_back(LineOrigin{lines.size(), std::nullopt});
        lines.push_back(fmt::format("END_{}", kindName(block_.kind)));

        for (const auto& line : lines) {
            result.text += line + "\n";
        }
        result.origins = std::move(origins);
        return result;
    }

private:
    std::string header() const {
        if (block_.kind == BlockKind::Function) {
            return fmt::format("FUNCTION {} : {}", block_.name, block_.returnType);
        }
        return fmt::format("{} {}", kindName(block_.kind), block_.name);
    }

    static std::string commentOf(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        std::string safe = text;
        for (auto at = safe.find("*)"); at != std::string::npos; at = safe.find("*)", at)) {
            safe.replace(at, 2, "* )"); // "*)" would end the comment early
        }
        std::replace(safe.begin(), safe.end(), '\n', ' ');
        return " (* " + safe + " *)";
    }

    void declarations(std::vector<std::string>& lines) const {
        for (const auto section : {Section::Input, Section::Output, Section::Local}) {
            std::vector<std::string> entries;
            for (const auto& v : block_.variables) {
                if (v.section != section) {
                    continue;
                }
                entries.push_back(fmt::format("    {}{} : {}{};{}", v.name, v.address.empty() ? "" : " AT " + v.address,
                                              v.type, v.initial.empty() ? "" : " := " + v.initial,
                                              commentOf(v.comment)));
            }
            if (section == Section::Local) {
                for (const auto& [address, generated] : addresses_) {
                    entries.push_back(fmt::format("    {} AT {} : BOOL; (* generated: the operand {} *)", generated,
                                                  address, address));
                }
                for (std::size_t i = 1; i <= temporaries_; ++i) {
                    entries.push_back(
                        fmt::format("    {}power{} : BOOL; (* generated: a rung's power *)", kGeneratedPrefix, i));
                }
            }
            if (entries.empty()) {
                continue;
            }
            lines.emplace_back(sectionName(section));
            lines.insert(lines.end(), entries.begin(), entries.end());
            lines.emplace_back("END_VAR");
        }
    }

    // One network's statements, or a comment saying why it's left out.
    std::vector<std::string> network(std::size_t index, std::vector<TranslationProblem>& problems) {
        const auto& rung = block_.networks[index];
        std::vector<std::string> lines;
        const auto commentLines = [&](std::string_view comment) {
            std::vector<std::string> out;
            std::size_t start = 0;
            for (;;) {
                const auto end = comment.find('\n', start);
                out.emplace_back(comment.substr(start, end == std::string_view::npos ? end : end - start));
                if (end == std::string_view::npos)
                    break;
                start = end + 1;
            }
            return out;
        };
        const auto title = commentLines(rung.comment);
        lines.push_back(
            fmt::format("    // Network {}{}", index + 1, rung.comment.empty() ? "" : ": " + title.front()));
        for (std::size_t i = 1; i < title.size(); ++i) {
            lines.push_back("    // " + title[i]);
        }

        const auto problems_ = check(rung, boxes_);
        try {
            if (!problems_.empty()) {
                throw TranslationError{problems_.front().message};
            }
            statements_.clear();
            auto power = series(rung.logic, makeTrue());
            coils(rung, power);
            lines.insert(lines.end(), statements_.begin(), statements_.end());
        } catch (const TranslationError& error) {
            problems.push_back(TranslationProblem{index, error.message});
            lines.push_back(fmt::format("    // (not translated: {})", error.message));
        }
        return lines;
    }

    // An operand as ST: a name as it is, a bit address through its generated variable.
    std::string operand(const std::string& text) {
        if (text.empty() || text.front() != '%') {
            return text;
        }
        if (block_.kind != BlockKind::Program) {
            throw TranslationError{fmt::format("{}: I/O addresses can only be used in the PROGRAM; pass the value in "
                                               "through an input",
                                               text)};
        }
        if (!isBitAddress(text)) {
            throw TranslationError{
                fmt::format("{} isn't a bit address: contacts and coils need one, like %IX0.0", text)};
        }
        const auto key = upper(text);
        auto it = addresses_.find(key);
        if (it == addresses_.end()) {
            it = addresses_.emplace(key, addressVariable(key)).first;
        }
        return it->second;
    }

    // A pin's name as the box type writes it (the rung may write it in another case).
    static std::string spelling(const std::vector<std::string>& pins, const std::string& pin) {
        const auto it =
            std::find_if(pins.begin(), pins.end(), [&](const std::string& p) { return upper(p) == upper(pin); });
        return it == pins.end() ? pin : *it;
    }

    // Keeps a power value in a generated variable, so it can be used twice.
    ExprPtr keep(const ExprPtr& power) {
        if (isSimple(*power)) {
            return power;
        }
        const auto generated = fmt::format("{}power{}", kGeneratedPrefix, ++temporaries_);
        statements_.push_back(fmt::format("    {} := {};", generated, print(*power)));
        return name(generated);
    }

    ExprPtr series(const Series& s, ExprPtr power) {
        for (const auto& element : s.elements) {
            power = std::visit([&](const auto& e) { return this->element(e, power); }, element);
        }
        return power;
    }

    ExprPtr element(const Contact& contact, const ExprPtr& power) {
        auto value = name(operand(contact.operand));
        return andOf(power, contact.kind == ContactKind::NormallyOpen ? value : notOf(value));
    }

    ExprPtr element(const Parallel& parallel, ExprPtr power) {
        const bool boxes = std::any_of(parallel.branches.begin(), parallel.branches.end(), containsBox);
        std::vector<ExprPtr> sides;
        if (!boxes) {
            // Only contacts: the branch is a condition, ANDed with the power: a AND (b OR c).
            for (const auto& side : parallel.branches) {
                sides.push_back(series(side, makeTrue()));
            }
            return andOf(power, combine(Expr::Kind::Or, sides));
        }
        // Boxes on a side get the power itself (a timer's IN must see it),
        // so each side starts from the power entering the branch.
        power = keep(power);
        for (const auto& side : parallel.branches) {
            sides.push_back(series(side, power));
        }
        return combine(Expr::Kind::Or, sides);
    }

    ExprPtr element(const Box& box, const ExprPtr& power) {
        const auto* type = boxes_.find(box.type);
        if (type == nullptr) {
            throw TranslationError{fmt::format("unknown box type '{}'", box.type)};
        }
        // Without a power output the power goes on as it came in, so it's
        // kept before the call (and its outputs) can change what it reads.
        const auto through = type->powerOutput.empty() ? keep(power) : power;
        std::string arguments = fmt::format("{} := {}", type->powerInput, print(*through));
        for (const auto& pin : box.inputs) {
            arguments += fmt::format(", {} := {}", spelling(type->inputs, pin.name), pin.operand);
        }
        statements_.push_back(fmt::format("    {}({});", box.instance, arguments));
        for (const auto& pin : box.outputs) {
            if (!pin.operand.empty() && pin.operand.front() == '%') {
                throw TranslationError{fmt::format("{} of {}: connect outputs to variables, not to addresses",
                                                   upper(pin.name), box.instance)};
            }
            statements_.push_back(
                fmt::format("    {} := {}.{};", pin.operand, box.instance, spelling(type->outputs, pin.name)));
        }
        return type->powerOutput.empty() ? through : name(box.instance + "." + type->powerOutput);
    }

    void coils(const Rung& rung, ExprPtr power) {
        if (rung.coils.size() > 1) {
            power = keep(power); // computed once, used by every coil
        }
        const auto value = print(*power);
        for (const auto& coil : rung.coils) {
            const auto target = operand(coil.operand);
            switch (coil.kind) {
            case CoilKind::Normal:
                statements_.push_back(fmt::format("    {} := {};", target, value));
                break;
            case CoilKind::Negated:
                statements_.push_back(fmt::format("    {} := {};", target, print(*notOf(power))));
                break;
            case CoilKind::Set:
                statements_.push_back(fmt::format("    IF {} THEN {} := TRUE; END_IF;", value, target));
                break;
            case CoilKind::Reset:
                statements_.push_back(fmt::format("    IF {} THEN {} := FALSE; END_IF;", value, target));
                break;
            }
        }
    }

    const Block& block_;
    const BoxCatalog& boxes_;
    std::vector<std::string> statements_;          // the current network's statements
    std::map<std::string, std::string> addresses_; // operand address -> generated variable
    std::size_t temporaries_ = 0;
};

std::size_t lineCount(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + (text.empty() || text.back() != '\n');
}

} // namespace

std::string addressVariable(std::string_view address) {
    std::string generated = kGeneratedPrefix;
    for (const char c : upper(address.substr(address.empty() ? 0 : 1))) {
        generated += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
    }
    return generated;
}

std::optional<std::size_t> TranslatedBlock::networkAt(std::size_t line) const {
    auto it = std::upper_bound(origins.begin(), origins.end(), line,
                               [](std::size_t l, const LineOrigin& origin) { return l < origin.firstLine; });
    if (it == origins.begin()) {
        return std::nullopt;
    }
    return std::prev(it)->network;
}

TranslatedBlock translateBlock(const Block& block, const BoxCatalog& boxes) {
    return BlockTranslator(block, boxes).run();
}

std::optional<std::pair<std::size_t, std::size_t>> TranslatedProject::blockAt(std::size_t line) const {
    auto it = std::upper_bound(parts.begin(), parts.end(), line,
                               [](std::size_t l, const Part& part) { return l < part.firstLine; });
    if (it == parts.begin()) {
        return std::nullopt;
    }
    --it;
    return std::pair{it->block, line - it->firstLine};
}

TranslatedProject translateProject(const Project& project, const BoxCatalog& boxes) {
    TranslatedProject result;
    std::size_t line = 0;
    for (std::size_t i = 0; i < project.blocks.size(); ++i) {
        const auto& block = project.blocks[i];
        TranslatedProject::Part part{i, line, {}};
        std::string text;
        if (block.language == Language::Kop) {
            part.translation = translateBlock(block, boxes);
            text = part.translation.text;
        } else {
            text = block.source;
            if (text.empty() || text.back() != '\n') {
                text += '\n';
            }
        }
        result.text += text;
        line += lineCount(text);
        result.parts.push_back(std::move(part));
    }
    return result;
}

} // namespace ladder
