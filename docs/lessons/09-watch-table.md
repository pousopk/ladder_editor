# Lesson 9: The watch table

Milestone 9 adds a **watch table**, like the watch and force tables of TIA Portal: a list of variables chosen by hand, from any block, with their live values, and a value ready to write to each one.

```
Variable     Type   Value        Modify value   Comment
stop         BOOL   TRUE    F    TRUE           wired normally closed
start        BOOL   TRUE    F    TRUE           press start
travelTime   TIME   T#3s910ms                   jammed after 8 s
boxes        INT    0
                    ▲    ▲       ▲
                    │    │       └── Set: WRITE once · Force: FORCE every cycle
                    │    └── forced (orange)
                    └── read by the session, like any watched value
```

Try it: `./build/ladder_editor --simulate examples/conveyor`, open **Watch table**, press **Set all**: stop and eStop are released, start is pressed, the belt runs. Then set `boxAtStart` and watch `travelTime` climb to 8 s and `jammed` trip.

---

## Part 1: Why a table, when the blocks already show values?

The blocks show values **where they're used**, and only for the open block. Commissioning needs something else:
- variables from **several blocks at once** (a pump's inputs and the program's outputs);
- values **prepared in advance**, to write together: release the safety circuit, then press start (**Set all** sends them in order, and soft_plc answers them in order, between cycles);
- a place to keep that test setup with the project. The table is saved in `project.json`, under `"watch"`, only when it has lines, so projects without one are unchanged.

### Set or Force, again
**Set** (WRITE) changes the value once; the next cycle, the program (or, on a real PLC, the input scan) may overwrite it. **Force** makes soft_plc write the value every cycle, before and after the program, until **Unforce**. A forced value shows in orange with an `F`, and **Unforce all** removes every force in the PLC, including ones made from the KOP editor: forgetting a force on a real machine is a classic commissioning mistake, so it should be easy to see and to clear.

---

## Part 2: How it works

### 2.1 The same session
The table doesn't need anything new to read values: it reports the paths it showed, like a block does, and the window gives them to the session. An address works too: `%QX0.0` reads the variable the translation generated for it (when a rung uses that address).

### 2.2 Knowing the PLC's variables
Once, when the session starts, it sends `LIST`, and keeps soft_plc's answer: every variable's path, spelled as declared, and its type. The table uses it to:
- show the **Type** column;
- mark a name soft_plc doesn't have in red, with a tooltip (a typo shows at once, instead of an empty value forever);
- send commands with the declared spelling (`START` in the table becomes `start`).

The list is taken once because a running program's variables don't change. After a **Download**, the session is new, and lists again.

### 2.3 Not undoable, but saved
Table edits don't go through a block's undo history: they're a test setup, not program code. The window compares the table with the one last saved to know whether the project is modified (the `*` next to "Watch table").

---

## Part 3: C++ concepts

### 3.1 `std::optional<std::map<...>>`
`variables_` is `std::optional<std::map<std::string, Variable>>`. *Not listed yet* and *listed, and empty* are different states: the first means "don't mark anything red yet", the second "the PLC really has no variables". An optional around the container says that without an extra flag.

### 3.2 A member only one thread touches
`listed_` is read and written only by the session thread, so it needs no mutex; `variables_`, read by the UI thread, is written under the mutex. Saying in a comment which thread owns a member is the cheapest way to keep a threaded class correct.

### 3.3 `std::less<>` for lookups by `string_view`
`WidgetPositions` (used by the tests to find "set 0", "force 0") keeps a `std::map<std::string, ImVec2, std::less<>>`. The transparent comparator lets `find` take a `std::string_view` without building a `std::string` first.

---

## Part 4: Testing

- `ladder/project_test.cpp`: the watch table saved and loaded with the project, without empty fields.
- `online/online_test.cpp` (1 new case): the session's variable list, with declared spellings and types.
- `ui/online_ui_test.cpp` (2 new cases): adding, editing and removing lines offline (and the project becoming modified); online, Set all making the belt run, then forcing stop FALSE from the modify field and unforcing.
- By eye: the example's table connected to a standalone soft_plc with four inputs forced.

---

## Review questions

**1.** You forced `stop` from the KOP editor, then open the watch table, which doesn't list `stop`. Why does **Unforce all** still remove that force?
<details><summary>Answer</summary>

Forces live in the PLC, not in the table or the editor: soft_plc keeps the list, and the session reads it with `FORCES`. Unforce all removes every force soft_plc reports, wherever it was made, because a force someone forgot is exactly the one you need to find.
</details>

**2.** You Set `motor` TRUE in the table, and it shows FALSE again a moment later. Why, and what would keep it TRUE?
<details><summary>Answer</summary>

The program writes `motor` every cycle (`motor := (start OR motor) AND stop`), so the next cycle overwrites the written value. Forcing it would keep it TRUE: soft_plc writes a forced value every cycle, after the program too.
</details>

**3.** You type `Strat` instead of `start`. What does the table show, and how does it know?
<details><summary>Answer</summary>

The name in red, with the tooltip "soft_plc has no variable called Strat". The session listed soft_plc's variables when it started (`LIST`), and `Strat` isn't among them.
</details>
