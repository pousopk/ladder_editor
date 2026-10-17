# Lesson 4: Undo and redo

Milestone 4 makes every change reversible. Each change is a **command** object that knows how to apply and revert itself; a **history** keeps them in order.

```
done:    [insert contact] [add branch] [edit comment]    ◄── Ctrl+Z takes the last one back
undone:  [delete network]                                 ◄── Ctrl+Y does it again
```

Try it: edit anything, then Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z), or the Undo / Redo buttons; their tooltips say what they'll undo or redo.

---

## Part 1: Concepts for the user

### 1.1 What counts as one step
One step is one thing *you* did, not one change the program made:
- inserting a contact **and typing its operand** is one step: one undo removes the contact;
- adding a network and typing its first coil's operand is one step;
- editing an existing operand (double-click, type, Enter) is its own step;
- a comment, however many letters, is one step when you leave the field.

### 1.2 Undo goes back to the saved state
The `*` after the file name means "different from the file". It comes from the history: undoing back to the moment of saving clears it again, and redoing past it brings it back. If you undo past the saved point and then do something new, the saved state can't be reached any more, so the `*` stays until you save.

### 1.3 A line, not a tree
After an undo, doing something new forgets what was undone: there's one line of history. That's how almost every editor works, TIA Portal included.

---

## Part 2: Design concepts

### 2.1 The command pattern
**Code:** `src/ladder/history.hpp`

```cpp
class Command {
public:
    virtual void apply(std::vector<Rung>& rungs) const = 0;  // do, and redo
    virtual void revert(std::vector<Rung>& rungs) const = 0; // undo
    virtual std::string description() const = 0;
    virtual bool absorb(const Command& next);                // merge the next step into this one
    std::optional<Selection> selectionBefore, selectionAfter;
};
```

Every change becomes an object, so it can be stored, undone, redone and described ("Undo insert contact"). The editor's actions no longer change the rungs themselves: they build a command and hand it to `History::perform`.

### 2.2 How much should a command remember?
There are two classic ways to make a command reversible:
- **Inverse operations:** "insert at path P" is undone by "remove at path P". Small, but every operation needs an exact inverse. Deleting with tidying (a branch disappears, its elements are lifted up) is hard to invert exactly.
- **Snapshots (mementos):** keep the state before and after. Always exact, but costs memory.

The editor uses snapshots, but only **per network**: `ReplaceRung` keeps one rung before and after. Rungs are plain values (lesson 1), so copying one is cheap and exact. Only changes to the list of networks get their own commands (`InsertNetwork`, `RemoveNetwork`, `SwapNetworks`), which have simple exact inverses.

### 2.3 Merging steps
**Code:** `ReplaceRung::absorb`, `History::record(command, mergeWithLast)`

The operand popup edits the rung directly, keystroke by keystroke, so the user sees each letter appear. When the popup opens, the editor keeps a copy of the rung (`PendingEdit`); when it closes, the editor *records* (not performs: it's already done) one `ReplaceRung` from that copy to the current rung. If the popup was opened by an insertion, the record asks to be merged: the insertion's command `absorb`s it, extending its "after" to include the typed operand. So one undo takes both away.

### 2.4 Restoring the selection
Each command remembers the selection before and after. Undo returns `selectionBefore`, redo `selectionAfter`, and the editor selects it: after undoing a delete, the element that came back is selected, as if it had never left.

### 2.5 The saved point
**Code:** `History::markSaved`, `History::modified`

The history remembers how many commands were done when the file was saved. Modified means "a different number are done now", which covers undoing back to it. Two cases make the saved state unreachable: doing something new after undoing past it (those undone commands are thrown away), and merging into the very step that was saved. Then `saved_` is cleared and `modified()` stays true.

### 2.6 A limit
The history keeps the newest 500 steps. Each one is small, but an editor left open for a day shouldn't grow without end.

---

## Part 3: C++ concepts

### 3.1 Runtime polymorphism, and owning objects of different types
`std::vector<std::unique_ptr<Command>>` holds commands of different classes. Each `unique_ptr<Command>` owns one object of some derived class (`ReplaceRung`, `InsertNetwork`, ...); calls go through the vtable (`command->revert(rungs)`), and the virtual destructor makes sure the right destructor runs. Moving a command from `done_` to `undone_` moves only the pointer.

### 3.2 `dynamic_cast` for "is the next command one of mine?"
```cpp
const auto* edit = dynamic_cast<const ReplaceRung*>(&next);
if (edit == nullptr || edit->index() != index_) return false;
```
`absorb` gets a `const Command&` and needs to know whether it's really a `ReplaceRung` of the same network. `dynamic_cast` checks the object's actual type at run time and gives `nullptr` if it isn't one. It's rarely the best tool (a virtual function is usually better), but for "merge two commands of the same kind" it's the standard answer.

### 3.3 A member function template defined in the `.cpp`
```cpp
template <class Change> void editRung(std::size_t index, std::string what, Change change, bool edit = false);
```
`editRung` takes any callable (each action passes a lambda). It's declared in the header but defined in `editor_window.cpp`: that's allowed because it's only ever used in that file, so the compiler sees the definition wherever it's instantiated.

### 3.4 Small adapters with overloads
The edit operations return `expected<ElementPath>`, `expected<CoilPath>`, `expected<optional<ElementPath>>` or `expected<optional<CoilPath>>`. Two function templates, `toItem(...)`, turn all four into the one type `editRung` needs. The compiler picks the right template from the argument's type.

### 3.5 Knowing when a field is being edited
ImGui says when a widget starts and stops being active: `IsItemActivated()` (the user clicked into the comment field) and `IsItemDeactivated()` (they left it). Between the two, edits go straight into the rung; at the end, one command is recorded. The edit popup is handled the same way: the copy is taken on the frame the popup appears (`IsWindowAppearing()`), and the step is recorded on the first frame it's no longer open.

---

## Part 4: Testing

- `history_test.cpp` (8 cases): undo and redo in order; forgetting undone commands; the network commands; selections; merging an operand into its insertion and a first edit into a new network, but not into another network; the saved point, including when it becomes unreachable; the limit.
- `ui/editor_ui_test.cpp` (7 new cases): insert + operand undone in one step and redone with its operand and selection; a deleted element coming back selected; Ctrl+Shift+Z; an operand edit as its own step; a new network and its operand; a deleted network; the unsaved mark following undo and redo; a comment as one step.

---

## Review questions

**1.** Why does `ReplaceRung` keep the whole rung before and after instead of an inverse operation?
<details><summary>Answer</summary>

Some operations are hard to invert exactly: deleting an element can remove a branch side and lift the remaining elements up a level. Keeping the rung before and after is always exact, and cheap, because a rung is a small plain value.
</details>

**2.** You insert a contact, type `guard`, press Enter, then press Ctrl+Z. What happens, and why not just the operand?
<details><summary>Answer</summary>

The whole contact goes away. The popup's edit was recorded with "merge with last", and the insertion's command absorbed it, so insertion and operand are one step.
</details>

**3.** You save, undo one step, then insert a new contact. Why does the `*` stay even if you undo the new contact?
<details><summary>Answer</summary>

The saved state included the step you undid. Doing something new threw that step away (the history is a line), so there's no way back to the saved state; `saved_` was cleared, and `modified()` stays true until you save again.
</details>

**4.** What does `History::record` do differently from `History::perform`?
<details><summary>Answer</summary>

`perform` applies the command and records it. `record` only records it, for changes already made directly (typing in a field); it can also merge the command into the last step.
</details>

**5.** Why is `dynamic_cast` used in `ReplaceRung::absorb`?
<details><summary>Answer</summary>

`absorb` receives the next command as a `const Command&` and can only merge another `ReplaceRung` of the same network. `dynamic_cast<const ReplaceRung*>` checks the real type at run time and gives `nullptr` for any other kind of command.
</details>

**6.** Why does the editor take the copy of the rung on the frame the popup *appears*, and not when the user presses Enter?
<details><summary>Answer</summary>

The popup changes the rung directly as the user types. When Enter is pressed, the rung already has the new operand; the "before" state only exists at the moment the popup opens.
</details>
