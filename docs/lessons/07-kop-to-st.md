# Lesson 7: Translating KOP to ST

Milestone 7 makes the KOP blocks real code: each one is **translated to Structured Text**, put together with the ST blocks, and **compiled by soft_plc**. The ST is shown next to the ladder, soft_plc's errors come back to the network they belong to, and **Export ST** writes the whole project as one file soft_plc can run.

```
KOP block ──► ladder::translateBlock ──► ST text ─┐
ST blocks ───────────────────────────────────────┴─► one ST program ──► soft_plc compileProgram
                                                                              │
          network 3 ◄── TranslatedBlock::networkAt ◄── blockAt ◄── error line ┘
```

Try it: `./build/ladder_editor examples/conveyor`, tick **ST** in the toolbar, and compare each network with its code. Then change a coil's operand to a name that isn't declared, and watch soft_plc's message appear under that network.

---

## Part 1: Following the power

### 1.1 The rules
A rung is power flowing from the left rail to the coils. In ST, the power at each point is a BOOL expression:

| Ladder | ST |
|---|---|
| contacts in series `a b` | `a AND b` |
| a parallel branch `(a \| b)` | `a OR b` |
| a normally closed contact `/a` | `NOT a` |
| a coil `=> x` | `x := power;` |
| a negated coil `=> /x` | `x := NOT power;` |
| set / reset `=> S:x`, `=> R:x` | `IF power THEN x := TRUE; END_IF;` (`FALSE` for reset) |

```
(start | running) stop eStop => running      running := (start OR running) AND stop AND eStop;
```

A set coil isn't `x := x OR power`: that would be the same in this case, but the `IF` reads like what the coil does (nothing at all without power), and it's what TIA Portal shows for S and R in SCL.

### 1.2 An expression tree, printed with few parentheses
The translator builds a small tree (`Expr`: TRUE, a name, NOT, AND, OR) and prints it at the end. Two tidy-ups happen while building:
- **flattening**: `a AND (b AND c)` becomes one AND of three items;
- **dropping TRUE**: the power at the left rail is `TRUE`, and `TRUE AND a` is just `a`. An empty rung gives `x := TRUE;`.

Printing adds parentheses only where the precedence needs them (NOT binds tighter than AND, AND tighter than OR), so `a AND (b OR c)` keeps them and `a AND b OR c` doesn't need any.

### 1.3 Boxes: statements, not expressions
A box is a function block call. The power goes into its power input, and comes out of its power output:
```
a {TON t1, PT := T#5s, ET => elapsed} => lamp      t1(IN := a, PT := T#5s);
                                                   elapsed := t1.ET;
                                                   lamp := t1.Q;
```
The call has to happen **every cycle**, with or without power: a TON must see `IN` go FALSE to reset. So the call is a statement of its own, and what comes after the box sees `t1.Q`.

### 1.4 When a value is needed twice
Some power values are used more than once:
- several coils on one rung;
- a parallel branch with a box on one side: the box needs the power that *enters* the branch, and so does the other side.

Writing the expression twice would compute it twice, and worse: a box call in between could change what it reads. So the translator keeps it in a generated variable first:
```
a b ({TON t1, PT := T#1s} | c) => x         _kop_power1 := a AND b;
                                            t1(IN := _kop_power1, PT := T#1s);
                                            x := t1.Q OR _kop_power1 AND c;
```
A plain name (`a`) is never kept: reading it twice is free.

### 1.5 Names the translation keeps for itself
Generated names start with `_kop_`, and the project rules (`checkProject`) refuse user variables that start with it, so they can never clash. Two kinds exist:
- `_kop_power1`, `_kop_power2`, ... for kept power values;
- `_kop_QX0_0 AT %QX0.0 : BOOL` for an **address used as an operand**. soft_plc only allows addresses in `AT` declarations, so a contact on `%IX0.0` reads a generated variable at that address. Only the PROGRAM can do this: a function block has no `AT`, so an address there is a translation problem ("pass the value in through an input").

### 1.6 Networks that can't be translated
A network with an empty operand, an unknown box or an address in a function block is **left out**, with a comment saying why, and reported as a `TranslationProblem`. The rest of the block is still translated and compiled, so one unfinished network doesn't hide errors in the others.

---

## Part 2: The whole project, checked by soft_plc

### 2.1 One program, many parts
`translateProject` writes every block one after the other: ST blocks as they are, KOP blocks translated. It remembers where each block starts (`Part::firstLine`), and inside each translated block, where each network starts (`LineOrigin`). soft_plc compiles the result exactly as it will run it.

### 2.2 Tracing an error back
soft_plc reports `line 57: unknown variable 'missing'`. Two lookups, both `std::upper_bound` over sorted start lines, bring it back:
1. `blockAt(57)`: the block whose part starts at or before line 57, and the line inside it;
2. `networkAt(line)`: for a KOP block, the network whose code holds that line, or nothing (the interface).

The KOP editor shows the message under that network; the project's problem list says `Main, network 8: ...`, and clicking it opens the block.

### 2.3 Rechecking when anything changes
Before, only ST edits triggered a check. Now any change matters (a KOP edit, an interface cell, a rename), so the window keeps a **copy of the project as it was last checked**, and checks again whenever `project_ != *checked_`. Comparing the whole project each frame sounds expensive, but it's a few kilobytes compared with `operator==` (defaulted on every struct), far cheaper than drawing the frame.

### 2.4 The project's own function blocks as boxes
The box palette now lists the project's function blocks next to TON and CTU:
- a KOP function block: from its interface;
- an ST function block: from its text, parsed with soft_plc's `parseFunctionBlocks`.

The **first BOOL input** takes the rung's power and the **first BOOL output** gives it back; the other inputs and outputs become pins. A block with no BOOL output passes the power through unchanged (like ENO in TIA Portal); one with no BOOL input can't be a box. The example's `BoxRate` (ST) is now called from network 8 of the KOP program.

This is why `check()`, `layOut()` and `drawRung()` take a `BoxCatalog`: the box types are no longer a fixed list.

---

## Part 3: C++ concepts

### 3.1 `std::shared_ptr<const Expr>` for shared subtrees
The power entering a branch is used by each of its sides, so the same subtree can appear in several places of the tree. `shared_ptr<const Expr>` shares it without copying, and `const` makes sharing safe: nobody can change a subtree another part is using.

### 3.2 `std::visit` with a generic lambda calling overloads
```cpp
power = std::visit([&](const auto& e) { return this->element(e, power); }, element);
```
`element` is overloaded for `Contact`, `Box` and `Parallel`. The generic lambda is instantiated once per alternative, and overload resolution picks the right `element`. Inside a lambda, `this->` makes it clear that `element` is the member function, not the lambda's own parameter called `element` outside.

### 3.3 Exceptions inside a class, values outside
While translating a network, a problem can be found deep in the recursion (an address in a function block, three levels into branches). The translator **throws** a private `TranslationError` and catches it at the network level, where it becomes a `TranslationProblem` in the result. Nothing outside the file sees an exception: the public API returns values, like the rest of the editor. Throwing is the simplest way out of a recursion; catching it close by keeps it from being a hidden control path.

### 3.4 A bug the translation found
Reading the generated ST for the example showed `arrived(CLK := TRUE);`: the R_TRIG counting boxes had no contact in front of it, so it saw constant power and counted one box, ever. The rung had been wrong since milestone 5, and the ladder picture didn't make it obvious; the text did. It's now `boxAtEnd {R_TRIG arrived} ...`.

A second one: `check()` compared a pin's name in upper case with the box type's names as written. With TON's `PT` that worked; with `BoxRate`'s `perMinute` it never matched. Project boxes, with mixed-case names, made it visible.

---

## Part 4: Testing

- `ladder/translate_test.cpp` (9 cases): the exact text for series, parallel, NC, every coil kind, an empty rung, several coils, boxes, boxes on a branch, addresses; a whole block with its interface, comments (`*)` inside one made safe) and line origins; networks left out with their reasons; a FUNCTION and the project's line mapping.
- `st/translate_run_test.cpp` (8 cases): the translated rungs **compiled and run by soft_plc**: start/stop with stop winning, set/reset latching, a TON's delay and elapsed time, a CTU counting edges, a TON on a branch, direct I/O addresses through the process image, a KOP function block used as a box, and the example project running its start/stop.
- `st/st_support_test.cpp` (2 new cases): the project's boxes from KOP and ST blocks; errors traced to a KOP network, to the interface, and to an ST block.
- `ladder/check_test.cpp`: project boxes with pins in any case.
- `ui/project_ui_test.cpp` (2 new cases): the ST panel's text, the error under its network, and fixing it; Export ST writing the same text the editor compiled.
- By eye: the ST panel next to the example's networks.

---

## Review questions

**1.** What does `(a | b) /c => x` become in ST?
<details><summary>Answer</summary>

`x := (a OR b) AND NOT c;`. The parentheses stay: AND binds tighter than OR, so without them it would mean `a OR (b AND NOT c)`.
</details>

**2.** Why is a box's call a separate statement, even when the rung has no power?
<details><summary>Answer</summary>

A function block must run every cycle to see its inputs change: a TON resets when it sees `IN` go FALSE, and a CTU only counts when it sees the rising edge. If it were only called with power, it would never see the power go away.
</details>

**3.** `a b => x, y` gives `_kop_power1 := a AND b;` then two assignments. Why not just `x := a AND b; y := a AND b;`?
<details><summary>Answer</summary>

Both coils get the same power, computed once. Computing it twice costs twice, and the first coil can change what the expression reads: in `a b => a, y`, the line `a := a AND b;` changes `a` before `y` is computed, so the two coils could disagree.
</details>

**4.** A contact on `%IX0.0` works in the PROGRAM but is a problem in a function block. Why?
<details><summary>Answer</summary>

soft_plc reads addresses only through `AT` declarations, so the translator declares `_kop_IX0_0 AT %IX0.0 : BOOL`, and only a PROGRAM can have `AT`. A function block has instances: it shouldn't be tied to one physical input. The value comes in through a VAR_INPUT instead.
</details>

**5.** soft_plc reports an error at line 40 of the whole project. Main (KOP) starts at line 0; its network 3 starts at line 31 of its translation and network 4 at line 35. Where is the error shown?
<details><summary>Answer</summary>

Under network 4 of Main: `blockAt(40)` gives Main, line 40; `networkAt(40)` finds the last network starting at or before line 40, which is network 4 (line 35).
</details>

**6.** BoxRate has an INT output `perMinute` and a BOOL output `tooFast`. Which one carries the power, and what would happen without `tooFast`?
<details><summary>Answer</summary>

`tooFast`, the first BOOL output. Without it, BoxRate would pass the power through unchanged: what comes after the box would get the same power that went into `arrived`.
</details>
