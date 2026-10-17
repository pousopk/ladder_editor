# Lesson 2: Drawing rungs

Milestone 2 puts rungs on the screen. It's split in two on purpose:

```
Rung ──► layOut() ──► RungLayout ──► drawRung() ──► pixels (Dear ImGui)
          pure logic,      a grid of       drawing only,
          unit-tested      items, wires    checked by looking
                           and links
```

Try it:
```bash
./build/ladder_editor                         # the built-in example
./build/ladder_editor examples/conveyor.lad   # a conveyor with a jam detector
```

---

## Part 1: PLC concepts

### 1.1 How a ladder network is drawn
The editor draws rungs the way TIA Portal and most IEC editors do:
- the **left rail** is the power supply, the **right rail** closes the circuit;
- power flows left to right along the **top line of each row**;
- a **parallel branch** splits with a vertical link and joins again with another one; a shorter side gets a wire to the join;
- **coils line up on the right**, one per row, all fed by the same power;
- a **box** shows every pin of its block, with `...` for pins that aren't connected, so you can see what's left to wire.

### 1.2 Networks and comments
Every rung is a *network* with a number and a comment, as in TIA Portal. The comment says **why** the rung exists ("stop always wins"), which the rung itself can't show. In the rung file, the `//` lines right above a rung are its comment.

---

## Part 2: Design concepts

### 2.1 Layout as data, computed from the tree
**Code:** `src/ladder/layout.hpp`

Positions aren't stored in the rung; they're **computed** from its structure every time it changes:
- a series puts its elements **side by side**: widths add up, the height is the tallest element's;
- a parallel branch puts its sides **one below the other**: the width is the widest side's, the heights add up.

Because a branch must know how wide its widest side is before it can draw a wire to its end, layout is done in two passes: **measure** (sizes only), then **place** (positions). The result, `RungLayout`, is plain data: items (contacts, boxes, coils with their grid cells), wires and links.

Columns are numbered at element **boundaries**: an element at column 3 with width 1 goes from boundary 3 to boundary 4. Then "this element ends where the next one begins" is the same number, and wires and links are just pairs of boundary numbers.

### 2.2 Testable without a screen
The layout has no idea about pixels or ImGui, so its tests can say exactly where everything goes: "motor is at column 0, row 1; links at column 0 and column 1, from row 0 to row 1". The drawing code is thin: it multiplies grid positions by sizes in pixels and draws lines. That part is checked by **looking at it**. While writing this milestone, screenshots caught two problems the unit tests couldn't: a box too narrow for its pin names, and a file header comment glued onto the first network.

### 2.3 Paths: naming an element inside a tree
**Code:** `ElementPath`, `elementAt()`

To select an element, the editor needs a name for it. Its position in the tree works: "element 0 of the rung, side 1 of that branch, element 0 of that side" is `{0, 1, 0}`. `elementAt(rung, path)` walks the tree back to the element. Coils have their own, simpler `CoilPath`. A path stays valid as long as the rung's structure doesn't change; milestone 3 (editing) will have to keep the selection pointing at the right element when it does.

---

## Part 3: C++ and ImGui concepts

### 3.1 Immediate-mode GUI
**Code:** `view::drawRung`, `app::EditorWindow::draw`

Dear ImGui has no widget objects that live between frames. **Every frame**, about 60 times a second, the program draws everything again from its own data, and asks ImGui questions as it goes: "is the mouse over this rectangle?", "was it clicked?". The data (the rungs, the selection, the zoom) lives in our classes; ImGui only remembers small things like which item is active.

That fits a ladder editor very well: there's nothing to keep in sync. Change a rung, recompute its layout, and the next frame shows it.

### 3.2 Drawing with `ImDrawList`
A whole rung is one `ImGui::InvisibleButton` (it reserves the space and tells us when the mouse is over it), and the symbols are drawn with the window's draw list: `AddLine`, `AddRect`, `AddText`, and `PathArcTo` + `PathStroke` for the `( )` arcs of a coil. `PushClipRect`/`PopClipRect` keep a box's input texts inside its left half and its output texts inside its right half.

`ImGui::SetWindowFontScale(zoom)` scales the text with the zoom, so labels stay above their symbols.

### 3.3 Backtracking with one vector
**Code:** `Placer::series`, `Placer::element`

While placing, the code keeps the path to the current element in **one** vector: `push_back` the index before going into an element, `pop_back` after. At any moment the vector *is* the current path, and copying it into a `PlacedItem` records it. This is the usual way to track "where am I" in a recursive walk, without building a new vector at every level.

### 3.4 `std::variant` for "one of two kinds of path"
```cpp
using ItemPath = std::variant<ElementPath, CoilPath>;
```
The selection is "an element or a coil". `ItemPath` says exactly that, and since both have `operator==`, the variant can be compared too (`*selected == item.path`). The details panel uses `std::get_if<CoilPath>` to know which one it got.

### 3.5 A core library without the GUI
`ladder_core` (model, layout, notation, checks, rung files) doesn't link ImGui or OpenGL. The tests use only `ladder_core`, so they build and run on machines without a graphics stack, and the GUI is an optional target (`LADDER_EDITOR_BUILD_APP`).

---

## Part 4: Testing

- `layout_test.cpp`: the exact grid position of every item for single contacts, parallel branches, short sides, branches inside branches, boxes, several coils, long and empty rungs; and that every placed element can be found again from its path.
- `rung_file_test.cpp`: comments, empty lines, the file header that belongs to no rung, writing back, and errors with their line.
- By eye: the built-in example, `examples/conveyor.lad`, and a file with problems (shown in red under the network). Clicking to select couldn't be automated here: synthetic clicks don't reach the window on this desktop. The hover highlight, which uses the same hit test, was visible in the screenshots.

---

## Review questions

**1.** A parallel branch has two sides: `a` (1 column wide) and `b c` (2 columns). How wide is the branch, and what does the layout add to the first side?
<details><summary>Answer</summary>

2 columns, the width of its widest side. The first side ends at column 1 of the branch, so the layout adds a wire from there to the branch's right end, where the sides join.
</details>

**2.** Why does the layout measure everything first and place it afterwards?
<details><summary>Answer</summary>

To place a parallel branch, it must know its widest side before placing any side: the short sides need a wire up to that width, and the join link goes there. Measuring first gives all the sizes; placing then only computes positions.
</details>

**3.** Why are columns numbered at element boundaries instead of at element centres?
<details><summary>Answer</summary>

Then the end of one element and the start of the next are the same number, and wires and links are simply "from boundary a to boundary b". The drawing code turns a boundary into an x position with one multiplication.
</details>

**4.** In `(start | motor) stop => motor`, what's the path of the `motor` contact, and what does each number mean?
<details><summary>Answer</summary>

`{0, 1, 0}`: element 0 of the rung (the parallel branch), side 1 of it (the second side), element 0 of that side.
</details>

**5.** In an immediate-mode GUI, where is the selected element remembered, and when is the screen updated after it changes?
<details><summary>Answer</summary>

In our own data (`EditorWindow::selection_`), not in a widget. The next frame draws everything again from that data, so the change shows up immediately, without any "update the widget" code.
</details>

**6.** The unit tests passed, but the screenshot showed `resetCountcount` in the CTU box. Why couldn't the tests catch that?
<details><summary>Answer</summary>

The tests check the layout (grid cells), which was right. The overlap came from how many pixels the texts take inside the box, which only the drawing code and the font decide. That's why the drawing is also checked by looking at it.
</details>
