#include "st_support/diagnostics.hpp"
#include "st/analyzer.hpp"
#include "st/lexer.hpp"
#include "st/parser.hpp"
#include "util/text.hpp"
#include <algorithm>
#include <fmt/format.h>

namespace st_support {

namespace {

using ladder::BlockKind;
using softplc::st::TokenKind;

bool startsPou(TokenKind kind) {
    return kind == TokenKind::Program || kind == TokenKind::FunctionBlock || kind == TokenKind::Function ||
           kind == TokenKind::Type;
}

TokenKind keywordOf(BlockKind kind) {
    switch (kind) {
    case BlockKind::Program:
        return TokenKind::Program;
    case BlockKind::FunctionBlock:
        return TokenKind::FunctionBlock;
    case BlockKind::Function:
        return TokenKind::Function;
    case BlockKind::Types:
        return TokenKind::Type;
    }
    return TokenKind::Program;
}

// Does the block's text declare the block the project says it is? Only the
// headers are looked at; everything else is the compiler's job.
std::optional<StDiagnostic> checkHeader(const ladder::Block& block, std::size_t index) {
    const auto tokens = softplc::st::tokenize(block.source);
    if (!tokens) {
        return std::nullopt; // the compiler reports lexical errors, with their place
    }
    const auto expected =
        fmt::format("{}{}", ladder::kindName(block.kind), block.kind == BlockKind::Types ? "" : " " + block.name);
    const auto problem = [&](const softplc::st::Token& at, std::string message) {
        return StDiagnostic{index, at.location.line - 1, at.location.column - 1, std::move(message), std::nullopt};
    };
    const auto& first = tokens->front();
    if (first.kind != keywordOf(block.kind)) {
        return problem(first, fmt::format("block {} is a {}: its text should start with {}", block.name,
                                          ladder::kindName(block.kind), expected));
    }
    if (block.kind != BlockKind::Types) {
        const auto& name = (*tokens)[1];
        if (name.kind != TokenKind::Identifier || softplc::toUpper(name.text) != softplc::toUpper(block.name)) {
            return problem(name, fmt::format("the text declares {} {}, but the block is called {}: rename one of them",
                                             ladder::kindName(block.kind), name.text, block.name));
        }
    }
    // One block, one POU (a TYPE block may have several TYPE sections).
    for (std::size_t i = 1; i < tokens->size(); ++i) {
        const auto& token = (*tokens)[i];
        if (startsPou(token.kind) && !(block.kind == BlockKind::Types && token.kind == TokenKind::Type)) {
            return problem(token, fmt::format("a block holds just its own {}: put this {} in a block of its own",
                                              ladder::kindName(block.kind), token.text));
        }
    }
    return std::nullopt;
}

// Every instance of the project's function blocks inside `pou`, by path.
void collectInstances(const softplc::st::Pou& pou, const std::string& prefix,
                      std::map<std::string, std::vector<std::string>>& instances) {
    for (const auto& variable : pou.variables) {
        const auto* block = variable.functionBlock;
        if (block == nullptr || block->builtin) {
            continue;
        }
        const auto path = prefix + variable.name;
        instances[softplc::toUpper(block->name)].push_back(path);
        collectInstances(*block, path + ".", instances);
    }
}

// The box type of an ST function block, from its text.
std::optional<ladder::BoxType> stBox(const ladder::Block& block) {
    const auto pous = softplc::st::parseFunctionBlocks(block.source);
    if (!pous || pous->size() != 1) {
        return std::nullopt; // the check reports what's wrong with the text
    }
    std::vector<ladder::PinDeclaration> pins;
    for (const auto& variable : pous->front().variables) {
        if (variable.section != softplc::st::VarSection::Local) {
            pins.push_back(ladder::PinDeclaration{variable.name, softplc::st::formatTypeSpec(variable.typeSpec),
                                                  variable.section == softplc::st::VarSection::Input});
        }
    }
    return ladder::boxTypeFromInterface(pous->front().name, pins);
}

} // namespace

ladder::BoxCatalog projectBoxes(const ladder::Project& project) {
    ladder::BoxCatalog boxes;
    for (const auto& block : project.blocks) {
        if (block.kind != BlockKind::FunctionBlock) {
            continue;
        }
        std::optional<ladder::BoxType> type;
        if (block.language == ladder::Language::St) {
            type = stBox(block);
        } else {
            std::vector<ladder::PinDeclaration> pins;
            for (const auto& variable : block.variables) {
                if (variable.section != ladder::Section::Local) {
                    pins.push_back(ladder::PinDeclaration{variable.name, variable.type,
                                                          variable.section == ladder::Section::Input});
                }
            }
            type = ladder::boxTypeFromInterface(block.name, pins);
        }
        if (type) {
            boxes.add(std::move(*type));
        }
    }
    return boxes;
}

CodeCheck checkCode(const ladder::Project& project) {
    CodeCheck result{projectBoxes(project), {}, std::nullopt, {}};
    for (std::size_t i = 0; i < project.blocks.size(); ++i) {
        const auto& block = project.blocks[i];
        if (block.language != ladder::Language::St) {
            continue;
        }
        if (auto header = checkHeader(block, i)) {
            result.error = std::move(header);
            return result;
        }
    }
    result.translation = ladder::translateProject(project, result.boxes);
    auto source = result.translation.text;
    if (std::none_of(project.blocks.begin(), project.blocks.end(),
                     [](const ladder::Block& b) { return b.kind == BlockKind::Program; })) {
        source += "PROGRAM StandIn END_PROGRAM\n"; // the project's rules already say a PROGRAM is missing
    }

    const auto compiled = softplc::st::compileProgram(source);
    if (compiled) {
        collectInstances(compiled->program, "", result.instances);
        return result;
    }
    // Back from a line of the whole text to the block (and network) it came from.
    const auto errorLine = compiled.error().location.line - 1;
    const auto place = result.translation.blockAt(errorLine);
    if (!place) {
        return result; // can't happen: the first block starts at line 0
    }
    const auto [block, line] = *place;
    std::optional<std::size_t> network;
    if (project.blocks[block].language == ladder::Language::Kop) {
        network = result.translation.parts[block].translation.networkAt(line);
    }
    result.error = StDiagnostic{block, line, compiled.error().location.column - 1, compiled.error().message, network};
    return result;
}

} // namespace st_support
