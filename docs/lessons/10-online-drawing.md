# Lesson 10: Clearer online drawing

Milestone 10 makes online mode easier to read at a glance, the way TIA Portal draws it:

```
 offline                       online, power flows          online, no power
 ──┤ ├────( )──               ━━┫ ┣━━━━( )━━  (green)       ─ ─┤ ├─ ─ ─( )─ ─  (dashed blue)
```

Try it: `./build/ladder_editor --simulate examples/conveyor`, open the watch table, **Set all**, go back to Main, and set `boxAtStart` TRUE.

---

## Part 1: Two different questions, two different signals

Online, a rung answers two separate questions, and the drawing keeps them apart:
1. **Where does power flow?** The wires: solid green with power, dashed blue without. The dashes matter: green against blue is hard to tell apart for many colour-blind people, and solid against dashed isn't.
2. **What state is each contact in?** The contact's symbol: green when it's **closed** (it would pass power), blue when it's open, plain when its value isn't known yet.

They differ often, and that's useful. In the example's network 4, `resetButton /boxAtEnd => R:jammed`: `boxAtEnd` is FALSE, so its normally closed contact is closed (green), but `resetButton` before it is open, so the wires around `boxAtEnd` are dashed. A closed contact with no power reaching it tells you exactly which contact to look at: the one before.

A coil is green when power reaches it, blue when not. For S and R coils that's not the operand's value: `jammed` stays TRUE after its S coil loses power, which is the whole point of a set coil.

---

## Part 2: Text that fits

A box is three columns wide, and each half holds a pin's name and operand. Before, the value was appended (`ET travelTime = T#3s919ms`) and clipped, so the value, the part you wanted, was what disappeared. Now:
- the value goes on its **own line**, under the operand, in green;
- an operand too long for its half is **cut short with `..`** (`fitText`), rather than clipped mid-letter;
- hovering a box shows a **tooltip** with every pin, its full operand and its value.

`fitText` measures with the current font (`ImGui::CalcTextSize`), shortens the text until it fits with `..`, and steps back over UTF-8 continuation bytes so a character like `ö` is never split (lesson 6 has the same rule for the cursor).

---

## Part 3: C++ concepts

### 3.1 An enum instead of two booleans
`Flow { Offline, Powered, Unpowered }` replaces "online?" and "powered?" passed together. Two booleans have four combinations, and "offline but powered" means nothing; the enum has exactly the three states there are, and `flowOf(online, powered)` is the one place that maps between them.

### 3.2 Dashes along any line
`wire()` draws a dashed line by walking along it in steps of dash + gap, as fractions of its length (`dx * at / length`), so the same code works for horizontal wires and vertical links. The dash length is a multiple of the line thickness, which already scales with the zoom, so dashes keep their proportions when zooming.

### 3.3 Tooltips on a drawn item
The rung is one `InvisibleButton`, and everything else is drawn straight into the draw list, which ImGui doesn't know as items. `ImGui::BeginItemTooltip()` works on the last item, the button, with ImGui's normal hover delay; the rung view already knows which box is under the mouse, so it fills the tooltip with that box.

---

## Part 4: Testing

- `ui/online_ui_test.cpp` (1 new case): `fitText` leaves short text alone, cuts long text to fit with `..`, and never splits a two-byte character.
- The drawing itself is checked by eye: the example connected to a standalone soft_plc, with the belt running and a box travelling (the screenshot in the milestone report). Colours and dashes are hard to assert on, and the logic behind them (`evaluatePower`, the powered nets) is already tested.

---

## Review questions

**1.** A contact is green, but the wires on both sides of it are dashed. What does that tell you?
<details><summary>Answer</summary>

The contact is closed (its operand is in the state that passes power), but no power reaches it: something before it in the rung is open. Look left, for the first blue contact.
</details>

**2.** Why are unpowered wires dashed, and not just a different colour?
<details><summary>Answer</summary>

So the difference doesn't depend on colour alone: green and blue can be hard to tell apart for colour-blind people, or on a poor screen, but solid and dashed can't be confused.
</details>

**3.** Online, `alarm => S:latched`: `alarm` went FALSE again. What does the coil look like, and what is `latched`?
<details><summary>Answer</summary>

The coil is blue (no power reaches it), but `latched` is still TRUE: a set coil only acts while powered and leaves the value as it is otherwise. The coil's colour shows the power, not the operand's value; the watch table or the ST panel shows the value.
</details>
