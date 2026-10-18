# 6. Containers in the editor

soft_plc's [first design note](../../../soft_plc/docs/design/01-choosing-containers.md) explains the general rules: a `std::vector` unless there's a reason, a hash map for many lookups when order doesn't matter, a `std::map` when order or stable addresses matter. The editor follows the same rules, but its numbers are different: it handles a few dozen rungs and a few hundred variables, and it runs at 60 frames a second. At that size, several choices **don't matter for speed at all**, and the interesting reason for a container is usually something else. This note goes through them honestly, including the ones that are simply habit.

---

## `std::map` for determinism: the generated code

The translator keeps the addresses used as operands in a `std::map` (`translate.cpp`):

```cpp
std::map<std::string, std::string> addresses_; // operand address -> generated variable
```

and declares a generated variable for each, by walking the map:

```
_kop_IX0_0 AT %IX0.0 : BOOL;
_kop_QX0_0 AT %QX0.0 : BOOL;
```

A `std::unordered_map` would also find them quickly. But walking it gives an order that depends on the hash, and the C++ standard doesn't promise it's the same from one run (or library version) to the next. The order of these lines **matters**, even though the program means the same either way:
- **Export ST** writes this text to a file in git; lines that move around between exports make noisy diffs;
- the ST panel rebuilds its view whenever the generated text changes (`stShown_`): text that changed only in its order would reset the panel's scroll position for nothing.

So the rule: **when a container's order ends up in output, use one whose order is defined.** Here that's a sorted map; often it's a vector in the order things were found.

---

## `std::map` where it doesn't matter

`online::Session` keeps the values read from the PLC in a `std::map<std::string, std::string>`, keyed by the path in upper case. The UI looks values up there many times per frame. An `std::unordered_map` would make each lookup O(1) instead of O(log n), and nothing reads it in order. With fifty watched variables, log₂ 50 is about six string comparisons per lookup: a few hundred nanoseconds per frame in total. The choice was habit, and either would be fine. Changing it is a one-word edit, if a profile ever points here.

The forces map (`forces_`) is different: the watch table shows forces, and **Unforce all** walks them, so a stable order is nice to have there.

The instances chosen per function block in the window (`instances_`, keyed by block name) and the per-block instance lists from soft_plc (`CodeCheck::instances`) are maps for the same reason as the forces: they're small, and whatever reads them in order (the Instance combo) shows them the same way every time.

---

## Vectors with a linear search

Several places keep a small `std::vector` and search it with a loop or `std::find`:

| Where | How many | Searched how often |
|---|---|---|
| `RungPower::items` (`power.cpp`): each element's power | elements in one rung, ~10 | a few times per element, per frame, online |
| `PoweredPoints::points_` (`rung_view.cpp`): grid points with power | ~10–30 | repeatedly, until no net gains power |
| `KopEditor::watched_`: paths to read from the PLC | ~10–100 | once per operand drawn, to avoid duplicates |
| `WatchTable::watched_` | the table's lines | the same |

All of these are **O(n) per lookup** and some are O(n²) overall (adding n paths, each checked against the ones before). With n in the tens, that's a few thousand comparisons of short strings per frame: microseconds. A hash set or map would be faster in theory, slower to build for so few elements, and more code. The vector also keeps the **order** things were added, which the watch list sends to the PLC in that order.

The rule of thumb behind it: O(n²) is a problem when n can grow without bound. For "elements in one rung" it can't, in any program a person would write.

`RungPower` uses a vector of pairs instead of a `std::map<ItemPath, ItemPower>` for a second reason: `ItemPath` is a `std::variant` of paths and has no `<`, so a map would first need an ordering written for it. A loop needs only `==`, which the struct already has (defaulted).

---

## `std::set` for "seen before?"

`checkProject` uses `std::set<std::string>` to find duplicate block and variable names (in upper case, since ST ignores case), and `saveProject` uses sets of file names to find the files of deleted or renamed blocks: everything listed in the old `project.json` that isn't in the new one is removed. A set makes "is this in there?" one call, and the sizes are small enough that sorted or hashed doesn't matter.

---

## `std::deque` for the command queue

The session's queue of commands (`commands_`) is a deque, like soft_plc's request queue: the UI adds at the back, the session thread takes everything at once with a `swap`. A deque grows at both ends without moving its elements; the swap makes taking the whole queue O(1) under the lock, regardless of its length.

---

## `std::map` with `std::less<>`: lookups without copies

```cpp
std::map<std::string, ImVec2, std::less<>> centers_; // WidgetPositions
```

The tests look widgets up with string literals and `string_view`s (`center("set all")`). A `std::map<std::string, …>` with the default comparator would build a temporary `std::string` for every lookup. `std::less<>` is a **transparent** comparator: it can compare a `std::string` key with a `string_view` directly, so `find(std::string_view)` works and allocates nothing. One template argument, and a whole class of small allocations disappears.

---

## `std::optional` instead of magic values

The editor has about a hundred `std::optional`s, more than any other type after vectors and strings. Each one replaces a "special value" that would otherwise mean "nothing here":

| Instead of | The code has |
|---|---|
| a selection of rung −1 | `std::optional<Selection>` |
| an empty project meaning "never checked" | `std::optional<ladder::Project> checked_` |
| a saved-step count of −1 meaning "unreachable" | `std::optional<std::size_t> saved_` |
| a null pointer meaning "not online" | `std::optional<std::string> instance_` ("" is the PROGRAM, nothing is "not watched") |

The last one shows why magic values are dangerous: the empty string already *means* something (the PROGRAM's instance path), so it can't also mean "nothing". An `optional<string>` has room for both.

---

## The one `std::shared_ptr`

The translator builds a small expression tree for each rung's power (`Expr` in `translate.cpp`), and it's the only place in either project with a `std::shared_ptr`:

```cpp
using ExprPtr = std::shared_ptr<const Expr>;
```

The power entering a parallel branch is the starting point of **every** side of the branch, so the same subtree is part of several bigger expressions: the tree is really a DAG (a graph that shares parts). With `unique_ptr`, each side would need its own copy of the subtree. `shared_ptr` lets them share it, and `const` makes sharing safe: nobody can change a subtree that another expression is using.

It's the exception that shows the rule from soft_plc's note 3: `shared_ptr` is for things that **really have several owners**. Everything else in both projects has one.

---

## Review questions

**1.** The translator's address map is a `std::map`. What would actually go wrong with an `std::unordered_map`?
<details><summary>Answer</summary>

Nothing in the program's meaning, but the generated declarations could come out in a different order (between runs or library versions): exported `.st` files would show meaningless diffs, and any comparison of the generated text could see a change where there was none.
</details>

**2.** `KopEditor::watched_` avoids duplicates with `std::find` on a vector, which is O(n²) to build. Why is that fine, and when wouldn't it be?
<details><summary>Answer</summary>

It holds the operands visible in one block, tens of them, so it's a few thousand short comparisons per frame: microseconds. It wouldn't be fine if n could grow into the tens of thousands; then a hash set next to the vector (to keep the order) would be the fix.
</details>

**3.** Why does `RungPower` search a vector of pairs instead of using a `std::map<ItemPath, ItemPower>`?
<details><summary>Answer</summary>

`ItemPath` has `==` but no `<`, so a map would need an ordering written for it; a rung has few elements, so a linear search is just as fast and needs only `==`.
</details>

**4.** `instance_` is a `std::optional<std::string>`. What would break if it were a plain `std::string` with "" meaning "not watched"?
<details><summary>Answer</summary>

"" already means the PROGRAM (its instance path is empty). The PROGRAM would then look "not watched", or a FUNCTION (which can't be watched) would look like the PROGRAM. The optional keeps "no instance" and "the empty path" apart.
</details>
