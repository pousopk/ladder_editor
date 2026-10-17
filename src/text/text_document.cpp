#include "text/text_document.hpp"
#include <algorithm>
#include <cctype>

namespace text {

namespace {

constexpr std::size_t kTabWidth = 4;
constexpr std::size_t kUndoLimit = 1000;

// Tabs become spaces, up to the next multiple of four, so every column is
// one character wide and matches soft_plc's columns.
std::string expandTabs(std::string_view line) {
    std::string result;
    for (const char c : line) {
        if (c == '\t') {
            result.append(kTabWidth - result.size() % kTabWidth, ' ');
        } else if (c != '\r') {
            result += c;
        }
    }
    return result;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    for (;;) {
        const auto newline = text.find('\n');
        lines.push_back(expandTabs(text.substr(0, newline)));
        if (newline == std::string_view::npos) {
            return lines;
        }
        text.remove_prefix(newline + 1);
    }
}

// A UTF-8 byte that continues a character (10xxxxxx) rather than starting one.
bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Letters, digits, _ and anything non-ASCII belong to words.
bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}

enum class CharClass { Space, Word, Other };

CharClass classOf(char c) {
    if (c == ' ')
        return CharClass::Space;
    return isWordChar(c) ? CharClass::Word : CharClass::Other;
}

std::size_t leadingSpaces(const std::string& line) {
    const auto first = line.find_first_not_of(' ');
    return first == std::string::npos ? line.size() : first;
}

} // namespace

TextDocument::TextDocument(std::string_view text) : lines_(splitLines(text)) {}

std::string TextDocument::text() const {
    std::string result;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) {
            result += '\n';
        }
        result += lines_[i];
    }
    return result;
}

std::string TextDocument::selectedText() const {
    const auto start = selectionStart();
    const auto end = selectionEnd();
    if (start.line == end.line) {
        return lines_[start.line].substr(start.column, end.column - start.column);
    }
    std::string result = lines_[start.line].substr(start.column);
    for (auto line = start.line + 1; line < end.line; ++line) {
        result += '\n' + lines_[line];
    }
    result += '\n' + lines_[end.line].substr(0, end.column);
    return result;
}

// ---------------------------------------------------------------- Moving

Position TextDocument::clamp(Position position) const {
    position.line = std::min(position.line, lines_.size() - 1);
    const auto& line = lines_[position.line];
    position.column = std::min(position.column, line.size());
    while (position.column > 0 && position.column < line.size() && isContinuation(line[position.column])) {
        --position.column; // never in the middle of a character
    }
    return position;
}

void TextDocument::moveTo(Position position, bool select) {
    cursor_ = clamp(position);
    if (!select) {
        anchor_ = cursor_;
    }
    lastEdit_ = EditKind::Other; // typing after a move is a new undo step
}

void TextDocument::setCursor(Position position, bool select) {
    moveTo(position, select);
    preferredColumn_ = cursor_.column;
}

void TextDocument::moveLeft(bool select) {
    if (hasSelection() && !select) {
        setCursor(selectionStart());
        return;
    }
    auto p = cursor_;
    if (p.column > 0) {
        const auto& line = lines_[p.line];
        do {
            --p.column;
        } while (p.column > 0 && isContinuation(line[p.column]));
    } else if (p.line > 0) {
        --p.line;
        p.column = lines_[p.line].size();
    }
    setCursor(p, select);
}

void TextDocument::moveRight(bool select) {
    if (hasSelection() && !select) {
        setCursor(selectionEnd());
        return;
    }
    auto p = cursor_;
    const auto& line = lines_[p.line];
    if (p.column < line.size()) {
        do {
            ++p.column;
        } while (p.column < line.size() && isContinuation(line[p.column]));
    } else if (p.line + 1 < lines_.size()) {
        ++p.line;
        p.column = 0;
    }
    setCursor(p, select);
}

void TextDocument::moveUp(bool select) {
    if (cursor_.line == 0) {
        setCursor(Position{0, 0}, select);
        return;
    }
    moveTo(Position{cursor_.line - 1, preferredColumn_}, select); // keeps the preferred column
}

void TextDocument::moveDown(bool select) {
    if (cursor_.line + 1 >= lines_.size()) {
        setCursor(Position{cursor_.line, lines_[cursor_.line].size()}, select);
        return;
    }
    moveTo(Position{cursor_.line + 1, preferredColumn_}, select);
}

void TextDocument::movePage(int lines, bool select) {
    const auto target = static_cast<long>(cursor_.line) + lines;
    const auto last = static_cast<long>(lines_.size()) - 1;
    moveTo(Position{static_cast<std::size_t>(std::clamp(target, 0L, last)), preferredColumn_}, select);
}

void TextDocument::moveWordLeft(bool select) {
    auto p = cursor_;
    const auto& line = lines_[p.line];
    if (p.column == 0) {
        moveLeft(select); // to the end of the line before
        return;
    }
    while (p.column > 0 && line[p.column - 1] == ' ') {
        --p.column;
    }
    if (p.column > 0) {
        const auto kind = classOf(line[p.column - 1]);
        while (p.column > 0 && classOf(line[p.column - 1]) == kind) {
            --p.column;
        }
    }
    setCursor(p, select);
}

void TextDocument::moveWordRight(bool select) {
    auto p = cursor_;
    const auto& line = lines_[p.line];
    if (p.column >= line.size()) {
        moveRight(select); // to the start of the next line
        return;
    }
    const auto kind = classOf(line[p.column]);
    while (p.column < line.size() && classOf(line[p.column]) == kind) {
        ++p.column;
    }
    while (p.column < line.size() && line[p.column] == ' ') {
        ++p.column;
    }
    setCursor(p, select);
}

void TextDocument::moveHome(bool select) {
    const auto indentEnd = leadingSpaces(lines_[cursor_.line]);
    setCursor(Position{cursor_.line, cursor_.column == indentEnd ? 0 : indentEnd}, select);
}

void TextDocument::moveEnd(bool select) {
    setCursor(Position{cursor_.line, lines_[cursor_.line].size()}, select);
}

void TextDocument::moveToStart(bool select) {
    setCursor(Position{0, 0}, select);
}

void TextDocument::moveToEnd(bool select) {
    setCursor(Position{lines_.size() - 1, lines_.back().size()}, select);
}

void TextDocument::selectAll() {
    anchor_ = Position{0, 0};
    cursor_ = Position{lines_.size() - 1, lines_.back().size()};
    preferredColumn_ = cursor_.column;
    lastEdit_ = EditKind::Other;
}

void TextDocument::selectWord(Position at) {
    at = clamp(at);
    const auto& line = lines_[at.line];
    auto start = at.column;
    auto end = at.column;
    if (at.column < line.size() && isWordChar(line[at.column])) {
        while (start > 0 && isWordChar(line[start - 1]))
            --start;
        while (end < line.size() && isWordChar(line[end]))
            ++end;
    } else if (at.column < line.size()) {
        ++end;
    }
    anchor_ = Position{at.line, start};
    cursor_ = Position{at.line, end};
    preferredColumn_ = end;
    lastEdit_ = EditKind::Other;
}

// ---------------------------------------------------------------- Editing

void TextDocument::remember(EditKind kind) {
    // Typing (or deleting) letter after letter is one undo step, until the
    // cursor moves or another kind of edit happens.
    if (kind != EditKind::Other && kind == lastEdit_) {
        return;
    }
    undo_.push_back(Snapshot{lines_, cursor_, anchor_});
    if (undo_.size() > kUndoLimit) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    lastEdit_ = kind;
}

void TextDocument::changed() {
    ++version_;
    preferredColumn_ = cursor_.column;
}

void TextDocument::eraseSelection() {
    if (!hasSelection()) {
        return;
    }
    const auto start = selectionStart();
    const auto end = selectionEnd();
    auto& first = lines_[start.line];
    const auto tail = lines_[end.line].substr(end.column);
    first.erase(start.column);
    first += tail;
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(start.line) + 1,
                 lines_.begin() + static_cast<std::ptrdiff_t>(end.line) + 1);
    cursor_ = anchor_ = start;
}

void TextDocument::insert(std::string_view text) {
    if (text.empty()) {
        return;
    }
    const bool typing = text.size() == 1 && text != "\n";
    remember(typing && !hasSelection() ? EditKind::Typing : EditKind::Other);
    eraseSelection();

    const auto pieces = splitLines(text);
    auto& line = lines_[cursor_.line];
    const auto after = line.substr(cursor_.column);
    line.erase(cursor_.column);
    line += pieces.front();
    if (pieces.size() == 1) {
        cursor_.column = line.size();
        line += after;
    } else {
        std::vector<std::string> added(pieces.begin() + 1, pieces.end());
        cursor_ = Position{cursor_.line + added.size(), added.back().size()};
        added.back() += after;
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(cursor_.line - added.size()) + 1, added.begin(),
                      added.end());
    }
    anchor_ = cursor_;
    changed();
}

void TextDocument::newline() {
    // The new line starts indented like this one (but not further than the cursor).
    const auto indent = std::min(leadingSpaces(lines_[cursor_.line]), hasSelection() ? 0 : cursor_.column);
    insert("\n" + std::string(indent, ' '));
}

void TextDocument::backspace() {
    if (hasSelection()) {
        remember(EditKind::Other);
        eraseSelection();
        changed();
        return;
    }
    if (cursor_ == Position{0, 0}) {
        return;
    }
    remember(EditKind::Deleting);
    anchor_ = cursor_;
    moveLeft(true);
    lastEdit_ = EditKind::Deleting; // moveLeft reset it; this is still the same deleting run
    eraseSelection();
    changed();
}

void TextDocument::deleteForward() {
    if (hasSelection()) {
        remember(EditKind::Other);
        eraseSelection();
        changed();
        return;
    }
    if (cursor_.line + 1 == lines_.size() && cursor_.column == lines_.back().size()) {
        return;
    }
    remember(EditKind::Deleting);
    anchor_ = cursor_;
    moveRight(true);
    lastEdit_ = EditKind::Deleting;
    eraseSelection();
    changed();
}

void TextDocument::indent() {
    const auto start = selectionStart();
    const auto end = selectionEnd();
    if (start.line == end.line) {
        insert(std::string(kTabWidth - cursor_.column % kTabWidth, ' '));
        return;
    }
    remember(EditKind::Other);
    // A selection ending at column 0 doesn't include that line.
    const auto last = end.column == 0 ? end.line - 1 : end.line;
    for (auto line = start.line; line <= last; ++line) {
        lines_[line].insert(0, kTabWidth, ' ');
    }
    for (auto* p : {&cursor_, &anchor_}) {
        if (p->line >= start.line && p->line <= last && !(p->column == 0 && p->line == end.line)) {
            p->column += kTabWidth;
        }
    }
    changed();
}

void TextDocument::unindent() {
    const auto start = selectionStart();
    const auto end = selectionEnd();
    const auto last = end.line > start.line && end.column == 0 ? end.line - 1 : end.line;
    remember(EditKind::Other);
    for (auto line = start.line; line <= last; ++line) {
        const auto removed = std::min(leadingSpaces(lines_[line]), kTabWidth);
        lines_[line].erase(0, removed);
        for (auto* p : {&cursor_, &anchor_}) {
            if (p->line == line) {
                p->column -= std::min(p->column, removed);
            }
        }
    }
    changed();
}

// ---------------------------------------------------------------- Undo

void TextDocument::undo() {
    if (undo_.empty()) {
        return;
    }
    redo_.push_back(Snapshot{lines_, cursor_, anchor_});
    auto snapshot = std::move(undo_.back());
    undo_.pop_back();
    lines_ = std::move(snapshot.lines);
    cursor_ = snapshot.cursor;
    anchor_ = snapshot.anchor;
    lastEdit_ = EditKind::Other;
    changed();
}

void TextDocument::redo() {
    if (redo_.empty()) {
        return;
    }
    undo_.push_back(Snapshot{lines_, cursor_, anchor_});
    auto snapshot = std::move(redo_.back());
    redo_.pop_back();
    lines_ = std::move(snapshot.lines);
    cursor_ = snapshot.cursor;
    anchor_ = snapshot.anchor;
    lastEdit_ = EditKind::Other;
    changed();
}

} // namespace text
