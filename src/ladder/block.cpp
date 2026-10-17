#include "ladder/block.hpp"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <set>

namespace ladder {

namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

} // namespace

const char* kindName(BlockKind kind) {
    switch (kind) {
    case BlockKind::Program:
        return "PROGRAM";
    case BlockKind::FunctionBlock:
        return "FUNCTION_BLOCK";
    case BlockKind::Function:
        return "FUNCTION";
    case BlockKind::Types:
        return "TYPE";
    }
    return "PROGRAM";
}

std::optional<BlockKind> kindFromName(std::string_view name) {
    for (const auto kind : {BlockKind::Program, BlockKind::FunctionBlock, BlockKind::Function, BlockKind::Types}) {
        if (upper(name) == kindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

const char* languageName(Language language) {
    return language == Language::Kop ? "KOP" : "ST";
}

std::optional<Language> languageFromName(std::string_view name) {
    if (upper(name) == "KOP")
        return Language::Kop;
    if (upper(name) == "ST")
        return Language::St;
    return std::nullopt;
}

const char* sectionName(Section section) {
    switch (section) {
    case Section::Input:
        return "VAR_INPUT";
    case Section::Output:
        return "VAR_OUTPUT";
    case Section::Local:
        return "VAR";
    }
    return "VAR";
}

std::optional<Section> sectionFromName(std::string_view name) {
    for (const auto section : {Section::Input, Section::Output, Section::Local}) {
        if (upper(name) == sectionName(section)) {
            return section;
        }
    }
    return std::nullopt;
}

std::vector<Section> sectionsFor(BlockKind kind) {
    switch (kind) {
    case BlockKind::Program:
        return {Section::Local};
    case BlockKind::FunctionBlock:
        return {Section::Input, Section::Output, Section::Local};
    case BlockKind::Function:
        return {Section::Input, Section::Local};
    case BlockKind::Types:
        return {};
    }
    return {};
}

bool isIdentifier(std::string_view text) {
    if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) != 0 || text.front() == '_')) {
        return false;
    }
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; });
}

Block newBlock(std::string name, BlockKind kind, Language language) {
    Block block;
    block.name = std::move(name);
    block.kind = kind;
    block.language = language;
    if (kind == BlockKind::Function) {
        block.returnType = "BOOL";
    }
    if (language == Language::Kop) {
        Rung rung;
        rung.coils.push_back(Coil{});
        block.networks.push_back(std::move(rung));
        return block;
    }
    switch (kind) {
    case BlockKind::Program:
        block.source = fmt::format("PROGRAM {}\nVAR\nEND_VAR\n\nEND_PROGRAM\n", block.name);
        break;
    case BlockKind::FunctionBlock:
        block.source = fmt::format("FUNCTION_BLOCK {}\nVAR_INPUT\nEND_VAR\nVAR_OUTPUT\nEND_VAR\nVAR\nEND_VAR\n\n"
                                   "END_FUNCTION_BLOCK\n",
                                   block.name);
        break;
    case BlockKind::Function:
        block.source = fmt::format("FUNCTION {} : BOOL\nVAR_INPUT\nEND_VAR\n\n{} := FALSE;\nEND_FUNCTION\n", block.name,
                                   block.name);
        break;
    case BlockKind::Types:
        block.source =
            fmt::format("TYPE\n    {} : STRUCT\n        value : INT;\n    END_STRUCT;\nEND_TYPE\n", block.name);
        break;
    }
    return block;
}

std::vector<ProjectProblem> checkProject(const Project& project) {
    std::vector<ProjectProblem> problems;
    const auto add = [&](const std::string& block, std::string message) {
        problems.push_back(ProjectProblem{block, std::move(message)});
    };

    std::set<std::string> names;
    std::size_t programs = 0;
    for (const auto& block : project.blocks) {
        if (!isIdentifier(block.name)) {
            add(block.name, fmt::format("'{}' isn't a valid block name (letters, digits and _, not starting with a "
                                        "digit)",
                                        block.name));
        } else if (!names.insert(upper(block.name)).second) {
            add(block.name, fmt::format("there's already a block called '{}'", block.name));
        }
        programs += block.kind == BlockKind::Program ? 1 : 0;
        if (block.kind == BlockKind::Types && block.language == Language::Kop) {
            add(block.name, "a TYPE block can only be written in ST");
        }
        if (block.kind == BlockKind::Function && block.language == Language::Kop && block.returnType.empty()) {
            add(block.name, "a FUNCTION needs a return type");
        }
        if (block.language != Language::Kop) {
            continue; // an ST block's text is checked by soft_plc
        }
        std::set<std::string> variables;
        const auto allowed = sectionsFor(block.kind);
        for (const auto& variable : block.variables) {
            if (!isIdentifier(variable.name)) {
                add(block.name, fmt::format("'{}' isn't a valid variable name", variable.name));
            } else if (!variables.insert(upper(variable.name)).second) {
                add(block.name, fmt::format("'{}' is declared twice", variable.name));
            } else if (upper(variable.name) == upper(block.name) && block.kind == BlockKind::Function) {
                add(block.name, fmt::format("'{}' is the function's own name, which holds its result", variable.name));
            }
            if (variable.type.empty()) {
                add(block.name, fmt::format("'{}' has no type", variable.name));
            }
            if (std::find(allowed.begin(), allowed.end(), variable.section) == allowed.end()) {
                add(block.name, fmt::format("a {} has no {} ('{}')", kindName(block.kind),
                                            sectionName(variable.section), variable.name));
            }
            if (!variable.address.empty() && block.kind != BlockKind::Program) {
                add(block.name, fmt::format("'{}': AT addresses are only allowed in the PROGRAM", variable.name));
            }
        }
    }
    if (programs == 0) {
        add("", "the project needs a PROGRAM: the block the PLC runs every cycle");
    } else if (programs > 1) {
        add("", "the project can only have one PROGRAM");
    }
    return problems;
}

} // namespace ladder
