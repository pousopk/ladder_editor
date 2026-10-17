#pragma once
#include "ladder/block.hpp"
#include <filesystem>
#include <string>
#include <tl/expected.hpp>

// A project on disk is a folder:
//
//   conveyor/
//     project.json          the project's name and its blocks, in order
//     Main.kop.json         a KOP block: its interface and its networks
//     Starter.st            an ST block: plain Structured Text
//
// project.json:
//   { "format": 1, "name": "conveyor",
//     "blocks": [ { "name": "Main", "kind": "PROGRAM", "language": "KOP", "file": "Main.kop.json" }, ... ] }
//
// A KOP block file keeps each network as its one-line notation (notation.hpp),
// so the files stay readable and diff well in git:
//   { "interface": [ { "name": "start", "type": "BOOL", "section": "VAR", "address": "%IX0.0" } ],
//     "networks":  [ { "comment": "Motor starter", "rung": "(start | motor) stop => motor" } ] }
namespace ladder {

// The file name a block is saved under: Main.kop.json, Starter.st.
std::string blockFileName(const Block& block);

// Reads a project folder, or says what's wrong and where.
tl::expected<Project, std::string> loadProject(const std::filesystem::path& folder);

// Writes the project into `folder` (made if needed). Each file is written to
// a temporary name first and then renamed over the old one, so a crash
// half-way never leaves a half-written file. Block files that the folder's
// previous project.json listed but that are no longer part of the project
// (deleted or renamed blocks) are removed.
tl::expected<void, std::string> saveProject(const Project& project, const std::filesystem::path& folder);

} // namespace ladder
