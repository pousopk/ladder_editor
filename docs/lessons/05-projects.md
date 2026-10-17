# Lesson 5: Projects

Milestone 5 turns the editor from "one list of rungs" into a **project**: a set of blocks, each a PROGRAM, FUNCTION_BLOCK, FUNCTION or TYPE block, written in KOP or ST, saved as a folder.

```
examples/conveyor/
  project.json        { "name": "conveyor", "blocks": [ {Main, PROGRAM, KOP}, {BoxRate, FUNCTION_BLOCK, ST} ] }
  Main.kop.json       the interface (19 variables, with I/O addresses) and 7 networks
  BoxRate.st          plain Structured Text
```

Try it: `./build/ladder_editor examples/conveyor`, or `./build/ladder_editor myproject` for a new one.

---

## Part 1: PLC concepts

### 1.1 Blocks, like in TIA Portal
| Kind | In TIA Portal | Here |
|---|---|---|
| PROGRAM | OB1 | the block the PLC runs every cycle; exactly one per project |
| FUNCTION_BLOCK | FB | logic with memory, used through instances |
| FUNCTION | FC with a return value | logic without memory |
| TYPE | PLC data types (UDTs) | struct types; ST only |

Each block is KOP or ST, and the project mixes them, like LAD and SCL blocks in one TIA project. Milestone 7 translates the KOP blocks to ST, and soft_plc compiles the whole project as one program.

### 1.2 The block interface
A KOP block's variables are a table: name, type, section, initial value, address and comment, like a block interface in TIA. The sections a block can have depend on its kind:

| Kind | Sections |
|---|---|
| PROGRAM | VAR only (with `AT` addresses for I/O) |
| FUNCTION_BLOCK | VAR_INPUT, VAR_OUTPUT, VAR |
| FUNCTION | VAR_INPUT, and VAR for temporaries; plus a return type |

Every operand used in the rungs should be declared there. The example declares `start AT %IX0.0` with the comment "start button": the address says where the signal is, the comment says what it is. Both help the next person who reads the program.

---

## Part 2: Design concepts

### 2.1 A folder of small text files
A project could be one big file. A folder with one file per block has advantages:
- **Version control:** changing one block changes one file, and `git diff` shows exactly what changed.
- **Readable formats:** KOP blocks are JSON with each network as its one-line notation (`"rung": "(start | running) stop eStop => running"`); ST blocks are plain `.st` files you can open in any editor or give straight to soft_plc.
- **Partial damage:** a broken file breaks one block, and the error message names it ("Main.kop.json, network 2: column 8: ...").

### 2.2 Saving safely
**Code:** `writeAtomically()` in `src/ladder/project_file.cpp`

Each file is written to `Name.tmp` first, then **renamed** over the old one. A rename is atomic on Linux: at any moment the file is either completely old or completely new. A crash or a full disk in the middle of saving can't leave a half-written block.

Saving also removes the files of blocks that were deleted or renamed, but only those the previous `project.json` listed, so your own files in the folder are never touched.

### 2.3 Round trips again
A test loads `examples/conveyor`, saves it to a new folder, and compares the files byte for byte. They must be identical, which proves the example is in exactly the editor's own format, and that load and save lose nothing.

That test found a real bug: an operand not filled in yet (the `???` in the editor) was written as nothing (`=> `) and couldn't be read back. Saving an unfinished network would have made the project impossible to open. Now the notation writes it as `?` (`=> ?`, `/?`, `{TON ?}`), and reads `?` back as empty.

### 2.4 One editor per block
**Code:** `src/app/project_window.hpp`

The project window keeps one `KopEditor` per KOP block, each with **its own undo history** (as most editors with several open documents do). The editor draws into whatever window it's given (it no longer opens its own), so the project window decides where it goes.

The project knows whether it's modified by asking each part: each KOP editor's history, each ST block's text compared with what was saved, and a flag for blocks added, deleted or renamed. Closing the window with unsaved changes asks: save and quit, quit without saving, or cancel.

### 2.5 Finding widgets in the UI tests
**Code:** `src/app/widget_positions.hpp`

The UI tests need to click widgets they can't see. Instead of one special function per widget, the windows remember where some named widgets were drawn (`widgets_.remember("+ variable")` right after drawing the button), and the tests click them by name: `ui.clickWidget("new block")`.

---

## Part 3: C++ concepts

### 3.1 JSON with nlohmann/json
```cpp
json entry = {{"name", variable.name}, {"type", variable.type}, {"section", sectionName(variable.section)}};
if (!variable.comment.empty()) entry["comment"] = variable.comment;
...
const auto data = json::parse(text);                     // throws json::parse_error on bad JSON
for (const auto& entry : data.value("interface", json::array())) { ... }  // a default when the key is missing
```
`json` behaves like a mix of `std::map`, `std::vector` and a variant. Initializer lists build objects, and `value(key, default)` reads optional fields. It keeps object keys sorted, which makes saved files stable (the same project always gives the same bytes).

### 3.2 `std::filesystem`
`fs::path` joins parts with `/` (`folder / "project.json"`), `fs::create_directories`, `fs::rename`, `fs::remove`, `fs::exists`. The functions used here take a `std::error_code&` instead of throwing, so a failed rename becomes a clear message rather than an exception from deep inside.

### 3.3 Heterogeneous lookup: `std::less<>`
```cpp
std::map<std::string, ImVec2, std::less<>> centers_;
centers_.find(name);   // name is a std::string_view: no std::string is built for the lookup
```
With the default `std::less<std::string>`, `find` needs a `std::string`. `std::less<>` ("transparent" comparison) compares any two things that can be compared, so a `string_view` key can be looked up directly.

### 3.4 Stable addresses as IDs
ImGui IDs separate widgets with the same label. Each block's popups get their own ID scope with `ImGui::PushID(pointer)`. The pointer is the block's **editor**, not the block itself: the blocks live in a `std::vector`, which moves them when it grows, but each editor is a separate `unique_ptr` allocation that never moves.

---

## Part 4: Testing

- `project_test.cpp`: new blocks; save and load back equal; old files removed, other files kept; every kind of load error with its file and place; the project rules; the example project round trip.
- `ui/project_ui_test.cpp`: adding a variable, renaming it in its cell, undoing both; creating a block (and the project then complaining about two PROGRAMs); refusing a taken name; renaming and deleting a block; typing into an ST block and saving (Ctrl+S while typing); quitting with unsaved changes.
- The KOP editor's 15 UI tests now run inside the project window, unchanged.

---

## Review questions

**1.** Why is a project a folder of files rather than one file?
<details><summary>Answer</summary>

One file per block makes version control useful (a change touches one file, and diffs are readable), keeps each format natural (JSON for KOP, plain text for ST), and limits damage: a broken file breaks one block, and the error says which.
</details>

**2.** What does writing to `Main.kop.json.tmp` and renaming it achieve?
<details><summary>Answer</summary>

The rename replaces the old file in one atomic step, so the file is always either fully old or fully new. A crash or a full disk while writing only affects the temporary file.
</details>

**3.** Why does saving only remove files listed in the previous `project.json`?
<details><summary>Answer</summary>

Those are the files the editor wrote itself: the old versions of deleted or renamed blocks. Anything else in the folder (notes, scripts) belongs to the user and must not be deleted.
</details>

**4.** The round-trip test found that `=> ` couldn't be read back. What was the real-world consequence, and what's the fix?
<details><summary>Answer</summary>

Saving a network with an operand not filled in yet would have produced a project that can't be opened again. The notation now writes a missing operand as `?` and reads `?` back as empty.
</details>

**5.** Which sections can a FUNCTION have, and why no VAR_OUTPUT?
<details><summary>Answer</summary>

VAR_INPUT and VAR (temporaries), plus a return type. A function gives back exactly one value, its result, assigned to its name; outputs are what function blocks are for.
</details>

**6.** Why does each block's ImGui ID come from its editor's address and not from the block's address?
<details><summary>Answer</summary>

The blocks are in a `std::vector`, which moves its elements when it grows (adding a block), so a block's address can change. Each editor is allocated separately through a `unique_ptr` and never moves, so its address is a stable ID.
</details>
