# 3. Drawing everything, every frame

The editor's UI is built with **Dear ImGui**, an *immediate-mode* library. That choice shapes the whole `src/app/` and `src/view/` code, so it's worth understanding what it means and why it fits this editor.

---

## Two ways to build a UI

**Retained mode** (Qt, GTK, the web's DOM): you create widget objects once (a button, a text field, a tree view), they live as long as the window, and you keep them in sync with your data: when a rung changes, find the widgets that show it and update them; when a widget changes, update the rung. Signals, callbacks, and a lot of code whose only job is keeping two copies of the same information equal.

**Immediate mode** (ImGui): there are no widget objects. Every frame, 60 times a second, your code walks its data and *says* what to show:

```cpp
if (ImGui::Button("Undo")) {   // draws the button, and returns true if it was clicked this frame
    undo();
}
```

The UI is a **function of the state**, re-run every frame. There's nothing to keep in sync, because there's only one copy of the information: the model.

### Why it fits this editor

- **The model is values** (note 1). Retained mode would need a widget per element and code to rebuild them after every edit, undo or load. Here, an undo replaces a rung, and the next frame simply draws the new one.
- **Derived things are just computed.** The layout of each rung is recomputed every frame from the tree (`layOut(rung, boxes())` in `KopEditor::drawNetworks`, with the comment "cheap: recomputed every frame, so it's never stale"). The rung's problems are checked every frame. Online, the power flow is evaluated every frame. None of them is ever stored, so none of them can be out of date.
- **It's small.** ImGui is a few files compiled into the editor, with no framework, no code generator, no event loop of its own.

### What it costs

- **CPU every frame.** Everything visible is laid out and drawn 60 times a second, even when nothing changed. For a few dozen rungs that's well under a millisecond; for thousands it would need culling (only drawing what's on screen, as the code editor already does for lines).
- **No native look.** ImGui draws its own widgets; it looks like a tool, not like a desktop application. For an engineering tool that's acceptable.
- **State that ImGui *does* keep** has to be identified by IDs. A text field being edited, an open popup, a tree node's open state: ImGui remembers them by an ID built from labels and `PushID`. Two blocks' "Edit element" popups must not share an ID, so the window pushes the address of each block's editor object (`PushID(editors_[current_].get())`), which never changes while the block exists. The block's index would change when blocks are deleted; its name when it's renamed.

---

## Order within a frame

In immediate mode, **when** in the frame something happens matters, and two bugs came from it:

- **A result one frame late.** The ST editor's error used to be re-checked at the start of the next frame, so for one frame after a keystroke the underline sat at the old position. The fix: re-check right after the edit, in the same frame (`drawStBlock` calls `checkCode()` as soon as the text changes).
- **A decision made before a change.** The window decided "the error is in this block", then drew the editor, whose edit fixed the error and cleared it, and the code below still used the error it had decided on: a read of an empty `std::optional`, caught by the Debug build's assertions. The fix: decide *after* the change, with a small lambda that looks at the current state each time it's asked.

Both have the same lesson: in a frame, derive things **after** the changes that affect them, or derive them again.

Popups follow the same thinking. An "Edit element" popup edits the rung in place, and the edit is recorded as one undo step when the popup closes. But a click elsewhere closes the popup *before* the frame's code runs, so `KopEditor::draw` checks at its very start whether a popup that was open is closed now, and records its edit before that click does anything else.

---

## Testing a UI without a screen

Because ImGui doesn't care where its input comes from, the tests create an ImGui context with no window and no GPU, and feed it events directly (`tests/ui/harness.hpp`): mouse moves, button presses, keys with modifiers, typed text, one frame at a time. Then they check the **model**: "after double-clicking the `start` contact and typing `stop`, network 0 is `stop => motor`".

Two pieces make that practical:
- **WidgetPositions.** The UI remembers where it drew a few named widgets in the last frame (`widgets_.remember("set all")`). A test then clicks a widget by name, the way a person would, without knowing the layout.
- **Time.** A double-click is two clicks close together in time, so the harness advances ImGui's clock by half a second between separate actions (`pause()`), or two unrelated clicks become a double-click.

Online tests add a third: the PLC runs on its own clock, so `waitFor(condition)` draws frames until the condition holds (or 3 s pass).

That's why the editor's UI has 36 test cases, where many GUI applications test their UI little or not at all: the tests drive the real UI code, through the same events a user produces, and all of them run in about a second.

---

## Review questions

**1.** After an undo, the KOP editor shows the old rung on the next frame. What code makes the drawing update, and what code would a retained-mode UI need instead?
<details><summary>Answer</summary>

None: the next frame draws the rungs from the model, which undo just changed, and recomputes their layout. A retained-mode UI would need code to find the widgets that showed the changed rung and rebuild or update them.
</details>

**2.** Why does each block's editor push the address of its editor object as an ImGui ID, and not the block's index?
<details><summary>Answer</summary>

ImGui keeps state (open popups, active text fields) by ID. The block's index changes when another block is deleted, and its name when it's renamed, which would hand one block's popup state to another. The editor object's address stays the same as long as the block exists.
</details>

**3.** The layout of every rung is recomputed 60 times a second. When would that stop being acceptable, and what would you do?
<details><summary>Answer</summary>

With very many rungs (thousands), where laying them all out each frame would take noticeable time. Then: only lay out and draw the rungs on screen (culling, as the code editor does for lines), and cache each rung's layout until that rung changes.
</details>
