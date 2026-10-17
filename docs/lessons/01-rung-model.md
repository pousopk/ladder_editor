# Lesson 1: The data model for rungs

Milestone 1 gives the editor its foundation: C++ types that describe a ladder (KOP) rung, a list of the function blocks that can sit in a rung as boxes, the checks a rung must pass, and a one-line text notation for rungs. There's no window yet; everything is tested with unit tests.

```
start (motor | /stop) {TON t1, PT := T#5s} => lamp
  │        │                 │                  │
Contact  Parallel           Box               Coil
   └────────── Series (the rung's logic) ──┘
```

---

## Part 1: PLC concepts

### 1.1 Rungs are series and parallel connections
**Code:** `src/ladder/rung.hpp`

Electrically, a rung is contacts in **series** (all must pass power: AND) and in **parallel** (any one may pass it: OR), between the left rail and the coils. Any rung built that way is a nested structure: a series contains elements, and an element may be a parallel branch whose sides are series again. That's exactly the C++ model:

```cpp
struct Parallel { std::vector<Series> branches; };
using Element = std::variant<Contact, Box, Parallel>;
struct Series { std::vector<Element> elements; };
struct Rung { std::string comment; Series logic; std::vector<Coil> coils; };
```

Some things are possible on a drawing but not in this model, for example a vertical link that connects the middle of one branch to the middle of another (a "bridge"). IEC ladder doesn't allow those either, and TIA Portal won't let you draw them. The model makes them impossible, which is what "structured editing" means: the editor can only produce rungs that make sense.

### 1.2 Contacts, coils and what they mean
| Element | Notation | Means |
|---|---|---|
| NO contact `┤ ├` | `start` | passes power when `start` is TRUE |
| NC contact `┤/├` | `/stop` | passes power when `stop` is FALSE |
| coil `( )` | `=> motor` | `motor` := the power |
| negated coil `(/)` | `=> /motor` | `motor` := NOT the power |
| set coil `(S)` | `=> S:alarm` | if power, `alarm` := TRUE (and it stays) |
| reset coil `(R)` | `=> R:alarm` | if power, `alarm` := FALSE |

A **normally closed contact on a stop button** is a classic source of confusion. The contact symbol says what the *program* checks, not what the button is. A stop button is usually wired normally closed (so a broken wire stops the machine), so the input is TRUE when the button is *not* pressed, and the rung uses a NO contact (`┤ ├`) on it. A `┤/├` on a NC-wired stop button would only run the motor while you hold stop.

### 1.3 Boxes: function blocks in the power flow
**Code:** `src/ladder/boxes.hpp`

A TON in a rung has power going **into** one input (IN) and **out of** one output (Q); its other pins are connected to operands (`PT := T#5s`, `ET => elapsed`). Which pin carries the power is a property of the block, so it's in a table (`standardBoxTypes()`): IN→Q for timers, CU→Q for CTU, CLK→Q for R_TRIG, S1→Q1 for SR. Connecting an operand to a power pin is an error the checks catch.

---

## Part 2: Design concepts

### 2.1 Make invalid states unrepresentable
A drawing could be stored as a grid of cells with wires between them, the way it looks on screen. Then a lot of grids would be nonsense (a wire to nowhere, a coil in the middle), and every piece of code would have to deal with that. The tree of series and parallels can't express most mistakes in the first place. What's left is checked by `check()` (`src/ladder/check.hpp`): empty operands, parallel branches with only one side, unknown boxes and pins. Where things go on screen will be *computed* from the tree (milestone 2), not stored.

### 2.2 A text notation as a testing tool
**Code:** `src/ladder/notation.hpp`

Building rungs by hand in C++ is long (see the first test in `rung_test.cpp`). A compact notation makes tests one line each, and it's a way to copy rungs as text later. Two properties make it trustworthy, and both are tested for every example:
- `formatRung(parseRung(text)) == text`: reading and writing back gives the same text;
- `parseRung(formatRung(rung)) == rung`: writing and reading back gives the same rung.

This is called a **round trip**. When both directions hold, the notation can't lose information.

The parser is a small recursive descent parser like soft_plc's: `series()` reads elements until something that can't start one; `parallel()` reads series separated by `|`; `box()` reads `{TYPE instance, PIN := value, PIN => variable}`.

---

## Part 3: C++ concepts

### 3.1 A recursive type through `std::vector`
`Parallel` contains `std::vector<Series>`, but `Series` is only declared at that point (`struct Series;`), not defined. Since C++17, `std::vector` may be declared with an incomplete element type, as long as the type is complete before the vector is really used. That's what lets the model be recursive without pointers. Compare soft_plc's syntax tree (its C++ lesson 4): a `BinaryExpression` has exactly two children, `left` and `right`, as named members rather than a vector. A member can't be of an incomplete type, so there the recursion needs a pointer, `std::unique_ptr<Expression>`.

### 3.2 Values all the way down: copying and comparing
Every part of a rung is a plain value: strings, vectors and variants of structs. So:
- `Rung b = a;` is a **deep copy**. Nothing is shared, and changing `b` never changes `a`. Undo/redo (milestone 4) will rely on this.
- `bool operator==(const Rung&) const = default;` compares everything, member by member, down through the vectors and variants (C++20 defaulted comparison). The tests compare whole rungs with one `==`.

### 3.3 `std::variant` again, with `std::visit` and overloads
`check()` walks the tree with
```cpp
std::visit([&](const auto& e) { check(e); }, element);
```
and three `check()` overloads (for `Contact`, `Parallel`, `Box`). That's the "generic lambda + overloads" pattern from soft_plc's analyzer: forget one overload and it doesn't compile.

### 3.4 `[[noreturn]]` and a default argument
`Parser::fail(message, back = 0)` reports the next token, or with `back = -1` the token just read. A default argument keeps the common call short. `[[noreturn]]` again tells the compiler that `fail` always throws.

---

## Part 4: Testing

- `rung_test.cpp`: building rungs by hand, deep copies and comparison, the box table.
- `notation_test.cpp`: reading every kind of element; eight round trips; spacing; a table of errors with their exact messages and columns.
- `check_test.cpp`: correct rungs have no problems; each rule, including those the notation can't even write (a parallel branch with one side), built by hand.

---

## Review questions

**1.** Write `start (motor | /stop) => motor` as a sentence in ST.
<details><summary>Answer</summary>

`motor := start AND (motor OR NOT stop);`. The series is AND, the parallel branch is OR, and the NC contact is NOT. (Milestone 7 generates exactly this kind of ST.)
</details>

**2.** Why can't the model describe a "bridge" between two parallel branches, and why is that good?
<details><summary>Answer</summary>

The model only has series and parallel nesting, and a bridge is neither. IEC ladder and TIA Portal don't allow bridges either, so the editor never has to deal with them, and every rung it can hold can be translated to ST.
</details>

**3.** What do the two round-trip checks prove about the notation?
<details><summary>Answer</summary>

`format(parse(text)) == text` shows that reading loses nothing that writing needs; `parse(format(rung)) == rung` shows that writing loses nothing that reading needs. Together, the text and the rung carry the same information.
</details>

**4.** `{TON t1, IN := start} => lamp`: what's wrong, and how should it be written?
<details><summary>Answer</summary>

IN is the TON's power input: it's fed by the rung, not by an operand. Write the contact before the box instead: `start {TON t1, PT := T#5s} => lamp`.
</details>

**5.** `Rung b = a; b.coils[0].operand = "x";`. Does `a` change?
<details><summary>Answer</summary>

No. A `Rung` is made only of values (strings, vectors, variants), so the copy is deep and nothing is shared.
</details>

**6.** A stop button is wired normally closed. Which contact do you use for it in the rung, `stop` or `/stop`?
<details><summary>Answer</summary>

`stop`, a normally open contact. With an NC-wired button, the input is TRUE while the button is *not* pressed, so the rung must pass power when the input is TRUE. The contact symbol describes the program's check, not the button's wiring.
</details>
