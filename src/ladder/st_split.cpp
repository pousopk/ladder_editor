#include "ladder/st_split.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <optional>

namespace ladder {

namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

bool isIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isIdentifierPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\v'; }

struct Declaration {
    BlockKind kind;
    const char* end; // the keyword that closes it
};

std::optional<Declaration> declarationFor(std::string_view keyword) {
    if (keyword == "PROGRAM") return Declaration{BlockKind::Program, "END_PROGRAM"};
    if (keyword == "FUNCTION_BLOCK") return Declaration{BlockKind::FunctionBlock, "END_FUNCTION_BLOCK"};
    if (keyword == "FUNCTION") return Declaration{BlockKind::Function, "END_FUNCTION"};
    if (keyword == "TYPE") return Declaration{BlockKind::Types, "END_TYPE"};
    return std::nullopt;
}

// Without the blank lines it starts with and the whitespace it ends with,
// and with one line end.
std::string trimmed(std::string_view text) {
    for (auto lineEnd = text.find('\n'); lineEnd != std::string_view::npos; lineEnd = text.find('\n')) {
        const auto line = text.substr(0, lineEnd);
        if (!std::all_of(line.begin(), line.end(), isBlank)) {
            break;
        }
        text.remove_prefix(lineEnd + 1);
    }
    while (!text.empty() && isBlank(text.back())) {
        text.remove_suffix(1);
    }
    return std::string(text) + '\n';
}

// Walks through the text the way soft_plc's lexer does, as far as finding
// the declarations needs: comments and strings are skipped whole.
class Scanner {
public:
    explicit Scanner(std::string_view text) : text_(text) {}

    std::size_t pos() const { return pos_; }
    void moveTo(std::size_t pos) { pos_ = pos; }
    bool atEnd() const { return pos_ >= text_.size(); }
    std::size_t line(std::size_t pos) const {
        return 1 + static_cast<std::size_t>(std::count(text_.begin(), text_.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
    }

    // Skips whitespace and comments. False if a comment is never closed.
    bool skipTrivia() {
        while (!atEnd()) {
            if (isBlank(text_[pos_])) {
                ++pos_;
            } else if (text_.substr(pos_, 2) == "//") {
                pos_ = std::min(text_.find('\n', pos_), text_.size());
            } else if (text_.substr(pos_, 2) == "(*") {
                const auto close = text_.find("*)", pos_ + 2);
                if (close == std::string_view::npos) {
                    return false;
                }
                pos_ = close + 2;
            } else {
                return true;
            }
        }
        return true;
    }

    // An identifier at the current position, if there's one; moves past it.
    std::optional<std::string_view> identifier() {
        if (atEnd() || !isIdentifierStart(text_[pos_])) {
            return std::nullopt;
        }
        const auto start = pos_;
        while (!atEnd() && isIdentifierPart(text_[pos_])) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    // Moves past whatever is at the current position that isn't an
    // identifier: a string, a number, a symbol.
    void skipToken() {
        const char c = text_[pos_];
        if (c == '\'' || c == '"') {
            ++pos_;
            while (!atEnd() && text_[pos_] != c) {
                pos_ += text_[pos_] == '$' ? std::size_t{2} : std::size_t{1}; // $ escapes the next character
            }
            pos_ = std::min(pos_ + 1, text_.size());
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            while (!atEnd() && (isIdentifierPart(text_[pos_]) || text_[pos_] == '#' || text_[pos_] == '.')) {
                ++pos_;
            }
        } else {
            ++pos_;
        }
    }

    // Where the declaration that ended here ends: after its line, unless
    // more code follows on that line.
    std::size_t endOfDeclaration() const {
        auto pos = pos_;
        while (pos < text_.size() && (text_[pos] == ' ' || text_[pos] == '\t')) {
            ++pos;
        }
        if (pos >= text_.size()) {
            return text_.size();
        }
        if (text_[pos] == '\n' || text_.substr(pos, 2) == "//") {
            return std::min(text_.find('\n', pos), text_.size() - 1) + 1;
        }
        return pos_;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

tl::expected<std::vector<Block>, std::string> splitStSource(std::string_view source) {
    std::string text(source);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    std::vector<Block> blocks;
    std::size_t lastStart = 0; // where the last block's text starts
    Scanner scanner(text);
    const auto unclosedComment = [&](std::size_t from) {
        return tl::unexpected(
            fmt::format("line {}: a comment is never closed with '*)'", scanner.line(text.find("(*", from))));
    };
    for (std::size_t start = 0;; start = scanner.pos()) {
        if (!scanner.skipTrivia()) {
            return unclosedComment(start);
        }
        if (scanner.atEnd()) {
            break;
        }
        const auto declarationStart = scanner.pos();
        const auto keyword = scanner.identifier();
        const auto declaration = keyword ? declarationFor(upper(*keyword)) : std::nullopt;
        if (!declaration) {
            return tl::unexpected(fmt::format("line {}: text outside any PROGRAM, FUNCTION_BLOCK, FUNCTION or TYPE",
                                              scanner.line(declarationStart)));
        }
        Block block;
        block.kind = declaration->kind;
        block.language = Language::St;
        if (block.kind != BlockKind::Types) { // TYPE blocks are named at the end
            if (!scanner.skipTrivia()) {
                return unclosedComment(declarationStart);
            }
            const auto name = scanner.identifier();
            if (!name) {
                return tl::unexpected(
                    fmt::format("line {}: {} needs a name", scanner.line(declarationStart), upper(*keyword)));
            }
            block.name = std::string(*name);
        }
        const auto hasNoEnd = [&] {
            return tl::unexpected(fmt::format("line {}: {}{}{} has no {}", scanner.line(declarationStart),
                                              upper(*keyword), block.kind == BlockKind::Types ? "" : " ",
                                              block.kind == BlockKind::Types ? "" : block.name, declaration->end));
        };
        for (;;) {
            const auto before = scanner.pos();
            if (!scanner.skipTrivia()) {
                return unclosedComment(before);
            }
            if (scanner.atEnd()) {
                return hasNoEnd();
            }
            if (const auto word = scanner.identifier()) {
                if (upper(*word) == declaration->end) {
                    break;
                }
            } else {
                scanner.skipToken();
            }
        }
        for (const auto& other : blocks) {
            if (block.kind != BlockKind::Types && upper(other.name) == upper(block.name)) {
                return tl::unexpected(fmt::format("two blocks are named {}", block.name));
            }
        }
        const auto end = scanner.endOfDeclaration();
        block.source = trimmed(std::string_view(text).substr(start, end - start));
        lastStart = start;
        blocks.push_back(std::move(block));
        scanner.moveTo(end);
    }
    if (blocks.empty()) {
        return tl::unexpected(std::string("no PROGRAM, FUNCTION_BLOCK, FUNCTION or TYPE in the program"));
    }
    // Comments after the last declaration: they belong to it.
    blocks.back().source = trimmed(std::string_view(text).substr(lastStart));
    // TYPE blocks: Types, Types2, ..., skipping names the program uses.
    const auto taken = [&](const std::string& name) {
        return std::any_of(blocks.begin(), blocks.end(),
                           [&](const Block& other) { return upper(other.name) == upper(name); });
    };
    std::size_t number = 1;
    for (auto& block : blocks) {
        if (block.kind != BlockKind::Types) {
            continue;
        }
        std::string name;
        do {
            name = number == 1 ? "Types" : fmt::format("Types{}", number);
            ++number;
        } while (taken(name));
        block.name = std::move(name);
    }
    return blocks;
}

tl::expected<Project, std::string> projectFromSt(std::string_view source) {
    auto blocks = splitStSource(source);
    if (!blocks) {
        return tl::unexpected(blocks.error());
    }
    Project project;
    project.name = "uploaded";
    for (const auto& block : *blocks) {
        if (block.kind == BlockKind::Program) {
            project.name = block.name;
            break;
        }
    }
    project.blocks = std::move(*blocks);
    return project;
}

} // namespace ladder
