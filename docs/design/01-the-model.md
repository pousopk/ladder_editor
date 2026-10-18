# 1. A model that can't be wrong

The first decision in a ladder editor is how to store a rung. Everything else (drawing, editing, undo, saving, translating) is shaped by it.

---

## Option A: a grid

The obvious model is the one you see: a grid of cells, each holding a contact, a coil, a horizontal wire, a vertical wire, or nothing. It's how the screen looks and how some editors work inside.

Its problem is that **most grids are wrong**. A wire that ends in the middle of nowhere, a contact with nothing on its left, two branches that cross, a short circuit around a coil: the grid can hold all of them. So every operation needs to check the whole grid afterwards, every translation must first decide whether the grid even means something, and every bug in an edit operation produces a broken rung that's saved to disk.

## Option B: a tree (chosen)

```cpp
// src/ladder/rung.hpp
using Element = std::variant<Contact, Box, Parallel>;
struct Series   { std::vector<Element> elements; };   // one after another: AND
struct Parallel { std::vector<Series> branches; };    // side by side: OR
struct Rung     { std::string comment; Series logic; std::vector<Coil> coils; };
```

A rung is a series of elements; an element can be a parallel branch of series; and so on, as deep as needed. This can describe **every** rung built from series and parallel connections, and **nothing else**: there's no way to write down a loose wire, because there's no "wire" type at all. Wires are what the *drawing* shows between elements.

That's the most important design idea in the editor: **make invalid states unrepresentable**. The checks that remain (`check.hpp`) are about content, not structure: an empty operand, a branch with only one side, a box type that doesn't exist.

What the tree costs:
- **The screen is not the model.** Clicking a cell must be turned into "which element is this", and the drawing must be computed from the tree (the layout, note 3). That's real work (`layout.cpp`), done once.
- **Some ladders can't be drawn.** Bridge circuits, where a vertical wire connects two branches in the middle, aren't series-parallel. IEC ladders allow them in theory; real programs almost never use them, and as far as I know TIA Portal's LAD editor doesn't offer them either.

The tree also maps straight onto ST: series is AND, parallel is OR (`translate.cpp`). A grid would first have to be *recognised* as a series-parallel structure before it could be translated.

---

## Values, all the way down

`Rung`, `Block` and `Project` are plain structs of strings, vectors and variants, with a defaulted comparison:

```cpp
bool operator==(const Rung&) const = default;
```

No pointers between them, no identity, no hidden state. A `Rung` is a **value**, like an `int`: copying it gives an independent copy, and two rungs are equal when they contain the same things. That one property is what a surprising amount of the editor is built on:

| Feature | How it uses values |
|---|---|
| Undo | a command stores the rung before and after (note 2) |
| Edit operations | they work on a **copy**; if the result is rejected, the original was never touched |
| "Modified" | the history knows the state at the last save; the watch table compares with its saved copy |
| Rechecking | the window keeps the project as it was last checked, and checks again when `project_ != *checked_` |
| Online "older version" | the project sent to the PLC is kept, and compared with the current one |
| Tests | `CHECK(rung == *parseRung("a b => x"))` |

The price is copying: an edit copies a rung, a recheck compares a whole project. Rungs and projects are small (kilobytes), so a copy or comparison takes microseconds. A model with millions of elements would need something smarter (sharing unchanged parts between versions); this one doesn't.

---

## Paths, not pointers

How does the UI say "the second contact in the lower branch"? It could keep a pointer to the `Contact`. But every edit can move things: inserting an element into a vector moves the elements after it, copying a rung for undo makes new objects. A pointer into the model would dangle after almost any change.

Instead the editor uses **paths**:

```cpp
struct ElementPath { std::vector<std::size_t> steps; }; // element, side, element, side, ...
using ItemPath = std::variant<ElementPath, CoilPath>;
```

`{1, 0, 2}` means "element 1 of the rung; it's a branch; its side 0; element 2 there". A path:
- **survives copies**: it means the same thing in the copy;
- **can be stored** in the history, with each command's selection before and after;
- **can be checked**: `elementAt(rung, path)` returns null if the path no longer exists, instead of crashing.

The price: a path can point at a *different* element after an edit (deleting element 0 shifts everything left). So every edit operation **returns the path of what it touched** (`insertElement` returns where the new element landed), and the editor selects that.

---

## Edit operations return results, not exceptions

```cpp
tl::expected<ElementPath, std::string> insertElement(Rung& rung, const InsertPoint& point, Element element);
```

An edit can be refused for ordinary reasons ("this element isn't on a branch", "a rung needs at least one coil"), shown to the user in the status line. That's an expected outcome, so it's a value, the same "expected outside" rule as in soft_plc. After each removal, `tidy()` puts the tree back in its normal form: empty sides are dropped, and a branch left with one side becomes plain series in its parent. That keeps "the same rung" having one shape, which keeps `==` meaningful.

---

## A text notation

Every rung has a one-line text form (`notation.hpp`):

```
(start | motor) /stop {TON t1, PT := T#5s} => motor, S:alarm
```

It started as a way to write test cases quickly, and became the save format: KOP block files store each network as this text, so a project diff in git reads like a change to the program, not to a JSON tree. `parseRung(formatRung(r)) == r` is tested for each rung in the notation tests (contacts, branches inside branches, boxes, every coil kind), so the format doesn't lose anything they cover.

---

## Review questions

**1.** Name a rung a grid model could store but the tree can't, and say why that's an advantage.
<details><summary>Answer</summary>

A wire that ends nowhere, or a contact with nothing connected on its left. The tree has no wire type: elements are only ever in series or in parallel, so a disconnected piece can't be expressed, and no edit or file can produce one.
</details>

**2.** The selection is stored as a path. What happens to it when the element before it is deleted, and how does the editor deal with that?
<details><summary>Answer</summary>

Its index now points one element further on (or past the end). The edit operations return the path of what they changed, and the editor selects that result instead of keeping the old path; `elementAt` returns null for a path that no longer exists.
</details>

**3.** Why do the edit operations work on a copy of the rung?
<details><summary>Answer</summary>

So a refused edit leaves the original untouched (no half-done change), and so the history can record the rung before and after as one command. Copying a rung is cheap because it's a small value.
</details>
