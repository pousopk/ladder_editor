# ladder_editor

A graphical editor for PLC programs in **KOP** (ladder logic) and **ST** (Structured Text), like LAD and SCL blocks in TIA Portal. Place contacts, coils, timers and branches on rungs, or write ST, then run the program on `soft_plc` and watch power flow and live values online.

## Decisions

- **Two languages, per block:** each block (PROGRAM, FUNCTION_BLOCK, FUNCTION) is written either in KOP or in ST, and blocks call each other across languages, as in TIA Portal.
- **Execution:** no runtime of its own. KOP blocks are translated into ST, which `soft_plc` compiles and runs together with the ST blocks. The generated ST is shown read-only next to the ladder. Live values come back from `soft_plc` to animate power flow.
- **ST editing:** the ST editor uses `soft_plc`'s own lexer, parser and analyzer for syntax colours and for errors while typing, so the messages are exactly the ones `soft_plc` gives. The editor widget itself is written here (ImGui's text box has no colours).
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
9. Watch table: chosen variables from any block, with live values, Set and Force
10. Clearer online drawing: dashed wires without power, pin values that are never cut off
11. Online change: Download loads changes into the running PLC without losing its values

## Building

`ladder_editor` uses `soft_plc` as a library, from `../soft_plc` next to it (or set `-DLADDER_EDITOR_SOFT_PLC_DIR=...`).

```bash
cmake -S . -B build          # Debug by default, with AddressSanitizer + UBSan
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/ladder_editor                         # the built-in example (not saved)
./build/ladder_editor examples/conveyor       # a project folder: a KOP program and an ST function block
./build/ladder_editor myproject               # a folder without a project: start a new one
./build/ladder_editor --simulate examples/conveyor     # and go online at once, in the simulator
./build/ladder_editor --connect=5020 examples/conveyor # or with a soft_plc running with --monitor=5020
```

## Projects

A project is a folder: `project.json` lists the blocks, and each block has its own file. KOP blocks are `Name.kop.json` (the interface, and each network in the one-line notation); ST blocks are plain `Name.st` files.

The window shows the blocks on the left (New block, Rename, Delete) and the open block on the right. A KOP block has its interface table (name, type, section, initial value, AT address, comment) above its networks. An ST block opens in a code editor with line numbers and colours. Whenever anything changes, the KOP blocks are translated to ST and soft_plc compiles the whole project; its first error is underlined in place in an ST block, or shown under its network in a KOP block. Tick **ST** in a KOP block's toolbar to see its translation next to the networks; **Export ST** writes the whole project as `<project>.st` for soft_plc. Problems in the project, such as two PROGRAMs or a variable declared twice, are listed under the blocks. Closing with unsaved changes asks first.

## Online

**Simulate** runs the project in a soft_plc inside the editor (like PLCSIM); **Connect** goes online with a soft_plc you started yourself (**Export ST**, then `soft_plc --monitor=5020 <project>.st`). Online, the rungs show power flow as in TIA Portal: solid green where power flows, dashed blue where it doesn't, closed contacts green and open ones blue. Box pins show their values under their operands, and hovering a box lists all its pins, uncut; ST lines (and the ST panel of a KOP block) show their variables' values at the end of the line. A function block is watched through one of its instances, chosen in the Instance combo.

To change a value: double-click a contact or coil to flip its operand, or select it and use **Set TRUE/FALSE** (written once) or **Force TRUE/FALSE** (held every cycle, until **Unforce**). In the simulator, inputs keep what you set; a real soft_plc reads its inputs every cycle, so there inputs must be forced. When the project changes, **Download** loads it into the running PLC between two cycles, like a download in RUN in TIA Portal: every variable that's still there keeps its value (a running timer goes on timing, a counter keeps its count), and the header lists the ones that start at their initial value (new, or with a changed type). It works with a standalone soft_plc too. **Restart** (simulator only) starts over with every value reset.

The **Watch table**, under the blocks, lists variables from any block (`start`, `t1.ET`, `p1.demand`) with their type and live value, and a modify value for each: **Set** writes it once, **Force** holds it every cycle, **Set all** writes every line in order. It's saved with the project; the conveyor example's table releases the two normally closed buttons and presses start.

## Editing ST

Arrows, Home/End (Home goes to the first non-space, then column 0), Ctrl+arrows by word, PageUp/PageDown, Shift to select, double-click for a word, Ctrl+A/C/X/V, Tab and Shift+Tab (also on selected lines), Enter keeps the indentation, Ctrl+Z/Ctrl+Y. Hover the red underline to read the error.

## Editing KOP

Click an element (or a network's title) to select it, then:

| Key | Does |
|---|---|
| C / N | insert a normally open / closed contact after the selection |
| B | insert a box: a timer, counter, edge, flip-flop or one of the project's function blocks (arrows + Enter, or click) |
| P / Shift+P | put a branch around the selection / add a side to its branch |
| O | add a coil |
| Ctrl+← / Ctrl+→ | move the selection left / right |
| Enter, double-click | edit the selection: operand, NO/NC, coil kind, box pins |
| Del | delete the selection, or the network if only the network is selected |
| Ctrl+N | add a network after the selected one |
| Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | undo / redo |
| Ctrl+S | save the project |

The same actions are on the toolbar. Every edit keeps the rung valid: there are no loose wires to draw.

The application needs GLFW and OpenGL development files (`libglfw3-dev`, `libgl-dev`); without them, the library and the tests still build.

## Layout

| Path | Contents |
|---|---|
| `src/ladder/` | The model: rungs and elements (`rung.hpp`), the standard boxes (`boxes.hpp`), the checks (`check.hpp`), the text notation (`notation.hpp`), rung files (`rung_file.hpp`) and the layout on a grid (`layout.hpp`) |
| `src/ladder/edit.hpp` | The structured edit operations |
| `src/ladder/history.hpp` | Undo and redo: commands and the history |
| `src/ladder/block.hpp`, `project_file.hpp` | Blocks, projects and their rules; project folders on disk |
| `src/ladder/translate.hpp` | KOP blocks translated to ST, with each line traced back to its network |
| `src/text/` | The code editor's text: lines, cursor, selection, editing, undo |
| `src/ladder/power.hpp` | Where power flows in a rung, from the PLC's values |
| `src/online/` | Online mode: the simulator, the session that reads and writes the PLC's values, operand paths |
| `src/st_support/` | ST colours, the project's box types, and the whole project checked by `soft_plc`'s compiler |
| `src/view/` | Drawing a rung (`rung_view.hpp`) and the code editor widget (`code_editor.hpp`), with Dear ImGui |
| `src/app/` | The application: `main.cpp`, the project window and the KOP editor |
| `examples/` | Example projects |
| `tests/` | doctest unit tests, mirroring `src/`; `tests/ui/` drives the editor's UI headless, with simulated mouse and keys |
| `docs/lessons/` | What each milestone teaches, with review questions |
