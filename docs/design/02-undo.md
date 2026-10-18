# 2. Undo as snapshots

Undo looks like a feature you add at the end. It isn't: if it's not designed in from the start, it can't be added reliably later. This note explains the shape it has here.

---

## The command pattern

Every change to a KOP block is an object (`src/ladder/history.hpp`):

```cpp
class Command {
public:
    virtual void apply(Block& block) const = 0;   // do, and redo
    virtual void revert(Block& block) const = 0;  // undo
    virtual std::string description() const = 0; // "insert contact", for the Undo button
    virtual bool absorb(const Command& next);     // merge with the next one?
};
```

The `History` keeps two stacks, done and undone. Undo takes the last done command, reverts it and moves it to the undone stack; redo does the opposite; any new change clears the undone stack (the history is a line, not a tree).

This is the classic **command pattern**, and `virtual` is the right tool here (soft_plc's design note 6): the operations are fixed (apply, revert, describe, absorb) and new kinds of command can be added without touching the history.

---

## The big decision: inverse operations or snapshots?

There are two ways for a command to undo itself:

**Inverse operations.** "Insert contact at path P" knows its inverse is "remove the element at path P". Small to store (just the operation and its arguments), and how many text editors work.

**Snapshots.** The command stores the rung **before** and the rung **after**. Undo puts "before" back; redo puts "after" back.

The editor uses snapshots for every change inside a network:

```cpp
class ReplaceRung : public Command {   // any change inside one network
    std::size_t index_;
    Rung before_;
    Rung after_;
};
```

Why snapshots won:
- **They're always exact.** Inverse operations are easy for "insert contact" and hard for "delete the element that was the last-but-one on a branch, which made `tidy()` dissolve the branch into its parent". Writing the exact inverse of every edit, including what `tidy()` did, is a source of bugs that only show up when someone undoes something unusual. A snapshot doesn't care what the edit did: the rung was this, then it was that.
- **One command type covers every edit.** Insert, delete, move, add branch, change an operand, change a coil to S: all are `ReplaceRung`. New edit operations need no undo code at all.
- **It's cheap here.** A rung is a small value (note 1), a few hundred bytes. Two copies per step and a limit of 500 steps is well under a megabyte, the worst case.

Snapshots get expensive when the document is big and changes are tiny: a megabyte of text with a copy per keystroke. The ST code editor (`text_document.cpp`) also uses snapshots (the whole text, cursor and selection), and gets away with it for two reasons: a burst of typing is **one** step, so one copy per burst rather than per letter; and an ST block is a few hundred lines at most. An editor for big files would store only what each step changed. The KOP history, for the same reason, snapshots one **rung**, not the whole block.

Commands that change the block's *structure* store what they need: `InsertNetwork` and `RemoveNetwork` keep the rung and its index, `ReplaceInterface` keeps the interface before and after, and `SwapNetworks` is its own inverse (swapping again undoes it).

---

## One step for one intention

A person thinks "I added a contact on `start`", not "I inserted an empty contact, then typed s, t, a, r, t". So the history merges steps that belong together:

- An operand typed into the popup **right after** inserting the element is merged into the insertion (`record(command, mergeWithLast = true)`): one undo removes the contact with its operand.
- `ReplaceRung::absorb` merges two edits of the **same** network by keeping the first one's "before" and taking the second one's "after". With snapshots, merging two steps is one assignment, which is another quiet benefit of them.
- A comment field or an interface cell is one step per editing session, from focus in to focus out, not one per letter.

---

## The saved point

"Is the block modified?" isn't "has anything been done since saving": undoing back to the saved state should make it unmodified again. The history remembers **how many commands were done** at the last save (`saved_`). Modified means "the done-count isn't that number".

Two subtle cases, both handled in `History::push`/`record`:
- If you undo past the saved point and then make a new change, the saved state can never be reached again (the commands that led to it were thrown away): `saved_` becomes empty, so the block stays modified until the next save.
- When the 500-step limit drops the oldest command, the saved count shifts down by one, because the steps are counted from the oldest one still kept.

`std::optional<std::size_t>` says "a step number, or *unreachable*" without a magic value like −1.

---

## Selections travel with commands

Each command also stores the selection before and after it. Undoing an edit selects what it was about, so after Ctrl+Z you can see what changed. It costs two small paths per command, and it's the difference between undo that feels trustworthy and undo that leaves you wondering what just happened.

---

## Review questions

**1.** Deleting the last-but-one element of a two-sided branch makes `tidy()` dissolve the branch. Why is undoing this easy with snapshots and hard with inverse operations?
<details><summary>Answer</summary>

The snapshot just puts back the rung as it was, branch included. An inverse operation would have to know that the delete also dissolved a branch, and rebuild it at the right place with the right sides: logic that has to exactly mirror every rule in `tidy()`.
</details>

**2.** The ST code editor snapshots the whole text for undo. Why isn't that a copy per keystroke, and when would it become a problem?
<details><summary>Answer</summary>

Typing letter after letter is grouped into one step, so there's one snapshot per burst of typing, not per letter. It would become a problem with very large texts (megabytes), where even one copy per step is costly; then storing only what each step changed would be better. ST blocks are small, so the simple version is fine.
</details>

**3.** You save, undo two steps, then insert a contact. Is the block modified? What does `saved_` hold now?
<details><summary>Answer</summary>

Modified. The two undone steps were thrown away by the new change, so the saved state can't be reached by undo or redo any more: `saved_` is empty (unreachable) until the next save.
</details>
