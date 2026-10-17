#pragma once
#include "ladder/rung.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Blocks and projects: what the editor opens, edits and saves.
//
// A project is a set of blocks, like the program blocks of a PLC in TIA
// Portal. Each block is a PROGRAM, FUNCTION_BLOCK or FUNCTION (or a TYPE
// block with struct types), written either in KOP or in ST:
//
//   - a KOP block has an interface (its variables) and networks (rungs);
//   - an ST block is Structured Text, exactly as soft_plc reads it,
//     interface included.
//
// Milestone 7 translates the KOP blocks to ST; then the whole project is one
// ST program for soft_plc.
namespace ladder {

enum class BlockKind { Program, FunctionBlock, Function, Types };
enum class Language { Kop, St };

// The section a variable is declared in.
enum class Section {
    Input,  // VAR_INPUT: set by the caller
    Output, // VAR_OUTPUT: read by the caller
    Local,  // VAR: the block's own memory (in a FUNCTION: temporary, reset every call)
};

// One line of a block's interface.
struct Variable {
    std::string name;
    std::string type; // BOOL, INT, TIME, TON, ARRAY[1..3] OF INT, a struct type, ...
    Section section = Section::Local;
    std::string initial; // the initial value, as ST: 0, TRUE, T#5s; empty for the default
    std::string address; // AT address, as %IX0.0; only in a PROGRAM; empty for none
    std::string comment;

    bool operator==(const Variable&) const = default;
};

struct Block {
    std::string name;
    BlockKind kind = BlockKind::Program;
    Language language = Language::Kop;
    std::string returnType;          // FUNCTION only: the type it returns
    std::vector<Variable> variables; // KOP only (an ST block declares its own)
    std::vector<Rung> networks;      // KOP only
    std::string source;              // ST only: the whole text

    bool operator==(const Block&) const = default;
};

// One line of the project's watch table: a variable to watch online, and a
// value ready to write to it (like a watch and force table in TIA Portal).
struct WatchEntry {
    std::string path;   // as soft_plc names it: start, t1.ET, p1.demand
    std::string modify; // the value to write, as ST: TRUE, 42, T#5s; empty for none
    std::string comment;

    bool operator==(const WatchEntry&) const = default;
};

struct Project {
    std::string name;
    std::vector<Block> blocks;
    std::vector<WatchEntry> watch; // the watch table

    bool operator==(const Project&) const = default;
};

// "PROGRAM", "FUNCTION_BLOCK", "FUNCTION", "TYPE" and back.
const char* kindName(BlockKind kind);
std::optional<BlockKind> kindFromName(std::string_view name);
const char* languageName(Language language); // "KOP", "ST"
std::optional<Language> languageFromName(std::string_view name);
const char* sectionName(Section section); // "VAR_INPUT", "VAR_OUTPUT", "VAR"
std::optional<Section> sectionFromName(std::string_view name);

// The sections a block of this kind can have.
std::vector<Section> sectionsFor(BlockKind kind);

// A valid ST name: a letter or _, then letters, digits and _.
bool isIdentifier(std::string_view text);

// A new block as the editor creates it: an empty KOP block with one network,
// or an ST block with a skeleton to fill in.
Block newBlock(std::string name, BlockKind kind, Language language);

struct ProjectProblem {
    std::string block; // the block it's in; empty for the project as a whole
    std::string message;

    bool operator==(const ProjectProblem&) const = default;
};

// The project-level rules soft_plc needs: block names are valid and unique,
// there's exactly one PROGRAM, a FUNCTION has a return type, variable names
// are valid and unique in their block, sections and AT fit the block's kind,
// and KOP blocks are only PROGRAMs, FUNCTION_BLOCKs and FUNCTIONs. The
// rungs' own rules are check() in check.hpp; types and operands are checked
// by soft_plc once the project is translated (milestone 7).
std::vector<ProjectProblem> checkProject(const Project& project);

} // namespace ladder
