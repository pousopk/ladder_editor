# Lesson 11: Online change

Milestone 11 makes **Download** an **online change**: the new version of the program goes into the running PLC between two cycles, and the PLC keeps its memory. In TIA Portal it's a download in RUN. It's what makes a change on a running machine possible: the belt keeps running, the counters keep their counts, a box halfway along keeps its timer.

```
cycle n          between cycles                         cycle n+1
old program ──►  LOAD <new source>                 ──►  new program
                 compile; new memory;
                 for each variable of the new program:
                   same name, same type in the old one?  ──yes──►  copy its value
                                                         ──no───►  initial value
                 standard blocks: move their hidden state
                 AT variables: in the process image, untouched
```

Try it: `./build/ladder_editor --simulate examples/conveyor`, **Set all** in the watch table, set `boxAtStart` TRUE, and let `travelTime` run. Then change the TON's PT from `T#8s` to `T#20s` and press **Download**: the timer goes on from where it was, with the new limit. Press **Restart** to compare: every value starts over.

---

## Part 1: What carries over

### 1.1 Matching by name and type
The new program's memory is built fresh, then filled from the old one, variable by variable:
- **same name** (ignoring case, as ST does) **and same type**: the value is copied;
- **new**, or the **type changed** (INT to REAL, a bigger array, a struct with another field): it starts at its initial value. Copying an INT's bits into a REAL would make nonsense, so it's safer to start over, and the reply says which ones did;
- **gone**: its value is dropped, and so is a force on it.

"Same type" means same **layout**: the same elementary type, arrays with the same bounds, structs with the same fields in the same order. Two programs are compiled separately, so their types are different objects in memory: they're compared by shape, not by address.

### 1.2 Function block instances
An instance is matched by name and **block type** (`p : Pump` stays `p : Pump`), then its own variables are matched the same way, all the way down. A changed Pump (a new variable inside, new code) keeps everything it still has.

### 1.3 The hidden state of standard blocks
TON, CTU, R_TRIG and the others are written in C++, and part of their memory isn't visible as variables: a TON's **start time**, a CTU's own count and the last value of its input. Copying `ET` and `CV` wouldn't be enough: the TON would start timing again, and the CTU would write its own count, 0, over `CV` on the next call. So the C++ object itself is **moved** from the old instance to the new one (`std::unique_ptr`, so it's a pointer handover, not a copy).

### 1.4 What doesn't need copying
`AT` variables (`start AT %IX0.0`) have no memory in the program: they're bits in the **process image**, which belongs to the PLC, not to the program. They keep their values without anything being done. The same goes for data blocks; a data block can be added by an online change, but not made smaller or bigger, because S7 clients see its bytes.

### 1.5 STOP, and fixes
If the program was in STOP after a runtime error, a successful online change puts it back in RUN: the change may well be the fix. But a variable keeps its value, including the one that caused the error: if `b` was 0 and caused a division by zero, it's still 0 after the change. The fix has to handle it (`1 / (b + 1)`, or a check), exactly as on a real PLC.

---

## Part 2: How it travels

### 2.1 One more command
soft_plc's monitor protocol is one line per request, so the program travels on one line: `LOAD PROGRAM Main\nVAR ...`, with `\n` for each line end and `\\` for a backslash. soft_plc compiles it and, only if that works, swaps programs in its idle time between cycles, where every monitor request is answered. The program never sees half of each version. A program that doesn't compile is refused (`ERR line 2, column 5: ...`) and the old one goes on running.

The reply says what happened: `OK kept=46 initialized=1 shiftCount`.

### 2.2 In the editor
**Download** sends the project's ST (the same text soft_plc compiled for the error check) with `Session::load`, and remembers the project it sent. The answer comes a moment later on the session thread; the window picks it up the next frame:
- OK: that project becomes "what the PLC runs", and the header lists what started over;
- refused: the header says why, and the PLC still runs the old version.

After a change, the session lists the PLC's variables again, so the watch table knows about the new ones.

Because it's part of the protocol, Download works with a standalone soft_plc too. Before, only the simulator could take a new version, by restarting.

---

## Part 3: C++ concepts

### 3.1 Two trees walked side by side
`takeOverInstance(now, before)` walks the new program's instance tree, and for each variable looks up its namesake in the old one: a recursive walk over two trees that may differ in shape. Driving it from the **new** tree means every new variable is visited (to count it as initialized), and gone ones are simply never looked at.

### 3.2 Moving a `unique_ptr` out of an object you're about to throw away
```cpp
now.native = std::move(before.native);
```
After the move, `before.native` is empty: the old interpreter is left without its blocks' state, which is fine because it's destroyed right after (`interpreter_ = std::move(next)`). The interface says so (`old` is left without its state: throw it away), because a moved-from object that looks normal is a classic trap.

### 3.3 Build the new one, then swap
`StProgram::replace` builds the whole new interpreter, fills it, and only then assigns it. If anything went wrong before the assignment, the old program would still be intact. It's the same idea as writing project files to a `.tmp` and renaming them (lesson 5).

---

## Part 4: Testing

- soft_plc, `tests/st/online_change_test.cpp` (2 cases, on both engines): a counter, a running TON, a CTU, a user function block with an R_TRIG, an AT output, a changed type, a removed and a new variable: what's kept and what starts over, and the timer reaching its preset at the same moment as without the change; forces on removed variables dropped; a program in STOP running again.
- soft_plc, `tests/monitor/monitor_test.cpp` (2 cases): LOAD's reply, and a program that doesn't compile being refused while the old one runs on.
- `online/online_test.cpp` (1 new case): a change through the session, values kept, the new variable listed, a refused change.
- `ui/online_ui_test.cpp` (2 cases): Download keeps a counter counting and names the new variable, Restart starts it over; Download refuses a project with errors.
- By hand: the conveyor example on a standalone soft_plc, its jam timer at 2 s, changed to `T#20s` with LOAD: the timer went on to 2.5 s, the PLC stayed in RUN.

---

## Review questions

**1.** A box is at the start sensor, `travelTime` shows `T#5s`, and you change PT from `T#8s` to `T#20s` and Download. When does `jammed` trip?
<details><summary>Answer</summary>

20 s after the box started, so 15 s after the download. The TON's start time moved across with it, so it doesn't start timing again; only its preset changed.
</details>

**2.** You change `level : INT` to `level : REAL` and Download. What's `level` afterwards, and why not the old value converted?
<details><summary>Answer</summary>

Its initial value (0.0), and the reply lists it. The type changed, so its memory has another layout; copying would mean guessing a conversion for every case (INT to REAL is easy, a struct with a new field isn't). Starting over is predictable, and the editor tells you it happened.
</details>

**3.** Why does the TON's C++ object have to move, when `ET` and `Q` are copied like any variable?
<details><summary>Answer</summary>

`ET` and `Q` are outputs the block computes from its hidden state: the start time. A new TON object would have no start time, so on its next call it would start timing again and overwrite `ET` with 0. The hidden state is what has to survive.
</details>

**4.** The PLC stopped on a division by zero (`a := 1 / b;`, with `b` at 0). You change the line to `a := 1 / b;` with `b : INT := 1;` in the declaration and Download. Does it run?
<details><summary>Answer</summary>

No: it goes back to RUN, and stops again on the first cycle. `b` exists in both versions with the same type, so it keeps its value, 0; the new initial value only applies to a variable that starts over. The fix has to handle the value it has, or `b` has to be set (from the watch table, for example).
</details>
