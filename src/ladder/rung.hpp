#pragma once
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

// The data model of a ladder (KOP) program: rungs made of contacts, boxes
// and parallel branches, ending in coils.
//
//   start      stop                 motor
//  ──┤ ├──┬──┤/├──┬── [TON t1] ─────( )──
//         │       │
//   motor │       │
//  ──┤ ├──┘       │
//
// A rung is a *series* of elements that power flows through from the left
// rail to the coils on the right. An element is a contact, a box (a function
// block such as TON), or a *parallel* branch: two or more series side by
// side, where power gets through if it gets through any of them. Because a
// parallel branch holds series, which hold elements, which can be parallel
// branches again, any ladder network built from series and parallel parts
// can be represented, and nothing else can: every rung is valid by
// construction (see check.hpp for the rules that remain).
namespace ladder {

// ---------------------------------------------------------------- Elements

enum class ContactKind {
    NormallyOpen,   // ──┤ ├──  passes power when the operand is TRUE
    NormallyClosed, // ──┤/├──  passes power when the operand is FALSE
};

struct Contact {
    ContactKind kind = ContactKind::NormallyOpen;
    std::string operand; // what it reads: start, %IX0.0, t1.Q, sensors[2]

    bool operator==(const Contact&) const = default;
};

// One connection of a box: an input it reads, or an output it writes.
struct Pin {
    std::string name;    // the function block's input or output: PT, ET, PV, ...
    std::string operand; // the value or variable connected to it: T#5s, elapsed

    bool operator==(const Pin&) const = default;
};

// A function block in the rung, like a TON. Power flows into one of its
// inputs (IN for a TON) and out of one of its outputs (Q); the other inputs
// and outputs are connected to operands (see boxes.hpp).
struct Box {
    std::string type;     // TON, CTU, R_TRIG, ...
    std::string instance; // the instance it calls: t1
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    bool operator==(const Box&) const = default;
};

struct Series;

// Two or more series side by side: power gets through if any of them passes it.
struct Parallel {
    std::vector<Series> branches;

    bool operator==(const Parallel&) const = default;
};

using Element = std::variant<Contact, Box, Parallel>;

// Elements one after another: power gets through if every one passes it.
struct Series {
    std::vector<Element> elements;

    bool operator==(const Series&) const = default;
};

// ---------------------------------------------------------------- Coils

enum class CoilKind {
    Normal,  // ──( )──  the operand follows the power: TRUE with power, FALSE without
    Negated, // ──(/)──  the opposite
    Set,     // ──(S)──  power sets the operand TRUE; it stays TRUE without power
    Reset,   // ──(R)──  power sets the operand FALSE; it stays FALSE without power
};

struct Coil {
    CoilKind kind = CoilKind::Normal;
    std::string operand;

    bool operator==(const Coil&) const = default;
};

// ---------------------------------------------------------------- Rungs

// One rung (a "network" in TIA Portal): the logic, then one or more coils,
// all fed by the power that comes out of the logic.
struct Rung {
    std::string comment;
    Series logic; // may be empty: the coils are then always powered
    std::vector<Coil> coils;

    bool operator==(const Rung&) const = default;
};

// ---------------------------------------------------------------- Helpers

// How many elements a series holds, counting those inside parallel branches
// (and the branches' own elements) too.
std::size_t countElements(const Series& series);

} // namespace ladder
