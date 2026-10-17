#include "view/code_editor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace view {

namespace {

const ImU32 kText = IM_COL32(215, 218, 224, 255);
const ImU32 kLineNumber = IM_COL32(110, 115, 125, 255);
const ImU32 kErrorRed = IM_COL32(240, 90, 80, 255);
const ImU32 kSelection = IM_COL32(70, 110, 170, 140);
const ImU32 kCursor = IM_COL32(235, 235, 240, 255);
const ImU32 kCurrentLine = IM_COL32(255, 255, 255, 12);

// One character typed (a Unicode code point) as UTF-8.
std::string utf8(unsigned int c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}

float lineHeight() {
    return ImGui::GetTextLineHeightWithSpacing();
}

} // namespace

float CodeEditor::columnX(std::size_t line, std::size_t column) const {
    const auto& text = document_.lines()[line];
    column = std::min(column, text.size());
    return ImGui::CalcTextSize(text.data(), text.data() + column).x;
}

text::Position CodeEditor::positionAt(ImVec2 mouse, ImVec2 origin, float gutter) const {
    const auto& lines = document_.lines();
    const float row = std::floor((mouse.y - origin.y) / lineHeight());
    const auto line = static_cast<std::size_t>(std::clamp(row, 0.0F, static_cast<float>(lines.size() - 1)));
    const float x = mouse.x - origin.x - gutter;
    const auto& text = lines[line];
    // The character boundary nearest to the mouse.
    std::size_t column = 0;
    float left = 0.0F;
    while (column < text.size()) {
        auto next = column + 1;
        while (next < text.size() && isContinuation(text[next])) {
            ++next;
        }
        const float right = ImGui::CalcTextSize(text.data(), text.data() + next).x;
        if (x < (left + right) / 2.0F) {
            break;
        }
        column = next;
        left = right;
    }
    return text::Position{line, column};
}

void CodeEditor::handleKeyboard() {
    const auto& io = ImGui::GetIO();
    const bool shift = io.KeyShift;
    const bool ctrl = io.KeyCtrl;
    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key); };
    auto& doc = document_;
    const auto before = std::pair{doc.cursor(), doc.version()};
    const int page = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / lineHeight()) - 1);

    if (ctrl) {
        if (pressed(ImGuiKey_A))
            doc.selectAll();
        if (pressed(ImGuiKey_Z))
            shift ? doc.redo() : doc.undo();
        if (pressed(ImGuiKey_Y))
            doc.redo();
        if (pressed(ImGuiKey_C) && doc.hasSelection())
            ImGui::SetClipboardText(doc.selectedText().c_str());
        if (pressed(ImGuiKey_X) && doc.hasSelection()) {
            ImGui::SetClipboardText(doc.selectedText().c_str());
            doc.backspace();
        }
        if (pressed(ImGuiKey_V)) {
            if (const char* clipboard = ImGui::GetClipboardText()) {
                doc.insert(clipboard);
            }
        }
        if (pressed(ImGuiKey_LeftArrow))
            doc.moveWordLeft(shift);
        if (pressed(ImGuiKey_RightArrow))
            doc.moveWordRight(shift);
        if (pressed(ImGuiKey_Home))
            doc.moveToStart(shift);
        if (pressed(ImGuiKey_End))
            doc.moveToEnd(shift);
    } else {
        if (pressed(ImGuiKey_LeftArrow))
            doc.moveLeft(shift);
        if (pressed(ImGuiKey_RightArrow))
            doc.moveRight(shift);
        if (pressed(ImGuiKey_Home))
            doc.moveHome(shift);
        if (pressed(ImGuiKey_End))
            doc.moveEnd(shift);
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter))
            doc.newline();
        if (pressed(ImGuiKey_Backspace))
            doc.backspace();
        if (pressed(ImGuiKey_Delete))
            doc.deleteForward();
        if (pressed(ImGuiKey_Tab))
            shift ? doc.unindent() : doc.indent();
        // Typed characters (the backend turns key presses into these,
        // with the keyboard layout applied).
        for (const ImWchar c : io.InputQueueCharacters) {
            if (c >= 32 && c != 127) {
                doc.insert(utf8(c));
            }
        }
    }
    if (pressed(ImGuiKey_UpArrow))
        doc.moveUp(shift);
    if (pressed(ImGuiKey_DownArrow))
        doc.moveDown(shift);
    if (pressed(ImGuiKey_PageUp))
        doc.movePage(-page, shift);
    if (pressed(ImGuiKey_PageDown))
        doc.movePage(page, shift);
    if (std::pair{doc.cursor(), doc.version()} != before) {
        cursorMoved_ = true;
    }
}

void CodeEditor::handleMouse(ImVec2 origin, float gutter) {
    const auto& io = ImGui::GetIO();
    // The text area: the window minus its scrollbars.
    const ImVec2 windowMin = ImGui::GetWindowPos();
    ImVec2 windowMax{windowMin.x + ImGui::GetWindowWidth(), windowMin.y + ImGui::GetWindowHeight()};
    if (ImGui::GetScrollMaxY() > 0.0F)
        windowMax.x -= ImGui::GetStyle().ScrollbarSize;
    if (ImGui::GetScrollMaxX() > 0.0F)
        windowMax.y -= ImGui::GetStyle().ScrollbarSize;
    const bool overText = ImGui::IsWindowHovered() && io.MousePos.x >= windowMin.x && io.MousePos.x < windowMax.x &&
                          io.MousePos.y >= windowMin.y && io.MousePos.y < windowMax.y;
    if (overText) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    if (overText && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto position = positionAt(io.MousePos, origin, gutter);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            document_.selectWord(position);
            selectingWithMouse_ = false;
        } else {
            document_.setCursor(position, io.KeyShift); // Shift+click extends the selection
            selectingWithMouse_ = true;
        }
    } else if (selectingWithMouse_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        document_.setCursor(positionAt(io.MousePos, origin, gutter), true); // dragging selects
        cursorMoved_ = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        selectingWithMouse_ = false;
    }
}

void CodeEditor::scrollToCursor(ImVec2 visibleSize, float gutter) {
    const auto cursor = document_.cursor();
    const float top = static_cast<float>(cursor.line) * lineHeight();
    if (top < ImGui::GetScrollY()) {
        ImGui::SetScrollY(top);
    } else if (top + lineHeight() > ImGui::GetScrollY() + visibleSize.y) {
        ImGui::SetScrollY(top + lineHeight() - visibleSize.y);
    }
    const float x = gutter + columnX(cursor.line, cursor.column);
    const float margin = ImGui::CalcTextSize("0000").x;
    if (x - gutter < ImGui::GetScrollX()) {
        ImGui::SetScrollX(std::max(0.0F, x - gutter - margin));
    } else if (x + margin > ImGui::GetScrollX() + visibleSize.x) {
        ImGui::SetScrollX(x + margin - visibleSize.x);
    }
}

bool CodeEditor::draw(const char* id, ImVec2 size) {
    const auto versionBefore = document_.version();
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    const bool focused = ImGui::IsWindowFocused();
    if (focused) {
        ImGui::SetNextFrameWantCaptureKeyboard(true); // the keys are ours while we have the focus
        handleKeyboard();
    }

    const auto& lines = document_.lines();
    const float lineH = lineHeight();
    const float charW = ImGui::CalcTextSize("0").x;
    const auto digits = std::max<std::size_t>(3, std::to_string(lines.size()).size());
    const float gutter = charW * static_cast<float>(digits + 2);
    const ImVec2 origin = ImGui::GetCursorScreenPos(); // the top-left of line 0, scrolled with the content
    handleMouse(origin, gutter);

    auto* draw = ImGui::GetWindowDrawList();
    const auto cursor = document_.cursor();
    const auto selStart = document_.selectionStart();
    const auto selEnd = document_.selectionEnd();
    const auto first = static_cast<std::size_t>(std::max(0.0F, ImGui::GetScrollY() / lineH));
    const auto last = std::min(lines.size(), first + static_cast<std::size_t>(ImGui::GetWindowHeight() / lineH) + 2);
    const float textX = origin.x + gutter;

    for (auto line = first; line < last; ++line) {
        const float y = origin.y + static_cast<float>(line) * lineH;
        const auto& text = lines[line];
        if (line == cursor.line && focused) {
            draw->AddRectFilled({ImGui::GetWindowPos().x, y}, {ImGui::GetWindowPos().x + 1e5F, y + lineH},
                                kCurrentLine);
        }
        // The line number, right-aligned in the gutter; red on the error's line.
        const auto number = std::to_string(line + 1);
        const bool errorLine = error_ && error_->line == line;
        draw->AddText({textX - charW * (static_cast<float>(number.size()) + 1.0F), y},
                      errorLine ? kErrorRed : kLineNumber, number.c_str());
        // The selection on this line.
        if (selStart != selEnd && line >= selStart.line && line <= selEnd.line) {
            const float from = line == selStart.line ? columnX(line, selStart.column) : 0.0F;
            const float to = line == selEnd.line ? columnX(line, selEnd.column) : columnX(line, text.size()) + charW;
            draw->AddRectFilled({textX + from, y}, {textX + to, y + lineH}, kSelection);
        }
        // The text, in pieces: plain between spans, coloured inside them.
        std::size_t done = 0;
        const auto piece = [&](std::size_t from, std::size_t to, ImU32 color) {
            if (to > from) {
                draw->AddText({textX + columnX(line, from), y}, color, text.data() + from, text.data() + to);
            }
        };
        for (const auto& span : spans_) {
            if (span.line != line || span.column < done || span.column >= text.size()) {
                continue;
            }
            piece(done, span.column, kText);
            done = std::min(text.size(), span.column + span.length);
            piece(span.column, done, span.color);
        }
        piece(done, text.size(), kText);
    }

    // The error: a wavy red line under the word it points at, its message on hover.
    if (error_ && error_->line < lines.size()) {
        const auto& text = lines[error_->line];
        auto end = std::min(error_->column, text.size());
        while (end < text.size() && isWordChar(text[end]))
            ++end;
        if (end == error_->column)
            end = std::min(text.size(), end + 1);
        const float x0 = textX + columnX(error_->line, error_->column);
        const float x1 = std::max(x0 + charW, textX + columnX(error_->line, end));
        const float y = origin.y + static_cast<float>(error_->line + 1) * lineH - 2.0F;
        for (float x = x0; x < x1; x += 4.0F) {
            draw->AddLine({x, y}, {std::min(x + 2.0F, x1), y - 2.0F}, kErrorRed);
            draw->AddLine({std::min(x + 2.0F, x1), y - 2.0F}, {std::min(x + 4.0F, x1), y}, kErrorRed);
        }
        const auto mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsWindowHovered() && mouse.x >= x0 && mouse.x < x1 && mouse.y >= y - lineH && mouse.y < y + 3.0F) {
            ImGui::SetTooltip("%s", error_->message.c_str());
        }
    }

    // The cursor, blinking while the editor has the focus.
    if (focused && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
        const float x = textX + columnX(cursor.line, cursor.column);
        const float y = origin.y + static_cast<float>(cursor.line) * lineH;
        draw->AddLine({x, y}, {x, y + lineH}, kCursor, 1.5F);
    }

    // The whole text's size, so the child window can scroll over it.
    float width = 0.0F;
    for (const auto& text : lines) {
        width = std::max(width, ImGui::CalcTextSize(text.data(), text.data() + text.size()).x);
    }
    ImGui::Dummy({gutter + width + charW * 4.0F, static_cast<float>(lines.size()) * lineH});
    if (cursorMoved_) {
        scrollToCursor({ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize,
                        ImGui::GetWindowHeight() - ImGui::GetStyle().ScrollbarSize},
                       gutter);
        cursorMoved_ = false;
    }
    ImGui::EndChild();
    return document_.version() != versionBefore;
}

} // namespace view
