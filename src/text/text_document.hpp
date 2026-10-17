#pragma once
#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The text behind the ST code editor: lines, a cursor, a selection, editing
// and undo, with no idea how it's drawn. The editor widget (view/code_editor)
// turns keys and clicks into calls to this class and draws what it holds.
//
// Columns are byte offsets in a line, the same as soft_plc's lexer counts
// them, so an error's line and column point at the same place here. The text
// is UTF-8: moving left and right steps over whole characters.
namespace text {

struct Position {
    std::size_t line = 0;
    std::size_t column = 0;

    auto operator<=>(const Position&) const = default; // compares line first, then column
};

class TextDocument {
public:
    explicit TextDocument(std::string_view text = {});

    // The whole text, lines joined with '\n'. Tabs were turned into spaces
    // when the text was loaded (see the constructor).
    std::string text() const;
    const std::vector<std::string>& lines() const { return lines_; }

    // Goes up by one with every change, so a view knows when to recompute
    // colours and errors.
    std::size_t version() const { return version_; }

    // ------------------------------------------------------------ Cursor and selection

    Position cursor() const { return cursor_; }
    Position anchor() const { return anchor_; } // the other end of the selection
    bool hasSelection() const { return cursor_ != anchor_; }
    Position selectionStart() const { return std::min(cursor_, anchor_); }
    Position selectionEnd() const { return std::max(cursor_, anchor_); }
    std::string selectedText() const;

    // `select` keeps the anchor where it is, extending the selection, like
    // holding Shift.
    void setCursor(Position position, bool select = false);
    void moveLeft(bool select = false);
    void moveRight(bool select = false);
    void moveUp(bool select = false);
    void moveDown(bool select = false);
    void moveWordLeft(bool select = false);
    void moveWordRight(bool select = false);
    void moveHome(bool select = false); // to the first non-space, then to column 0
    void moveEnd(bool select = false);
    void moveToStart(bool select = false);
    void moveToEnd(bool select = false);
    void movePage(int lines, bool select = false);
    void selectAll();
    void selectWord(Position at); // a double-click

    // ------------------------------------------------------------ Editing

    // Types text at the cursor, replacing the selection. May contain newlines.
    void insert(std::string_view text);
    // Enter: a new line indented like the current one.
    void newline();
    void backspace();
    void deleteForward();
    // Tab: four spaces, or with several lines selected, indents them all.
    void indent();
    // Shift+Tab: removes up to four spaces from the start of each selected line.
    void unindent();

    // ------------------------------------------------------------ Undo

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();
    void redo();

private:
    struct Snapshot {
        std::vector<std::string> lines;
        Position cursor;
        Position anchor;
    };
    // What kind of edit happened last: typing runs together into one undo step.
    enum class EditKind { Other, Typing, Deleting };

    void remember(EditKind kind); // saves the state before an edit, for undo
    void changed();
    void eraseSelection();
    Position clamp(Position position) const;
    void moveTo(Position position, bool select);

    std::vector<std::string> lines_;
    Position cursor_;
    Position anchor_;
    std::size_t preferredColumn_ = 0; // up and down keep to this column across short lines
    std::size_t version_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind lastEdit_ = EditKind::Other;
};

} // namespace text
