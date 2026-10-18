# 4. Standing on soft_plc

An editor for a PLC language has to know the language: to colour it, to report errors, to run it. The tempting route is to write that knowledge again inside the editor: a quick ST highlighter with a keyword list, a KOP simulator that evaluates rungs directly. ladder_editor does none of that. It uses soft_plc's own code for everything about the language, and this note is about why, and how the two projects fit together.

---

## One definition of the language

If the editor had its own keyword list, its own type rules or its own idea of what a TON does, then sooner or later it would **disagree with the PLC**: a program the editor accepts and soft_plc rejects, a rung that behaves one way in the simulator and another way on the PLC. Every such disagreement is a bug that's hard to see, because each side looks right on its own.

So:
- **Colours** come from soft_plc's lexer (`st_support/highlight.cpp`): a word is coloured as a keyword exactly when soft_plc reads it as one.
- **Errors** come from soft_plc's parser and analyzer (`st_support/diagnostics.cpp`): the editor shows soft_plc's own message, at soft_plc's own line and column.
- **The simulator** *is* soft_plc: its scan cycle, interpreter and monitor service, running on a thread inside the editor (`online/simulator.cpp`).
- **The project's function blocks** are found by parsing ST blocks with soft_plc's parser (`projectBoxes`).

A new keyword or a new check in soft_plc reaches the editor with no change in the editor.

---

## KOP is translated, not interpreted

The biggest consequence: KOP blocks don't have a runtime of their own. They're **translated into ST** (`ladder/translate.cpp`), and soft_plc compiles and runs the result together with the ST blocks.

The alternative was a KOP interpreter in the editor: walk the rung tree, AND and OR the contacts, call timers. It would have been quicker to write for simple rungs, and it would have meant **two implementations** of every function block, of the scan cycle, of timing, of the memory model, and of how KOP blocks call ST blocks and back. Through translation:
- a KOP block and an ST block can call each other, because after translation they're the same thing;
- the simulator, Export ST and a standalone soft_plc all run **exactly** the same code;
- the user can tick **ST** and read what their rungs mean, which is also how a real bug in the conveyor example was found (lesson 7).

The price is that every error soft_plc reports is about the **generated** ST, and must be brought back to the ladder.

### Bringing errors back

The translator records where each part of its output came from:
- `TranslatedProject::parts`: the line where each block starts in the whole program;
- `TranslatedBlock::origins`: the line where each network's code starts within its block.

Both lists are **sorted by line**, so finding where an error's line came from is a binary search: `std::upper_bound` finds the first part that starts *after* the line, and the one before it is the part that contains it (`blockAt`, `networkAt`). An error at line 57 becomes "block Main, network 8", and the KOP editor shows it under that network.

Sorted vectors plus `upper_bound` is the standard way to map positions to ranges. A `std::map` from start line to part would also work (`map::upper_bound` does the same search); the vectors are built in order anyway and never change, which is exactly when a sorted vector beats a map.

---

## How the projects are joined

soft_plc is used **from source**, as a CMake subproject:

```cmake
add_subdirectory(${LADDER_EDITOR_SOFT_PLC_DIR} soft_plc EXCLUDE_FROM_ALL)
```

To make that clean, soft_plc's build was split (a soft_plc change made for the editor): `soft_plc_language` (lexer, parser, analyzer, interpreter, standard library) and `soft_plc_core` (that plus the scan cycle, monitoring and S7). When soft_plc isn't the top-level project, its programs and tests aren't built. `EXCLUDE_FROM_ALL` builds only the targets the editor actually links.

The editor links them **privately** where it can:

```cmake
target_link_libraries(ladder_st PUBLIC ladder_core PRIVATE soft_plc_language fmt::fmt)
target_link_libraries(ladder_online PUBLIC ladder_core PRIVATE soft_plc_core)
```

`PRIVATE` means code that uses `ladder_st` doesn't see soft_plc's headers. The UI code includes `st_support/diagnostics.hpp`, which talks in the editor's own types (`StDiagnostic`, `BoxCatalog`), not soft_plc's AST. If soft_plc's internals change, only `st_support` and `online` need to follow.

`online::Simulator` takes this one step further with the **pimpl** idiom: its header declares `struct State;` and holds a `std::unique_ptr<State>`; the scan cycle, the program and the server are only named in `simulator.cpp`. The project window can own a simulator without its file knowing that soft_plc exists.

---

## The same road for the simulator and a real soft_plc

The simulator listens on a free port, and the editor talks to it **over TCP**, with soft_plc's monitor protocol, exactly as it talks to a soft_plc running on its own. It could have called the interpreter directly, since it's in the same process, which would have been faster. Using the protocol means:
- **one code path** (`online::Session`) for both, so the remote case is tested by every simulator test;
- **the same timing**: requests answered between cycles, values from one consistent moment;
- features like the online change worked for both the moment `LOAD` existed.

The cost is latency, not CPU: each request waits for the next gap between two cycles (up to 10 ms), and a round of the session sends several requests one after another (commands, `READ`, `STATUS`, `FORCES`), so a round takes a few cycles. For values refreshed about ten times a second, that's invisible.

---

## Review questions

**1.** Why do KOP blocks get translated to ST, instead of the editor simulating rungs directly?
<details><summary>Answer</summary>

So there's one runtime: function blocks, timers, the scan cycle and calls between KOP and ST blocks exist once, in soft_plc, and the simulator, Export ST and a real soft_plc all run the same code. A KOP simulator in the editor would be a second implementation that could disagree with the PLC.
</details>

**2.** soft_plc reports an error at line 57 of the whole program. Describe the two lookups that turn it into "Main, network 8".
<details><summary>Answer</summary>

`blockAt(57)`: `upper_bound` over the blocks' start lines finds the first block starting after line 57; the one before it contains the line, giving the block and the line within it. Then `networkAt(line)` does the same over that block's network start lines, giving the network (or none, for the interface).
</details>

**3.** What does linking soft_plc `PRIVATE`ly protect the editor's UI code from?
<details><summary>Answer</summary>

From depending on soft_plc's headers. The UI only sees the editor's own types; if soft_plc's AST or interpreter changes, only `st_support` and `online` (the code that links it) need changes and recompiling.
</details>

**4.** The simulator runs in the editor's own process, yet the editor talks to it over TCP. Why?
<details><summary>Answer</summary>

So the simulator and a standalone soft_plc use one code path (the session and the monitor protocol): the remote case is tested by every simulator test, the timing is identical (requests answered between cycles), and new protocol features work for both at once. The cost is a few cycles of latency per round of reads, invisible at about ten refreshes a second.
</details>
