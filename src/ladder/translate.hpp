#pragma once
#include "ladder/block.hpp"
#include "ladder/boxes.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Translates KOP blocks into Structured Text that soft_plc compiles, by
// following the power through each rung:
//
//   (start | motor) stop => motor          motor := (start OR motor) AND stop;
//   a {TON t1, PT := T#5s} => lamp         t1(IN := a, PT := T#5s);
//                                          lamp := t1.Q;
//   alarm => S:latched                     IF alarm THEN latched := TRUE; END_IF;
//
// Contacts in series are AND, parallel branches OR, a normally closed
// contact NOT. A box is a call with the rung's power on its power input, and
// the power goes on from its power output. Where a power value is needed
// more than once (several coils, boxes on a branch), it's kept in a
// generated BOOL variable, named _kop_power1, _kop_power2, ...
//
// soft_plc's ST only allows I/O addresses in AT declarations, so an operand
// like %QX0.0 gets a generated variable, _kop_QX0_0 AT %QX0.0 : BOOL (in the
// PROGRAM only: function blocks can't have AT).
namespace ladder {

// A network that couldn't be translated, and why. It's left out of the
// code (as a comment), so the rest of the block still compiles.
struct TranslationProblem {
    std::size_t network;
    std::string message;
};

// Which part of the translated text a line belongs to.
struct LineOrigin {
    std::size_t firstLine;              // where this part starts, from 0
    std::optional<std::size_t> network; // the network it was translated from; nothing for the interface
};

struct TranslatedBlock {
    std::string text;
    std::vector<LineOrigin> origins; // in order of firstLine
    std::vector<TranslationProblem> problems;

    // The network a line of `text` was translated from, if any.
    std::optional<std::size_t> networkAt(std::size_t line) const;
};

// Translates a KOP block. `boxes` are the box types it may use.
TranslatedBlock translateBlock(const Block& block, const BoxCatalog& boxes = standardBoxes());

// The whole project as one ST text, the way soft_plc compiles it: ST
// blocks as they are, KOP blocks translated.
struct TranslatedProject {
    struct Part {
        std::size_t block;
        std::size_t firstLine;       // where the block starts in `text`, from 0
        TranslatedBlock translation; // KOP blocks only: the block's own text and line origins
    };
    std::string text;
    std::vector<Part> parts; // in order of firstLine

    // The block and the line in the block's own text (its ST source, or its
    // translation) that a line of `text` comes from.
    std::optional<std::pair<std::size_t, std::size_t>> blockAt(std::size_t line) const;
};

TranslatedProject translateProject(const Project& project, const BoxCatalog& boxes = standardBoxes());

// The names the translation keeps for itself.
constexpr const char* kGeneratedPrefix = "_kop_";

// The variable generated for an address used as an operand: %QX0.0 is _kop_QX0_0.
std::string addressVariable(std::string_view address);

} // namespace ladder
