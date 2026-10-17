# Lesson 3: Structured editing

Milestone 3 makes the editor edit. Every change goes through a small set of **edit operations** (`src/ladder/edit.hpp`), each of which leaves a valid rung behind; the UI only decides *which* operation to call.

```
key C / button -| |-  ──►  insertElement(rung, after(selection), Contact{})  ──►  rung changes
                                                                              ──►  next frame draws it
```

Try it: `./build/ladder_editor examples/conveyor.lad`, click an element, then:

| Key | Does |
|---|---|
| C / N | insert a normally open / closed contact after the selection |
| B | insert a box (arrows + Enter, or click, in the palette) |
| P / Shift+P | put a branch around the selection / add a side to its branch |
| O | add a coil |
| Ctrl+← / Ctrl+→ | move the selection left / right |
| Enter, double-click | edit the selection (operand, NO/NC, coil kind, box pins) |
| Del | delete the selection (or the network, if only the network is selected) |
| Ctrl+N | add a network after the selected one |
| Ctrl+S | save |

---

## Part 1: PLC concepts

### 1.1 Editing like TIA Portal
TIA Portal's LAD editor doesn't let you draw wires either: you select a position, insert an element, open a branch around elements and close it again. The same idea here gives the same guarantee: whatever you do, the network stays a valid series/parallel structure that can be translated and run.

### 1.2 "???" means "not finished"
A new element has no operand yet and is drawn with `???` in orange, like TIA's red `<??.?>`. `check()` lists it under the network. The editor goes straight into editing the operand after inserting, so most of the time it's filled in before you notice.

### 1.3 The last coil
A rung must end in at least one coil (it's what the rung is *for*), so the editor refuses to delete the last one and says why. To get rid of a rung, delete the network.

---

## Part 2: Design concepts

### 2.1 Operations that keep the structure valid
**Code:** `src/ladder/edit.cpp`

- `insertElement(rung, point, element)`: an `InsertPoint` is a series (by path) and an index in it: `before(path)` and `after(path)` make one from an element.
- `addParallel(rung, path, other)`: `a` becomes `(a | other)`.
- `addSide(rung, path, element)`: `(a | b)` becomes `(a | b | element)`.
- `removeElement(rung, path)`, followed by **tidying**: a side left empty disappears, and a branch left with one side becomes plain series again. So deleting `c` from `(a | c) d` gives `a d`, never a branch with one side.
- `moveElement` swaps neighbours in a series; `insertCoil`/`removeCoil` keep at least one coil.

Building `(start | motor) stop => motor` from `=> motor` takes three operations: insert `start`, put a branch around it with `motor` below, insert `stop` after the branch. That's one of the unit tests.

### 2.2 Paths move when the tree changes
After `addParallel`, the element that was at `{0}` is at `{0, 0, 0}` (inside the new branch, on side 0). After an insertion, everything behind the new element shifts by one. So every operation **returns** the path to select afterwards, and the editor never assumes an old path still points to the same thing.

### 2.3 Testing a GUI without a screen
**Code:** `tests/ui/editor_ui_test.cpp`

The editor window is drawn with ImGui, and ImGui doesn't care where its input comes from: the backend normally passes mouse and keyboard events from the window, but a test can call `io.AddMousePosEvent`, `io.AddMouseButtonEvent`, `io.AddKeyEvent` and `io.AddInputCharactersUTF8` itself, then run `NewFrame()`, `editor.draw()`, `Render()`. Nothing is shown on screen, but every widget behaves exactly as with a window. The UI tests click elements, press shortcuts, type operands, save, and check the rungs and the file.

It needed a separate library, `ladder_ui`, that uses only ImGui's core (no GLFW, no OpenGL), and two small hooks: `EditorWindow::rungOrigin(i)` (where network `i` was drawn) and `view::itemCenter(...)` (where an item's centre is in pixels).

Writing the harness taught three things about how a real ImGui backend behaves:
- **Modifiers are separate events.** `io.KeyCtrl` comes from `ImGuiMod_Ctrl`, not from `ImGuiKey_LeftCtrl`; a backend sends both.
- **Time matters.** Two clicks less than 0.3 s apart at the same place are a double-click. The harness pauses half a second before each single click.
- **Focus selects the text.** When a text field gets keyboard focus, its content is selected, so typing replaces it. That's what you want when editing an operand.

It also found a real gap: the box palette couldn't be used from the keyboard. Now `B`, `↓`, `Enter` works.

---

## Part 3: C++ and ImGui concepts

### 3.1 Moving parts of a tree: `std::move` and move iterators
`addParallel` moves the element into the new branch (`push_back(std::move(*element))`), then moves the branch into the element's old place. `tidy` lifts a side's elements into the parent series with `std::make_move_iterator`, so `insert` moves them instead of copying. Nothing is copied when the tree is rearranged.

### 3.2 `std::remove_if` + `erase`
```cpp
sides.erase(std::remove_if(sides.begin(), sides.end(), [](const Series& s) { return s.elements.empty(); }), sides.end());
```
`remove_if` moves the elements to keep to the front and returns where the rest begins; `erase` then cuts the rest off. It's the standard idiom (C++20 adds `std::erase_if` to do both in one call).

### 3.3 `tl::expected` with an optional inside
`removeElement` returns `tl::expected<std::optional<ElementPath>, std::string>`: either an error ("there's no element there"), or success with *maybe* something to select next. The two layers say different things: failure vs. success, and "nothing left to select" is a normal success.

### 3.4 Editing in place with `imgui_stdlib`
`ImGui::InputText("Instance", &box->instance)` edits the `std::string` inside the rung directly (`misc/cpp/imgui_stdlib.h` adds the `std::string` overloads). There's no copy of the rung for the popup: the drawing a frame later shows the new operand.

### 3.5 Don't change a list while drawing it
Deleting or moving a network from a button inside the loop that draws the networks would invalidate the loop. The loop only records the request (`deleteRequest`, `moveRequest`) and the change happens after it.

---

## Part 4: Testing

- `edit_test.cpp` (10 cases): building the starter step by step; inserting inside branches; deleting with the right neighbour selected; sides disappearing and branches turning back into series, also nested; adding sides; moving; coils; series paths.
- `ui/editor_ui_test.cpp` (8 cases): every shortcut and the box palette, double-click editing, networks, refusing to delete the last coil, and saving.

---

## Review questions

**1.** Starting from `=> lamp`, which edit operations build `a (b | c) => lamp`?
<details><summary>Answer</summary>

Insert `a` (the logic is empty, so at index 0), insert `b` after `a`, then put a branch around `b` with `c` on the new side: `addParallel(rung, {1}, c)`.
</details>

**2.** `(a | b) c => x`: you delete `b`. What's left, and why?
<details><summary>Answer</summary>

`a c => x`. Deleting `b` leaves its side empty, so the side is removed; the branch then has one side, so its elements (`a`) take the branch's place.
</details>

**3.** Why does every edit operation return a path instead of letting the editor keep its old selection?
<details><summary>Answer</summary>

Because paths change with the tree: an insertion shifts the elements after it, and putting a branch around an element moves it one level down. The operation knows where things ended up; the old path might now point to a different element, or to nothing.
</details>

**4.** How can a test click a button in an ImGui window that is never shown on screen?
<details><summary>Answer</summary>

ImGui gets its input from events (`io.AddMousePosEvent`, `io.AddMouseButtonEvent`, `io.AddKeyEvent`). Normally the backend sends them from the real window, but a test can send them itself and then run the frames. The widgets react exactly as if a person had clicked.
</details>

**5.** The harness first sent only `ImGuiKey_LeftCtrl`, and Ctrl+S didn't save. Why?
<details><summary>Answer</summary>

`io.KeyCtrl` is set from the modifier event `ImGuiMod_Ctrl`, which real backends send alongside the key event. Without it, ImGui saw the S key but not Ctrl held.
</details>

**6.** Why does the loop that draws the networks only *record* a request to delete one, and delete it after the loop?
<details><summary>Answer</summary>

Erasing from `rungs_` while iterating over it would shift the elements under the loop (and invalidate references to them, like the `rung` being drawn). Recording the request and applying it afterwards keeps the loop safe.
</details>
