# Lesson 6: The ST editor

Milestone 6 turns the ST blocks' plain text box into a **code editor**: line numbers, colours, a real cursor and selection, undo, and **errors while you type**, all from soft_plc's own compiler.

```
keys, mouse ──► view::CodeEditor ──► text::TextDocument       (the text, cursor, selection, undo)
                     │
                     ├── colours ◄── st_support::highlight    ◄── soft_plc's lexer
                     └── error   ◄── st_support::checkStBlocks ◄── soft_plc's parser and analyzer
```

Try it: `./build/ladder_editor examples/conveyor`, open BoxRate, and break something: change `count + 1` to `count + TRUE`.

---

## Part 1: Using soft_plc as a library

### 1.1 One compiler, two places
The editor doesn't have its own idea of what ST is. It **links soft_plc's own lexer, parser and analyzer** (`soft_plc_language`), so:
- a keyword is coloured exactly when soft_plc reads it as a keyword;
- an error shows the exact message, line and column soft_plc would give when running the project.

If soft_plc learns something new (a new keyword, a new check), the editor follows without any change.

### 1.2 How the projects are joined
soft_plc's `CMakeLists.txt` now has two libraries: `soft_plc_language` (ST and the standard library, no networking) and `soft_plc_core` (that plus the scan cycle, monitoring and S7). When soft_plc is included by another project, its programs and tests are skipped (`SOFT_PLC_BUILD_PROGRAMS` defaults to `PROJECT_IS_TOP_LEVEL`). ladder_editor includes it with:
```cmake
add_subdirectory(${LADDER_EDITOR_SOFT_PLC_DIR} soft_plc EXCLUDE_FROM_ALL)
```
`EXCLUDE_FROM_ALL` builds only what ladder_editor actually links.

### 1.3 Checking several blocks as one program
soft_plc compiles one source file with one PROGRAM. So `checkStBlocks` joins the project's ST blocks one after the other, remembering on which line each starts. If the PROGRAM is a KOP block (not translated yet: that's milestone 7), it adds a stand-in `PROGRAM StandIn END_PROGRAM`. An error's line in the joined text then maps back to its block and its own line (`std::upper_bound` over the start lines).

Before compiling, each block's **header** is checked against the project: block Mixer must start with `FUNCTION_BLOCK Mixer`, and must hold only that one POU.

---

## Part 2: The editor's design

### 2.1 Model and view, again
`text::TextDocument` is the text: a `std::vector<std::string>` of lines, a cursor and an anchor (the selection is between them), and every edit: insert, newline with indentation, backspace/delete across lines, word jumps, smart Home, indent/unindent of selected lines, undo/redo. It has no idea how it's drawn, so it has 13 unit tests of its own.

`view::CodeEditor` turns keys and clicks into calls to the document, and draws it. It doesn't know about ST either: it gets coloured spans and an error mark from outside, so it could edit any language.

### 2.2 Drawing only what's visible
Each frame draws only the lines that fit in the window (`first = scrollY / lineHeight`). An `ImGui::Dummy` the size of the whole text makes the child window's scrollbars work. Each line is drawn in pieces: plain text between spans, coloured text inside them.

### 2.3 Undo steps for typing
Typing letter after letter is one undo step; so is deleting with Backspace held. A step ends when the cursor moves or another kind of edit happens (`EditKind` in the document). Each step is a snapshot of the lines, like the KOP editor's `ReplaceRung`.

### 2.4 Bytes, characters and columns
soft_plc's lexer counts columns in **bytes**. The document does too, so an error's column points at the same place in both. But text is UTF-8: `ö` is two bytes. The cursor must never stop in the middle of a character, so moving left and right skips continuation bytes (`10xxxxxx`), and clicking finds the nearest character boundary. Tabs become four spaces when a text is loaded, so every column is one character wide.

### 2.5 Recovering from errors while colouring
While you type, the text is often broken. The lexer stops at the first character it can't read, but the colours shouldn't stop there: the highlighter colours what came before, skips that character, and starts lexing again after it. An unclosed `(*` colours everything after it as a comment, which is what it will be once you close it.

---

## Part 3: C++ concepts

### 3.1 `operator<=>` for positions
```cpp
struct Position {
    std::size_t line = 0;
    std::size_t column = 0;
    auto operator<=>(const Position&) const = default;
};
```
A defaulted three-way comparison compares members in order (line first, then column), which is exactly "earlier in the text". `std::min(cursor, anchor)` then gives the selection's start.

### 3.2 Encoding UTF-8 by hand
ImGui hands typed characters over as code points (`ImWchar`); `utf8()` turns each into one to four bytes: `0xxxxxxx`, `110xxxxx 10xxxxxx`, ... It's the most compact way to see how UTF-8 is built.

### 3.3 Re-checking in the same frame
A real bug, found by a UI test: the window decided whether the open block had the error, *then* drew the editor, which re-checked the code after an edit. When an edit fixed the error, the diagnostic disappeared, and the code below still read `stDiagnostic_->...` on an empty `std::optional`: a crash (the debug build's assertion caught it). The fix: decide after the re-check, with a small lambda (`errorIn(index)`) that looks at the current state each time.

---

## Part 4: Testing

- `text/text_document_test.cpp` (13 cases): tabs and line ends, typing over a selection, Enter's indentation, deleting across lines, multi-line selections, up/down keeping the column, word jumps and smart Home, double-click words, UTF-8 characters, indent/unindent, undo grouping.
- `st/st_support_test.cpp` (9 cases): every style; comments on one or several lines; built-ins in any case; going on after an unreadable character; an unclosed comment; diagnostics in the right block, line and column with soft_plc's message; header checks; types from a TYPE block.
- `ui/project_ui_test.cpp` (4 new cases): typing with Enter's indentation; an error appearing while typing and disappearing with undo; select all, copy and paste; clicking the problem to open its block.
- By eye: the editor with an error (the screenshot in the milestone report).

---

## Review questions

**1.** Why does the editor use soft_plc's lexer and analyzer instead of its own list of keywords and rules?
<details><summary>Answer</summary>

So it can never disagree with soft_plc: what's coloured as a keyword is what soft_plc reads as one, and an error shows soft_plc's own message at soft_plc's own line and column. Any change to soft_plc's language reaches the editor automatically.
</details>

**2.** The PROGRAM is a KOP block. How can soft_plc compile the ST blocks without it?
<details><summary>Answer</summary>

`checkStBlocks` adds a stand-in `PROGRAM StandIn END_PROGRAM` to the joined text, because soft_plc needs exactly one PROGRAM. Once KOP blocks are translated (milestone 7), the real one takes its place.
</details>

**3.** An error is at line 30 of the joined text. Mixer starts at line 0 (12 lines) and Main at line 12. Where is the error?
<details><summary>Answer</summary>

In Main, at its line 18 (30 − 12), counting from 0. `upper_bound` finds the first block starting after line 30, and the one before it is the block that contains the line.
</details>

**4.** Why must the cursor skip bytes like `10xxxxxx` when moving left and right?
<details><summary>Answer</summary>

They continue a UTF-8 character that started earlier. Stopping on one would put the cursor inside a character, and typing there would break it into invalid bytes.
</details>

**5.** You type `IF a THEN`, then press ← once, then type `X`. How many Ctrl+Z bring back the empty text?
<details><summary>Answer</summary>

Two. The typing before the move is one step; the move ends it, and `X` starts a new step.
</details>

**6.** How did the UI test find the crash in section 3.3, and why didn't the unit tests?
<details><summary>Answer</summary>

The UI test typed an error and then undid it, so the error disappeared in the middle of a frame; the window then read the empty diagnostic. The unit tests checked the diagnostics and the editor separately, and the bug was in how the window combined them in one frame.
</details>
