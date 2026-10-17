#include "text/text_document.hpp"
#include <doctest/doctest.h>

using text::Position;
using text::TextDocument;

namespace {

// Puts the cursor at line/column (0-based) with nothing selected.
TextDocument at(const char* content, std::size_t line, std::size_t column) {
    TextDocument doc(content);
    doc.setCursor(Position{line, column});
    return doc;
}

} // namespace

TEST_CASE("Text is kept as lines; tabs become spaces, carriage returns go") {
    TextDocument doc("IF a THEN\r\n\tb := 1;\nEND_IF;");
    REQUIRE(doc.lines().size() == 3);
    CHECK(doc.lines()[1] == "    b := 1;");
    CHECK(doc.text() == "IF a THEN\n    b := 1;\nEND_IF;");
    CHECK(TextDocument("").lines().size() == 1);
    CHECK(TextDocument("x\n").text() == "x\n"); // a final newline is an empty last line
}

TEST_CASE("Typing inserts at the cursor and replaces a selection") {
    auto doc = at("a := 1;", 0, 5);
    doc.insert("2");
    CHECK(doc.text() == "a := 21;");
    CHECK(doc.cursor() == Position{0, 6});
    doc.setCursor(Position{0, 0});
    doc.setCursor(Position{0, 1}, true); // select "a"
    doc.insert("count");
    CHECK(doc.text() == "count := 21;");
    doc.insert("\nx");
    CHECK(doc.text() == "count\nx := 21;");
    CHECK(doc.cursor() == Position{1, 1});
}

TEST_CASE("Enter keeps the indentation") {
    auto doc = at("    IF a THEN", 0, 13);
    doc.newline();
    CHECK(doc.text() == "    IF a THEN\n    ");
    CHECK(doc.cursor() == Position{1, 4});
}

TEST_CASE("Backspace and Delete, also across lines") {
    auto doc = at("ab\ncd", 1, 0);
    doc.backspace(); // joins the lines
    CHECK(doc.text() == "abcd");
    CHECK(doc.cursor() == Position{0, 2});
    doc.deleteForward();
    CHECK(doc.text() == "abd");
    doc.moveToEnd();
    doc.deleteForward(); // nothing after the end
    CHECK(doc.text() == "abd");
    doc.moveToStart();
    doc.backspace(); // nothing before the start
    CHECK(doc.text() == "abd");
}

TEST_CASE("Selected text, over several lines") {
    TextDocument doc("one\ntwo\nthree");
    doc.setCursor(Position{0, 1});
    doc.setCursor(Position{2, 2}, true);
    CHECK(doc.selectedText() == "ne\ntwo\nth");
    doc.backspace();
    CHECK(doc.text() == "oree");
    doc.selectAll();
    CHECK(doc.selectedText() == "oree");
}

TEST_CASE("Up and down keep the column across short lines") {
    auto doc = at("a long line\nab\nanother long", 0, 8);
    doc.moveDown();
    CHECK(doc.cursor() == Position{1, 2}); // the short line's end
    doc.moveDown();
    CHECK(doc.cursor() == Position{2, 8}); // back to column 8
    doc.moveUp();
    doc.moveUp();
    CHECK(doc.cursor() == Position{0, 8});
    doc.moveUp(); // on the first line: to its start
    CHECK(doc.cursor() == Position{0, 0});
}

TEST_CASE("Word jumps, Home and End") {
    auto doc = at("    motor := start AND stop;", 0, 4);
    doc.moveWordRight();
    CHECK(doc.cursor().column == 10); // after "motor "
    doc.moveWordRight();
    CHECK(doc.cursor().column == 13); // after ":= "
    doc.moveWordLeft();
    CHECK(doc.cursor().column == 10);
    doc.moveEnd();
    CHECK(doc.cursor().column == 28);
    doc.moveHome(); // first to the first non-space...
    CHECK(doc.cursor().column == 4);
    doc.moveHome(); // ...then to column 0
    CHECK(doc.cursor().column == 0);
}

TEST_CASE("A double-click selects a word") {
    TextDocument doc("x := motorSpeed + 1;");
    doc.selectWord(Position{0, 8});
    CHECK(doc.selectedText() == "motorSpeed");
    doc.selectWord(Position{0, 2}); // not a word: just that character
    CHECK(doc.selectedText() == ":");
}

TEST_CASE("Moving steps over whole UTF-8 characters") {
    auto doc = at("(* Förderband *)", 0, 4); // after "(* F"
    doc.moveRight();
    CHECK(doc.cursor().column == 6); // ö is two bytes
    doc.moveLeft();
    CHECK(doc.cursor().column == 4);
    doc.setCursor(Position{0, 5}); // the middle of ö: moved back to its start
    CHECK(doc.cursor().column == 4);
    doc.moveRight();
    doc.backspace(); // deletes the whole ö
    CHECK(doc.text() == "(* Frderband *)");
}

TEST_CASE("Tab indents to the next multiple of four, or indents and unindents whole lines") {
    auto doc = at("a := 1;", 0, 1);
    doc.indent(); // three spaces: from column 1 to column 4
    CHECK(doc.text() == "a    := 1;");
    CHECK(doc.cursor() == Position{0, 4});
    TextDocument lines("x;\ny;\nz;");
    lines.setCursor(Position{0, 1});
    lines.setCursor(Position{1, 1}, true);
    lines.indent();
    CHECK(lines.text() == "    x;\n    y;\nz;");
    CHECK(lines.cursor() == Position{1, 5});
    lines.unindent();
    CHECK(lines.text() == "x;\ny;\nz;");
}

TEST_CASE("Undo takes back typing in one step, but stops at cursor moves") {
    auto doc = at("", 0, 0);
    for (const char* c : {"a", "b", "c"}) {
        doc.insert(c);
    }
    doc.moveLeft(); // a move ends the typing step
    doc.insert("X");
    CHECK(doc.text() == "abXc");
    doc.undo();
    CHECK(doc.text() == "abc");
    doc.undo();
    CHECK(doc.text() == "");
    CHECK_FALSE(doc.canUndo());
    doc.redo();
    doc.redo();
    CHECK(doc.text() == "abXc");
    CHECK(doc.cursor() == Position{0, 3});
}

TEST_CASE("Deleting letter after letter is one undo step too") {
    auto doc = at("abcdef", 0, 6);
    doc.backspace();
    doc.backspace();
    doc.backspace();
    CHECK(doc.text() == "abc");
    doc.undo();
    CHECK(doc.text() == "abcdef");
}

TEST_CASE("The version changes with every edit, not with moves") {
    TextDocument doc("abc");
    const auto before = doc.version();
    doc.moveRight();
    CHECK(doc.version() == before);
    doc.insert("x");
    CHECK(doc.version() == before + 1);
}
