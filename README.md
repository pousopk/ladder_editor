# ladder_editor

A graphical editor for PLC programs in **KOP** (ladder logic) and **ST** (Structured Text), like LAD and SCL blocks in TIA Portal. Place contacts, coils, timers and branches on rungs, or write ST, then run the program on `soft_plc` and watch power flow and live values online.

## Decisions

- **Two languages, per block:** each block (PROGRAM, FUNCTION_BLOCK, FUNCTION) is written either in KOP or in ST, and blocks call each other across languages, as in TIA Portal.
- **Execution:** no runtime of its own. KOP blocks are translated into ST, which `soft_plc` compiles and runs together with the ST blocks. The generated ST is shown read-only next to the ladder. Live values come back from `soft_plc` to animate power flow.
- **ST editing:** the ST editor uses `soft_plc`'s own lexer, parser and analyzer for syntax colours and for errors while typing, so the messages are exactly the ones `soft_plc` gives.
- **Look:** generic IEC ladder, kept clean and simple.
- **KOP editing:** structured editing (insert elements into rung positions, add branches), so rungs are always valid.
- **Timing:** this project starts once `soft_plc` is mature (it did, after `soft_plc` milestone 9).

## Goals

- Rung editing with contacts (NO/NC), coils (normal, negated, set/reset), timers, counters and other function block boxes, and parallel branches.
- An ST text editor with syntax colours and live compile errors.
- Translation of KOP blocks into ST that `soft_plc` executes.
- Online mode showing power flow on the rungs and live values in both languages, like online mode in TIA Portal.
- Undo/redo, and saving and loading projects.

## Milestones

1. Data model for rungs and elements, with a text notation and unit tests
2. Render rungs in Dear ImGui
3. Structured editing: insert and delete elements, add branches
4. Undo/redo with the command pattern
5. Projects: blocks in KOP or ST, block interfaces, save and load
6. ST editor: text editing with syntax colours and live errors from `soft_plc`'s compiler
7. Translate KOP blocks to ST for `soft_plc`, shown next to the ladder
8. Run and online mode: start `soft_plc`, show power flow and live values in KOP and ST

## Building

```bash
cmake -S . -B build          # Debug by default, with AddressSanitizer + UBSan
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/ladder_editor                         # the built-in example
./build/ladder_editor examples/conveyor.lad   # a rung file: one rung per line, // comments above
```

## Editing

Click an element (or a network's title) to select it, then:

| Key | Does |
|---|---|
| C / N | insert a normally open / closed contact after the selection |
| B | insert a box: a timer, counter, edge or flip-flop (arrows + Enter, or click) |
| P / Shift+P | put a branch around the selection / add a side to its branch |
| O | add a coil |
| Ctrl+← / Ctrl+→ | move the selection left / right |
| Enter, double-click | edit the selection: operand, NO/NC, coil kind, box pins |
| Del | delete the selection, or the network if only the network is selected |
| Ctrl+N | add a network after the selected one |
| Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | undo / redo |
| Ctrl+S | save the rung file |

The same actions are on the toolbar. Every edit keeps the rung valid: there are no loose wires to draw.

The application needs GLFW and OpenGL development files (`libglfw3-dev`, `libgl-dev`); without them, the library and the tests still build.

## Layout

| Path | Contents |
|---|---|
| `src/ladder/` | The model: rungs and elements (`rung.hpp`), the standard boxes (`boxes.hpp`), the checks (`check.hpp`), the text notation (`notation.hpp`), rung files (`rung_file.hpp`) and the layout on a grid (`layout.hpp`) |
| `src/ladder/edit.hpp` | The structured edit operations |
| `src/ladder/history.hpp` | Undo and redo: commands and the history |
| `src/view/` | Drawing a rung with Dear ImGui (`rung_view.hpp`) |
| `src/app/` | The application: `main.cpp` and the editor window |
| `examples/` | Example rung files |
| `tests/` | doctest unit tests, mirroring `src/`; `tests/ui/` drives the editor's UI headless, with simulated mouse and keys |
| `docs/lessons/` | What each milestone teaches, with review questions |
