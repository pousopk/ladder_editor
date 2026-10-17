#pragma once
#include "text/text_document.hpp"
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A code editor widget for Dear ImGui: line numbers, coloured text, a
// selection, a blinking cursor, and an error marked with a wavy underline
// and its message. The text and its editing live in text::TextDocument;
// this class turns keys and mouse into calls to it, and draws it.
//
// It doesn't know about Structured Text: the colours and the error are
// given to it (see st_support), so it could edit any language.
namespace view {

struct ColoredSpan {
    std::size_t line;
    std::size_t column;
    std::size_t length;
    ImU32 color;
};

struct ErrorMark {
    std::size_t line;
    std::size_t column;
    std::string message;
};

class CodeEditor {
public:
    explicit CodeEditor(std::string_view text = {}) : document_(text) {}

    text::TextDocument& document() { return document_; }
    const text::TextDocument& document() const { return document_; }

    void setColors(std::vector<ColoredSpan> spans) { spans_ = std::move(spans); }
    void setError(std::optional<ErrorMark> error) { error_ = std::move(error); }
    // A note at the end of each line (index = line), such as online values; empty for none.
    void setNotes(std::vector<std::string> notes) { notes_ = std::move(notes); }
    const std::vector<std::string>& notes() const { return notes_; }
    // Read-only: the text can be selected and copied, not changed.
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }

    // Draws the editor as a child window of `size` and handles its input
    // while it has the focus. Returns true when the text changed this frame.
    bool draw(const char* id, ImVec2 size);

private:
    void handleKeyboard();
    void handleMouse(ImVec2 origin, float gutter);
    text::Position positionAt(ImVec2 mouse, ImVec2 origin, float gutter) const;
    float columnX(std::size_t line, std::size_t column) const; // from the start of the text area
    void scrollToCursor(ImVec2 visibleSize, float gutter);

    text::TextDocument document_;
    std::vector<ColoredSpan> spans_;
    std::optional<ErrorMark> error_;
    std::vector<std::string> notes_;
    bool readOnly_ = false;
    bool selectingWithMouse_ = false;
    bool cursorMoved_ = false; // scroll it into view after drawing
};

} // namespace view
