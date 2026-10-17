# Lesson 8: Online mode

Milestone 8 runs the project and **goes online**, like *Go online* and *Monitoring* in TIA Portal: power flow is drawn green on the rungs, box pins and ST lines show live values, and values can be **modified** or **forced** from the editor.

```
           ladder_editor                                   soft_plc
 ┌───────────────────────────────┐                ┌──────────────────────────┐
 │ KOP editor ─ watched paths ─┐ │   READ start   │ scan cycle (10 ms)       │
 │ ST editor  ─────────────────┤ │   STATUS       │   between cycles:        │
 │                             ▼ │   FORCES       │   MonitorService answers │
 │        online::Session ───────┼──────TCP──────►│                          │
 │        (its own thread)  ◄────┼── OK TRUE ... ─│                          │
 └───────────────────────────────┘                └──────────────────────────┘
      soft_plc runs either inside the editor (Simulate) or on its own (Connect)
```

Try it: `./build/ladder_editor --simulate examples/conveyor`. Select the `stop` contact and press **Set TRUE**, the same for `eStop`, then double-click `start`: the belt runs. Or run soft_plc yourself: **Export ST**, then `soft_plc --monitor=5020 conveyor.st`, and **Connect**.

---

## Part 1: The PLC side

### 1.1 One protocol, two PLCs
soft_plc already answers monitoring requests (its milestone 5): one line of text per request, `READ`, `WRITE`, `FORCE`, `UNFORCE`, `FORCES`, `STATUS`, answered **between scan cycles**, so the program never sees half a change. The editor uses exactly that, whichever PLC it talks to:
- **Simulate** starts `online::Simulator`: soft_plc's own `ScanCycle`, `StProgram`, `MonitorServer` and `MonitorService`, on a thread inside the editor, on a free port. It's PLCSIM: the real runtime, without hardware.
- **Connect** talks to a soft_plc started with `--monitor`, on another terminal or another machine (soft_plc only listens on 127.0.0.1, so the same machine for now).

Because the simulator speaks the same protocol over TCP, the editor has **one code path** for both, and the tests of the simulator test the remote case too.

### 1.2 Inputs: Set or Force
The simulator has no I/O driver that reads inputs, so an input written with **Set** (WRITE) keeps its value, like a button held down. A real PLC reads its inputs at the start of every cycle and would overwrite a WRITE right away: there an input must be **forced** (FORCE holds a value every cycle until UNFORCE). Internal variables can be set either way; the program may overwrite a Set value on its next cycle.

A detail the example shows: `stop` and `eStop` are wired **normally closed**, so they're TRUE while nobody presses them. In the simulator every input starts at 0: both read as *pressed* until you set them TRUE. That's what a real machine does with its safety circuit open.

### 1.3 Downloading
The simulator runs the project as it was when started. When the project changes, the header says **The PLC runs an older version** and offers **Download**, which restarts the simulator with the new code. Its values start over: soft_plc can't swap code while keeping memory (real PLCs can, for some changes, which is a big piece of work).

---

## Part 2: The editor side

### 2.1 Never wait on the network in the UI
A frame takes 16 ms; a network round trip can take longer, or never come back. So `online::Session` has its own thread. About ten times a second it:
1. sends the queued commands (WRITE, FORCE, ...);
2. reads the watched variables (`READ a b c ...`, 64 at a time);
3. reads `STATUS` and `FORCES`;
4. swaps the results in under a mutex.

The UI thread only takes the mutex for a moment: to say what to watch, to read the latest values, or to queue a command. A command wakes the thread at once (a `std::condition_variable` with a timeout), so a click acts without waiting for the next round.

### 2.2 What to watch
Each frame, the open block reports which variables it showed: the KOP editor every contact operand, box output and pin operand it drew; an ST block every name on its lines. The window passes the list to the session only when it changes. Names soft_plc doesn't know (keywords, types, pin names in a call) come back as `?` and show nothing, so the ST side doesn't need to know which names are variables: the PLC says.

### 2.3 From an operand to a path
`online::monitorPath` turns what's written in a rung into what soft_plc calls it:
- `start` is `start`; `t1.Q` is `t1.Q`;
- in a function block, everything is seen through an **instance**: `demand` in Pump, watched through `line.p1`, is `line.p1.demand`;
- `%IX0.0` is the variable the translation generated for it, `_kop_IX0_0`;
- `TRUE`, `30`, `T#5s` are literals: nothing to read.

A function block's instances come from soft_plc's analyzer: after compiling, `checkCode` walks the PROGRAM's variables, and each instance's variables, collecting every path whose type is a project function block. A FUNCTION has no instance and no memory between calls, so there's nothing to watch.

### 2.4 Power flow
`ladder::evaluatePower` works out, from the values, where power flows, the same way the translation runs the rung: series contacts in order, parallel sides ORed, a box giving out its power output (`t1.Q`). The box output is **read, not computed**: a TOF's Q stays TRUE after its power is gone, and only the PLC knows.

Drawing it needs one more idea: wires and links join grid points into **electrical nets**. Power at any point of a net is power on all of it. The view marks the points where power enters (the left rail, each element whose output is powered) and spreads it along wires and links until nothing changes. A contact is drawn green when it's **closed** (it would pass power), even if no power reaches it: that's what the operand's state says, separately from the flow.

The values are read *after* a cycle, so a rung that writes what it reads shows the end of the cycle: `(start | motor) stop => motor` shows `motor` already TRUE on its own branch, which is what holds it.

---

## Part 3: C++ concepts

### 3.1 Pimpl to keep soft_plc out of the headers
`Simulator` holds soft_plc's scan cycle, program and server, but its header includes none of them: it holds a `std::unique_ptr<State>` to a struct defined only in the `.cpp`. Code that uses the simulator doesn't see soft_plc's headers, and doesn't recompile when they change. `ladder_online` links `soft_plc_core` **privately** for the same reason. The destructor must be defined in the `.cpp`, where `State` is complete.

### 3.2 A thread, a mutex and a condition variable
```cpp
std::unique_lock lock(mutex_);
wake_.wait_for(lock, period_, [this] { return stop_ || !commands_.empty(); });
```
`wait_for` releases the mutex while waiting and takes it back before returning. The predicate guards against spurious wake-ups, and returns at once if a command arrived before the wait began. The destructor sets `stop_`, notifies, and joins: the thread is always finished before the members it uses are destroyed (the `std::thread` is the **last** member, so it's started after all the others exist).

### 3.3 Copy under the lock, work outside it
`exchange()` copies the watch list and takes the command queue (`swap`) under the mutex, then does all the network calls **without** it, and takes it again only to store the results. Holding a mutex across a network call would freeze the UI whenever the network is slow.

### 3.4 A flood fill
`PoweredPoints` repeats passes over the wires and links until a pass changes nothing: a fixed point. It's quadratic in theory, but a rung has a handful of wires, so the simplest correct loop wins over a union-find.

---

## Part 4: Testing

- `ladder/power_test.cpp` (5 cases): closed and open contacts, a closed contact without power, parallel sides, a TOF's output without power, unknown values.
- `online/online_test.cpp` (6 cases, real TCP on 127.0.0.1): the simulator running and the session reading; writing an input that holds; forcing and the forces list; a refused command and a runtime STOP reported; a program that doesn't compile; a PLC that goes away; operand paths.
- `st/st_support_test.cpp` (1 new case): names on each line; instances of each function block, nested ones included.
- `ui/online_ui_test.cpp` (5 cases): simulate, watch, double-click to press start, force and unforce stop from the details; Download after a change; no simulation with errors; a function block watched through its instance and ST lines with values; the built-in example compiling and running.
- The UI tests wait for the PLC with `waitFor`, drawing frames until a condition holds or 3 s pass: the PLC runs on its own clock. They were run many times in a row to check they don't depend on timing.
- By eye: the example connected to a standalone soft_plc, with a box stuck at the start sensor, until the jam detector tripped.

Two bugs the work found: the built-in example (the one without a folder) had a box counter with no contact in front and no variables for its third network, so since milestone 7 it showed a soft_plc error; it's now in `app/example.cpp` with a test. And `pkill -f ladder_editor` in a script also killed the shell running it, whose command line contained the name: `pkill -x` matches the process name only.

---

## Review questions

**1.** Why does the session use its own thread instead of reading values inside `draw()`?
<details><summary>Answer</summary>

`draw()` runs every frame on the UI thread. Each request waits for soft_plc's reply, which only comes between its cycles, and the network can be slow or gone. Waiting there would freeze the editor; on its own thread the waiting doesn't matter, and the UI only takes the latest values.
</details>

**2.** In the simulator you Set `start` TRUE and the belt runs. Against a real soft_plc the same click does nothing. Why, and what works?
<details><summary>Answer</summary>

A real PLC reads its inputs into `%I` at the start of every cycle, overwriting the written value before the program sees it. The simulator reads no inputs, so the value stays. Against a real PLC, force the input: a force is applied every cycle after the inputs are read.
</details>

**3.** `a {TOF t1, PT := T#5s} => lamp`: `a` just went FALSE. What's green?
<details><summary>Answer</summary>

The left rail and the wire up to `a`. `a` is open, so the box's input is not powered. But `t1.Q` stays TRUE for 5 s, so the box's output, the wire after it and the coil are green: the box output is read from the PLC, not computed from its input.
</details>

**4.** You open Pump, used as `p1` and `line.p1`. What path is read for its contact `demand`?
<details><summary>Answer</summary>

`p1.demand` with the first instance chosen in the Instance combo, or `line.p1.demand` with the other. Each instance has its own memory, so the editor needs to know which one you mean.
</details>

**5.** Why can't you watch a FUNCTION?
<details><summary>Answer</summary>

A FUNCTION has no instance: its variables are temporaries, created for each call and gone after it. Between cycles, when soft_plc answers requests, there's nothing left to read.
</details>

**6.** After Download, the counter that showed 57 shows 0. Why?
<details><summary>Answer</summary>

Download restarts the simulator with the new program, and a new program starts with fresh memory. Keeping values across a code change would need the runtime to map old variables to new ones, which soft_plc doesn't do.
</details>
