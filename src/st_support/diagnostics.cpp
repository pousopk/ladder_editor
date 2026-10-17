#include "st_support/diagnostics.hpp"
#include "st/analyzer.hpp"
#include "st/lexer.hpp"
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
        return StDiagnostic{index, at.location.line - 1, at.location.column - 1, std::move(message)};
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

std::size_t lineCount(const std::string& text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

} // namespace

std::optional<StDiagnostic> checkStBlocks(const ladder::Project& project) {
    // Every ST block's text, one after the other, remembering where each starts.
    std::string combined;
    std::vector<std::pair<std::size_t, std::size_t>> starts; // (first line in `combined`, block index)
    bool stProgram = false;
    std::size_t line = 0;
    for (std::size_t i = 0; i < project.blocks.size(); ++i) {
        const auto& block = project.blocks[i];
        if (block.language != ladder::Language::St) {
            continue;
        }
        if (auto header = checkHeader(block, i)) {
            return header;
        }
        starts.emplace_back(line, i);
        combined += block.source;
        combined += '\n';
        line += lineCount(block.source);
        stProgram = stProgram || block.kind == BlockKind::Program;
    }
    if (starts.empty()) {
        return std::nullopt;
    }
    if (!stProgram) {
        combined += "PROGRAM StandIn END_PROGRAM\n"; // the KOP PROGRAM, until it's translated
    }

    const auto compiled = softplc::st::compileProgram(combined);
    if (compiled) {
        return std::nullopt;
    }
    // Back from a line in the combined text to the block it came from.
    const auto errorLine = compiled.error().location.line - 1;
    auto it = std::upper_bound(starts.begin(), starts.end(), std::pair{errorLine, project.blocks.size()});
    if (it == starts.begin()) {
        return std::nullopt; // can't happen: the first block starts at line 0
    }
    --it;
    return StDiagnostic{it->second, errorLine - it->first, compiled.error().location.column - 1,
                        compiled.error().message};
}

} // namespace st_support
